// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once
#include "library/SongCatalog.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace OpenHDK {
// Pure control-path models. No filesystem, live ID construction or dispatch.
enum class LibraryCliErrorCode { Syntax, LimitExceeded, InvalidText, StorageFailure };
struct LibraryCliError { LibraryCliErrorCode code; std::size_t offset{}; };
struct LibraryCliToken { std::string bytes; bool quoted{}; std::size_t offset{}; };
struct LibraryCliTokens {
  std::vector<LibraryCliToken> tokens;
  std::optional<LibraryCliError> error;
};
inline constexpr std::size_t kLibraryCliLineBytes = 16384U;
inline constexpr std::size_t kLibraryCliTokenBytes = 4096U;
inline constexpr std::size_t kLibraryCliTokens = 8U;

// The reader never retains more than one bounded line. Oversized input is
// drained through LF; CR immediately before LF is transport framing only.
class LibraryCliLineReader {
public:
  enum class Status { More, Line, Rejected };
  Status push(char byte) {
    if (byte == '\n') {
      if (overflow_) { overflow_ = false; line_.clear(); return Status::Rejected; }
      if (!line_.empty() && line_.back() == '\r') line_.pop_back();
      return Status::Line;
    }
    if (overflow_) return Status::More;
    if (line_.size() == kLibraryCliLineBytes) {
      overflow_ = true; error_ = LibraryCliError{LibraryCliErrorCode::LimitExceeded,kLibraryCliLineBytes};
      line_.clear(); return Status::More;
    }
    try { line_.push_back(byte); }
    catch(const std::bad_alloc&) {
      overflow_ = true; error_ = LibraryCliError{LibraryCliErrorCode::StorageFailure,line_.size()}; line_.clear();
    }
    return Status::More;
  }
  Status end() const noexcept { return overflow_ ? Status::Rejected : pending() ? Status::Line : Status::More; }
  std::string_view line() const noexcept { return line_; }
  bool pending() const noexcept { return overflow_ || !line_.empty(); }
  bool overflow() const noexcept { return overflow_; }
  std::optional<LibraryCliError> error() const noexcept { return error_; }
  void reset() noexcept { line_.clear(); overflow_ = false; error_.reset(); }
private:
  std::string line_;
  bool overflow_{};
  std::optional<LibraryCliError> error_;
};

inline LibraryCliTokens tokenizeLibraryCommand(std::string_view line) {
  const auto fail=[](LibraryCliErrorCode c,std::size_t p) { return LibraryCliTokens{{},LibraryCliError{c,p}}; };
  if(line.size()>kLibraryCliLineBytes) return fail(LibraryCliErrorCode::LimitExceeded,kLibraryCliLineBytes);
  try {
    LibraryCliTokens out;
    std::size_t at=0;
    const auto space=[](char c){return c==' ' || c=='\t';};
    while(at<line.size()) {
      while(at<line.size() && space(line[at])) ++at;
      if(at==line.size()) break;
      if(out.tokens.size()==kLibraryCliTokens) return fail(LibraryCliErrorCode::LimitExceeded,at);
      LibraryCliToken token{{},line[at]=='"',at};
      bool closed=!token.quoted;
      if(token.quoted) ++at;
      while(at<line.size()) {
        char c=line[at];
        if(token.quoted && c=='"') { ++at;closed=true;break; }
        if(!token.quoted && space(c)) break;
        if(!token.quoted && (c=='"' || c=='\\')) return fail(LibraryCliErrorCode::Syntax,at);
        if(token.quoted && c=='\\') {
          ++at;
          if(at==line.size() || (line[at]!='"' && line[at]!='\\')) return fail(LibraryCliErrorCode::Syntax,at);
          c=line[at];
        }
        if((static_cast<unsigned char>(c)<32U && !(token.quoted && (c=='\t' || c=='\r'))) || c==127)
          return fail(LibraryCliErrorCode::InvalidText,at);
        if(token.bytes.size()==kLibraryCliTokenBytes) return fail(LibraryCliErrorCode::LimitExceeded,at);
        token.bytes.push_back(c);++at;
      }
      if(!closed || (at<line.size() && !space(line[at]))) return fail(LibraryCliErrorCode::Syntax,at);
      out.tokens.push_back(std::move(token));
    }
    return out;
  } catch(const std::bad_alloc&) { return fail(LibraryCliErrorCode::StorageFailure,0); }
}

