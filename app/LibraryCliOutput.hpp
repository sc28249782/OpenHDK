// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once
#include "app/LibraryCliGrammar.hpp"
#include <algorithm>

namespace OpenHDK {
// Borrowed UTF-8 validation permits all ASCII; JSON escaping handles controls.
inline bool libraryCliUtf8(std::string_view text) noexcept {
  for(std::size_t i=0;i<text.size();) {
    const auto c=static_cast<unsigned char>(text[i]);std::size_t n=1;
    if(c<128U) {++i;continue;}
    if(c>=0xc2U && c<=0xdfU) n=2;
    else if(c>=0xe0U && c<=0xefU) n=3;
    else if(c>=0xf0U && c<=0xf4U) n=4;
    else return false;
    if(n>text.size()-i) return false;
    for(std::size_t j=1;j<n;++j) {
      const auto d=static_cast<unsigned char>(text[i+j]);
      if(d<0x80U || d>0xbfU || (j==1 && ((c==0xe0U && d<0xa0U) || (c==0xedU && d>0x9fU)
          || (c==0xf0U && d<0x90U) || (c==0xf4U && d>0x8fU)))) return false;
    }
    i+=n;
  }
  return true;
}
struct LibraryCliOutputResult { std::optional<std::string> bytes;std::optional<LibraryCliError> error; };
class LibraryCliJsonRecord {
public:
  static constexpr std::size_t kMaxBytes=65536U;
  explicit LibraryCliJsonRecord(std::size_t limit=kMaxBytes):limit_(limit) {
    if(limit<3 || limit>kMaxBytes) reject(LibraryCliErrorCode::LimitExceeded);
  }
  bool text(std::string_view key,std::string_view value) {
    if(value.size()>kLibraryCliTokenBytes) return reject(LibraryCliErrorCode::LimitExceeded);
    if(!libraryCliUtf8(value)) return reject(LibraryCliErrorCode::InvalidText);
    return field(key,[&](std::string& b){quote(b,value);});
  }
  // Byte-safe tagged representation; no raw control bytes or lossy conversion.
  bool nativeText(std::string_view key,std::string_view value) {
    if(value.size()>kLibraryCliTokenBytes) return reject(LibraryCliErrorCode::LimitExceeded);
    const bool utf8=libraryCliUtf8(value);
    return field(key,[&](std::string& b){
      b+="{\"encoding\":";quote(b,utf8?"utf8":"bytes-hex");b+=",\"value\":";
      if(utf8) quote(b,value);
      else {b+='"';for(unsigned char c:value){b+=hex(c>>4);b+=hex(c&15U);}b+='"';}
      b+='}';
    });
  }
  bool number(std::string_view key,std::uint64_t value) {
    return field(key,[&](std::string& b){std::array<char,20> digits{};auto p=std::to_chars(digits.data(),digits.data()+digits.size(),value);b.append(digits.data(),p.ptr);});
  }
  bool boolean(std::string_view key,bool value) {return field(key,[&](std::string& b){b+=value?"true":"false";});}
  bool null(std::string_view key) {return field(key,[](std::string& b){b+="null";});}
  LibraryCliOutputResult finish() const {
    if(error_) return {{},error_};
    try {auto result=bytes_;result+="}\n";return {std::move(result),{}};}
    catch(const std::bad_alloc&) {return {{},LibraryCliError{LibraryCliErrorCode::StorageFailure,0}};}
  }
private:
  static char hex(unsigned n) noexcept {return "0123456789abcdef"[n];}
  static void quote(std::string& b,std::string_view text) {
    b+='"';
    for(std::size_t i=0;i<text.size();++i) {
      const auto c=static_cast<unsigned char>(text[i]);
      if(c==0xc2U && i+1U<text.size()) {
        const auto next=static_cast<unsigned char>(text[i+1U]);
        if(next>=0x80U && next<=0x9fU) {b+="\\u00";b+=hex(next>>4);b+=hex(next&15U);++i;continue;}
      }
      if(c=='"' || c=='\\') {b+='\\';b+=static_cast<char>(c);}
      else if(c<32U || c==127U) {b+="\\u00";b+=hex(c>>4);b+=hex(c&15U);}
      else b+=static_cast<char>(c);
    }
    b+='"';
  }
  bool reject(LibraryCliErrorCode c) {if(!error_) error_=LibraryCliError{c,0};return false;}
  template<class Emit> bool field(std::string_view key,Emit emit) {
    if(error_) return false;
    if(key.empty() || key.size()>64 || keys_.size()==64) return reject(LibraryCliErrorCode::LimitExceeded);
    for(char c:key) if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_')) return reject(LibraryCliErrorCode::Syntax);
    if(std::find(keys_.begin(),keys_.end(),key)!=keys_.end()) return reject(LibraryCliErrorCode::Syntax);
    try {
      std::string value;emit(value);
      const auto extra=key.size()+3U+(keys_.empty()?0U:1U);
      if(extra>limit_-2U-bytes_.size() || value.size()>limit_-2U-bytes_.size()-extra)
        return reject(LibraryCliErrorCode::LimitExceeded);
      auto candidate=bytes_;candidate.reserve(bytes_.size()+extra+value.size());
      if(!keys_.empty()) candidate+=',';
      quote(candidate,key);candidate+=':';candidate+=value;
      keys_.emplace_back(key);bytes_.swap(candidate);return true;
    } catch(const std::bad_alloc&) {return reject(LibraryCliErrorCode::StorageFailure);}
  }
  std::size_t limit_;
  std::string bytes_{"{"};
  std::vector<std::string> keys_;
  std::optional<LibraryCliError> error_;
};
// All-or-error response envelope. It holds only records produced by the model.
class LibraryCliOutputBatch {
public:
  static constexpr std::size_t kMaxBytes=2U*1048576U;
  explicit LibraryCliOutputBatch(std::size_t limit=kMaxBytes):limit_(limit) {
    if(limit==0 || limit>kMaxBytes) error_=LibraryCliError{LibraryCliErrorCode::LimitExceeded,0};
  }
  bool add(const LibraryCliJsonRecord& record) {
    if(error_) return false;
    auto output=record.finish();
    if(output.error) {error_=output.error;return false;}
    if(output.bytes->size()>limit_-bytes_.size()) {error_=LibraryCliError{LibraryCliErrorCode::LimitExceeded,0};return false;}
    try {bytes_+=*output.bytes;return true;}
    catch(const std::bad_alloc&) {error_=LibraryCliError{LibraryCliErrorCode::StorageFailure,0};return false;}
  }
  LibraryCliOutputResult finish() const {
    if(error_) return {{},error_};
    try {return {bytes_,{}};}catch(const std::bad_alloc&) {return {{},LibraryCliError{LibraryCliErrorCode::StorageFailure,0}};}
  }
private:
  std::size_t limit_;
  std::string bytes_;
  std::optional<LibraryCliError> error_;
};
// Pure classification only; no service invocation, stdout I/O or retry machinery.
enum class LibraryCliCommitOutcome { Unknown, Saved, NoChange, Rejected, Uncertain };
struct LibraryCliDelivery {
  LibraryCliCommitOutcome commit{LibraryCliCommitOutcome::Unknown};
  bool outputFailed{};
  int exitCode(int ordinaryExit=0) const noexcept {return outputFailed?74:ordinaryExit;}
  bool automaticRetry() const noexcept {return false;}
};
} // namespace OpenHDK