// Map decoded text diagnostics back to the original line, including escapes.
inline std::size_t libraryCliTokenOffset(std::string_view line,const LibraryCliToken& token,std::size_t decoded) noexcept {
  std::size_t at=token.offset+(token.quoted?1U:0U);
  for(std::size_t n=0;n<decoded && at<line.size();++n) {
    if(token.quoted && line[at]=='\\') ++at;
    if(at<line.size()) ++at;
  }
  return at;
}

inline std::optional<std::uint64_t> libraryCliNumber(std::string_view bytes) noexcept {
  if(bytes.empty()) return {};
  for(char c:bytes) if(c<'0' || c>'9') return {};
  std::uint64_t value{};
  const auto result=std::from_chars(bytes.data(),bytes.data()+bytes.size(),value);
  if(result.ec!=std::errc{} || result.ptr!=bytes.data()+bytes.size()) return {};
  return value;
}
// Diagnostic selector only: resolution must reuse typed IDs in owned snapshots.
inline std::optional<std::uint64_t> libraryCliSelector(std::string_view bytes) noexcept {
  const auto n=libraryCliNumber(bytes);
  if(!n || *n==0 || *n==std::numeric_limits<std::uint64_t>::max()) return {};
  return n;
}
enum class LibraryCliVerb { Help, Quit, Roots, Songs, Register, Reattach, Scan, Relocate, Remove, Overrides, Prepare };
struct LibraryCliCommand {
  LibraryCliVerb verb{LibraryCliVerb::Help};
  std::optional<std::uint64_t> song,root;
  std::size_t offset{},count{};
  std::string path,locator;
  RootSourcePolicy policy{};
  std::optional<std::string> title,artist;
};
struct LibraryCliCommandResult {
  std::optional<LibraryCliCommand> command;
  std::optional<LibraryCliError> error;
};
inline LibraryCliCommandResult parseLibraryCommand(std::string_view line) {
  const auto fail=[](LibraryCliErrorCode c,std::size_t p=0){return LibraryCliCommandResult{{},LibraryCliError{c,p}};};
  auto lex=tokenizeLibraryCommand(line);
  if(lex.error) return fail(lex.error->code,lex.error->offset);
  const auto& t=lex.tokens;
  if(t.empty()) return fail(LibraryCliErrorCode::Syntax);
  try {
    LibraryCliCommand cmd;
    const auto& verb=t[0].bytes;
    if(t[0].quoted) return fail(LibraryCliErrorCode::Syntax);
    if(verb=="help" || verb=="quit" || verb=="roots") {
      if(t.size()!=1) return fail(LibraryCliErrorCode::Syntax);
      cmd.verb=verb=="help"?LibraryCliVerb::Help:verb=="quit"?LibraryCliVerb::Quit:LibraryCliVerb::Roots;
    } else if(verb=="register") {
      if(t.size()!=2 && t.size()!=4) return fail(LibraryCliErrorCode::Syntax);
      cmd.verb=LibraryCliVerb::Register;cmd.path=t[1].bytes;
      if(cmd.path.empty()) return fail(LibraryCliErrorCode::InvalidText,t[1].offset);
      if(t.size()==4) {
        if(t[2].bytes=="utf8") cmd.policy.lyrics.encoding=LyricTextEncoding::Utf8;
        else if(t[2].bytes=="tis620") cmd.policy.lyrics.encoding=LyricTextEncoding::Tis620;
        else return fail(LibraryCliErrorCode::Syntax,t[2].offset);
        if(t[3].bytes!="auto") {
          auto n=libraryCliNumber(t[3].bytes);
          if(!n || *n>std::numeric_limits<std::size_t>::max()) return fail(LibraryCliErrorCode::Syntax,t[3].offset);
          cmd.policy.lyrics.trackIndex=static_cast<std::size_t>(*n);
        }
      }
    } else if(verb=="songs") {
      if(t.size()!=4) return fail(LibraryCliErrorCode::Syntax);
      cmd.verb=LibraryCliVerb::Songs;cmd.root=libraryCliSelector(t[1].bytes);
      auto offset=libraryCliNumber(t[2].bytes),count=libraryCliNumber(t[3].bytes);
      if(!cmd.root || !offset || *offset>std::numeric_limits<std::size_t>::max() || !count || *count==0 || *count>100) return fail(LibraryCliErrorCode::Syntax);
      cmd.offset=static_cast<std::size_t>(*offset);cmd.count=static_cast<std::size_t>(*count);
    } else {
      std::size_t size=2;
      if(verb=="reattach") {cmd.verb=LibraryCliVerb::Reattach;size=3;}
      else if(verb=="scan") cmd.verb=LibraryCliVerb::Scan;
      else if(verb=="relocate") {cmd.verb=LibraryCliVerb::Relocate;size=4;}
      else if(verb=="remove") cmd.verb=LibraryCliVerb::Remove;
      else if(verb=="overrides") {cmd.verb=LibraryCliVerb::Overrides;size=4;}
      else if(verb=="prepare") cmd.verb=LibraryCliVerb::Prepare;
      else return fail(LibraryCliErrorCode::Syntax);
      if(t.size()!=size) return fail(LibraryCliErrorCode::Syntax);
      auto selector=libraryCliSelector(t[1].bytes);
      if(!selector) return fail(LibraryCliErrorCode::Syntax,t[1].offset);
      if(verb=="reattach" || verb=="scan") cmd.root=selector;else cmd.song=selector;
      if(verb=="reattach") {cmd.path=t[2].bytes;if(cmd.path.empty()) return fail(LibraryCliErrorCode::InvalidText,t[2].offset);}
      if(verb=="relocate") {
        cmd.root=libraryCliSelector(t[2].bytes);cmd.locator=t[3].bytes;
        if(!cmd.root || !isCatalogLocator(cmd.locator)) return fail(LibraryCliErrorCode::Syntax,t[2].offset);
      }
      if(verb=="overrides") {
        for(std::size_t i=2;i<4;++i) {
          if(!t[i].quoted && t[i].bytes=="-") continue;
          auto text=MetadataText::create(t[i].bytes);
          if(!text.succeeded()) return fail(LibraryCliErrorCode::InvalidText,libraryCliTokenOffset(line,t[i],text.error()->byteOffset));
          (i==2?cmd.title:cmd.artist)=t[i].bytes;
        }
      }
    }
    return {std::move(cmd),{}};
  } catch(const std::bad_alloc&) {return fail(LibraryCliErrorCode::StorageFailure);}
}

enum class LibraryCliMode { Help, Create, Session };
struct LibraryCliEntry { LibraryCliMode mode;std::string parent,primary; };
struct LibraryCliEntryResult { std::optional<LibraryCliEntry> entry;std::optional<LibraryCliError> error; };
inline LibraryCliEntryResult parseLibraryEntry(std::span<const std::string_view> args) {
  const auto fail=[](LibraryCliErrorCode c){return LibraryCliEntryResult{{},LibraryCliError{c,0}};};
  if(args.size()==1 && args[0]=="--help") return {LibraryCliEntry{LibraryCliMode::Help,{},{}},{}};
  if(args.size()!=5) return fail(LibraryCliErrorCode::Syntax);
  try {
    LibraryCliEntry entry{LibraryCliMode::Session,{},{}};
    for(std::size_t i=0;i<4;i+=2) {
      if(args[i+1].empty() || args[i+1].size()>kLibraryCliTokenBytes) return fail(LibraryCliErrorCode::LimitExceeded);
      if(args[i]=="--store-dir" && entry.parent.empty()) entry.parent=args[i+1];
      else if(args[i]=="--store-name" && entry.primary.empty()) entry.primary=args[i+1];
      else return fail(LibraryCliErrorCode::Syntax);
    }
    if(entry.parent.empty() || entry.primary.empty()) return fail(LibraryCliErrorCode::Syntax);
    if(entry.parent[0]!='/' || (entry.parent!="/" && (!isCatalogLocator(std::string_view(entry.parent).substr(1))
        || 1U+static_cast<std::size_t>(std::count(entry.parent.begin()+1,entry.parent.end(),'/'))>32U))
        || entry.primary.size()>128U || !isCatalogLocator(entry.primary)
        || entry.primary.find('/')!=std::string::npos || entry.primary.ends_with(".ohk-lock")
        || entry.primary.starts_with(".ohk-stage-")) return fail(LibraryCliErrorCode::Syntax);
    if(args[4]=="create") entry.mode=LibraryCliMode::Create;
    else if(args[4]!="session") return fail(LibraryCliErrorCode::Syntax);
    return {std::move(entry),{}};
  } catch(const std::bad_alloc&) {return fail(LibraryCliErrorCode::StorageFailure);}
}
} // namespace OpenHDK
