// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/SongCatalog.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace OpenHDK {
// Detached wire values only. These numbers cannot construct live RootId/SongId.
// No path access, restore, source authority or runtime lineage is represented.
struct CheckpointRoot {
  std::uint64_t id = 0U, attachmentGeneration = 1U;
  RootSourcePolicy policy{};
  std::optional<std::string> hint;
  bool operator==(const CheckpointRoot&) const = default;
};
struct CheckpointSong {
  std::uint64_t id = 0U, root = 0U;
  SourceMemberRole role = SourceMemberRole::PrimaryMidi;
  std::string locator;
  std::optional<std::string> title, artist;
  bool operator==(const CheckpointSong&) const = default;
};
struct CatalogCheckpointProjection {
  std::uint64_t sequence = 1U, catalogRevision = 0U, nextRoot = 1U, nextSong = 1U;
  std::vector<CheckpointRoot> roots;
  std::vector<CheckpointSong> songs;
  bool operator==(const CatalogCheckpointProjection&) const = default;
};
struct CatalogCheckpointLimits {
  static constexpr std::size_t kBytes = 64U * 1024U * 1024U;
  std::size_t fileBytes = kBytes, roots = 1024U, songs = 100000U;
  std::size_t textBytes = 4096U, stagedBytes = kBytes;
};
enum class CheckpointErrorCode {
  InvalidConfiguration, InvalidCheckpoint, UnsupportedSchema, UnsupportedProfile,
  UnsupportedRepresentation, InvalidText, LimitExceeded, StorageFailure
};
enum class CheckpointOperation { Encode, Decode };
enum class CheckpointField {
  None, Header, Digest, Sequence, Counter, Id, Generation, Policy, Hint,
  Locator, Title, Artist, Ordering
};
struct CheckpointError {
  CheckpointErrorCode code;
  CheckpointOperation operation;
  CheckpointField field = CheckpointField::None;
  std::optional<std::size_t> record, byteOffset;
};
template<class T> struct CheckpointResult {
  std::optional<T> value;
  std::optional<CheckpointError> error;
  bool succeeded() const noexcept { return value.has_value() && !error; }
};
namespace CheckpointDetail {
constexpr std::array<std::uint8_t, 8> magic{'O','H','K','C','A','T',0,0};
constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
inline bool validLimits(const CatalogCheckpointLimits& l) noexcept {
  return l.fileBytes > 0U && l.fileBytes <= l.kBytes && l.stagedBytes > 0U
      && l.stagedBytes <= l.kBytes && l.roots > 0U && l.roots <= 1024U
      && l.songs > 0U && l.songs <= 100000U && l.textBytes > 0U && l.textBytes <= 4096U;
}
struct Failure { CheckpointErrorCode code; CheckpointField field;
  std::optional<std::size_t> record, offset; };
[[noreturn]] inline void fail(CheckpointErrorCode c, CheckpointField f,
    std::optional<std::size_t> r = {}, std::optional<std::size_t> o = {}) { throw Failure{c,f,r,o}; }
inline void add(std::size_t& total, std::size_t n, std::size_t limit,
    CheckpointField f = CheckpointField::None) {
  if (total > limit || n > limit - total) fail(CheckpointErrorCode::LimitExceeded,f);
  total += n;
}
inline std::uint64_t get(std::span<const std::uint8_t> b, std::size_t at, unsigned width) {
  if (at > b.size() || width > b.size() - at)
    fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Header,{},at);
  std::uint64_t n = 0U;
  for (unsigned i=0U; i<width; ++i) n |= static_cast<std::uint64_t>(b[at+i]) << (8U*i);
  return n;
}
inline void put(std::vector<std::uint8_t>& b, std::uint64_t n, unsigned width) {
  for (unsigned i=0U; i<width; ++i) b.push_back(static_cast<std::uint8_t>(n >> (8U*i)));
}
inline void append(std::vector<std::uint8_t>& b, std::string_view s) {
  b.insert(b.end(),s.begin(),s.end());
}
struct FoldLess {
  bool operator()(const std::pair<std::uint64_t,std::string_view>& a,
      const std::pair<std::uint64_t,std::string_view>& b) const noexcept {
    if (a.first != b.first) return a.first < b.first;
    return std::lexicographical_compare(a.second.begin(),a.second.end(),b.second.begin(),b.second.end(),
        [](char x,char y) { return catalogAsciiFold(static_cast<unsigned char>(x))
                               < catalogAsciiFold(static_cast<unsigned char>(y)); });
  }
};
// UTF-8 validation uses the canonical decoder policy, with one temporary owned
// string. The ledger reserves TWO largest-field logical byte copies; allocator
// capacity/overhead is not a process-memory cap. No MetadataText extra copy.
inline void text(std::string_view s, CheckpointField field, std::size_t record,
    std::size_t limit, bool hint = false) {
  if (s.empty()) fail(CheckpointErrorCode::InvalidText,field,record,0U);
  if (s.size()>limit) fail(CheckpointErrorCode::LimitExceeded,field,record,limit);
  auto d=decodeLyricText({reinterpret_cast<const std::uint8_t*>(s.data()),s.size()},
                        LyricTextEncoding::Utf8,{limit,limit});
  if (!d.succeeded()) fail(CheckpointErrorCode::InvalidText,field,record,d.error()->byteOffset);
  if (hint) for (std::size_t i=0U;i<s.size();++i) {
    const auto c=static_cast<unsigned char>(s[i]);
    if (c<0x20U || c==0x7fU) fail(CheckpointErrorCode::InvalidText,field,record,i);
  }
}
struct Shape { std::size_t wire=96U, strings=0U, largest=0U; };
// Preflight every length before validation allocates. Descriptor containers have
// separate bounded record counts. Borrowed wire bytes are charged too, even when
// caller-owned; callers must include any OTHER retained payload in alreadyOwned.
inline Shape shape(const CatalogCheckpointProjection& p,const CatalogCheckpointLimits& l) {
  if (p.roots.size()>l.roots || p.songs.size()>l.songs)
    fail(CheckpointErrorCode::LimitExceeded,CheckpointField::Header);
  Shape s;
  const auto field=[&](std::string_view v,CheckpointField f) {
    if (v.size()>l.textBytes) fail(CheckpointErrorCode::LimitExceeded,f);
    add(s.strings,v.size(),l.stagedBytes,f); add(s.wire,v.size(),l.fileBytes,f);
    s.largest=std::max(s.largest,v.size());
  };
  for (const auto& r:p.roots) { add(s.wire,32U,l.fileBytes); if(r.hint) field(*r.hint,CheckpointField::Hint); }
  for (const auto& r:p.songs) { add(s.wire,32U,l.fileBytes); field(r.locator,CheckpointField::Locator);
    if(r.title) field(*r.title,CheckpointField::Title);
    if(r.artist) field(*r.artist,CheckpointField::Artist);
  }
  if(s.wire>l.fileBytes) fail(CheckpointErrorCode::LimitExceeded,CheckpointField::Header);
  return s;
}
inline void ledger(const Shape& s,const CatalogCheckpointLimits& l,std::size_t alreadyOwned) {
  std::size_t total=0U;
  add(total,alreadyOwned,l.stagedBytes); add(total,s.wire,l.stagedBytes);
  add(total,s.strings,l.stagedBytes); add(total,s.largest,l.stagedBytes);
  add(total,s.largest,l.stagedBytes);
}
inline void validate(const CatalogCheckpointProjection& p,const CatalogCheckpointLimits& l) {
  if(p.sequence==0U) fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Sequence);
  if(p.nextRoot==0U || p.nextSong==0U) fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Counter);
  std::map<std::uint64_t,bool> roots, songs;
  std::map<std::pair<std::uint64_t,std::string_view>,bool,FoldLess> keys;
  for(std::size_t i=0U;i<p.roots.size();++i) {
    const auto& r=p.roots[i];
    if(r.id==0U || r.id==maximum || r.id>=p.nextRoot || !roots.emplace(r.id,true).second)
      fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Id,i);
    if(r.attachmentGeneration==0U) fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Generation,i);
    if(!isValidRootSourcePolicy(r.policy)) fail(CheckpointErrorCode::UnsupportedProfile,CheckpointField::Policy,i);
    if(r.hint) text(*r.hint,CheckpointField::Hint,i,l.textBytes,true);
  }
  for(std::size_t i=0U;i<p.songs.size();++i) {
    const auto& r=p.songs[i];
    if(r.id==0U || r.id==maximum || r.id>=p.nextSong || !songs.emplace(r.id,true).second || !roots.contains(r.root))
      fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Id,i);
    if(r.role!=SourceMemberRole::PrimaryMidi) fail(CheckpointErrorCode::UnsupportedProfile,CheckpointField::Policy,i);
    if(!isCatalogLocator(r.locator)) fail(CheckpointErrorCode::InvalidText,CheckpointField::Locator,i);
    if(!keys.emplace(std::make_pair(r.root,std::string_view(r.locator)),true).second)
      fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Locator,i);
    if(r.title) text(*r.title,CheckpointField::Title,i,l.textBytes);
    if(r.artist) text(*r.artist,CheckpointField::Artist,i,l.textBytes);
  }
}
} // namespace CheckpointDetail

// Allocating serialized control-path operations; never invoke in a callback.
// Result owns complete output or only an error. std::bad_alloc is mapped; input
// values and caller-owned buffers are never modified. No durability is implied.
inline CheckpointResult<std::vector<std::uint8_t>> encodeCatalogCheckpoint(
    const CatalogCheckpointProjection& p, CatalogCheckpointLimits l={}, std::size_t alreadyOwned=0U) {
  using namespace CheckpointDetail;
  try {
    if(!validLimits(l)) fail(CheckpointErrorCode::InvalidConfiguration,CheckpointField::None);
    const auto s=shape(p,l); ledger(s,l,alreadyOwned); validate(p,l);
    std::vector<const CheckpointRoot*> roots; std::vector<const CheckpointSong*> songs;
    roots.reserve(p.roots.size()); songs.reserve(p.songs.size());
    for(const auto& r:p.roots) roots.push_back(&r);
    for(const auto& r:p.songs) songs.push_back(&r);
    const auto less=[](auto a,auto b){return a->id<b->id;};
    std::sort(roots.begin(),roots.end(),less); std::sort(songs.begin(),songs.end(),less);
    std::vector<std::uint8_t> b; b.reserve(s.wire); b.insert(b.end(),magic.begin(),magic.end());
    put(b,1U,4U); put(b,0U,4U); put(b,s.wire-96U,8U); put(b,p.sequence,8U);
    put(b,p.catalogRevision,8U); put(b,p.nextRoot,8U); put(b,p.nextSong,8U);
    put(b,roots.size(),4U); put(b,songs.size(),4U);
    for(const auto* r:roots) {
      put(b,r->id,8U); put(b,r->attachmentGeneration,8U); put(b,0U,1U);
      put(b,r->policy.lyrics.encoding==LyricTextEncoding::Utf8?0U:1U,1U);
      put(b,r->policy.lyrics.trackIndex.has_value(),1U); put(b,r->hint.has_value(),1U);
      put(b,r->policy.lyrics.trackIndex.value_or(0U),8U); put(b,r->hint?r->hint->size():0U,4U);
      if(r->hint) append(b,*r->hint);
    }
    for(const auto* r:songs) {
      put(b,r->id,8U); put(b,r->root,8U); put(b,0U,4U); put(b,r->locator.size(),4U);
      put(b,r->title?r->title->size():0U,4U); put(b,r->artist?r->artist->size():0U,4U);
      append(b,r->locator); if(r->title) append(b,*r->title); if(r->artist) append(b,*r->artist);
    }
    const auto digest=sourceRevision(b).sha256; b.insert(b.end(),digest.begin(),digest.end());
    return {std::move(b),{}};
  } catch(const Failure& e) { return {{},CheckpointError{e.code,CheckpointOperation::Encode,e.field,e.record,e.offset}};
  } catch(const std::bad_alloc&) { return {{},CheckpointError{CheckpointErrorCode::StorageFailure,CheckpointOperation::Encode,{},{},{}}}; }
}

inline CheckpointResult<CatalogCheckpointProjection> decodeCatalogCheckpoint(
    std::span<const std::uint8_t> b,CatalogCheckpointLimits l={},std::size_t alreadyOwned=0U) {
  using namespace CheckpointDetail;
  try {
    if(!validLimits(l)) fail(CheckpointErrorCode::InvalidConfiguration,CheckpointField::None);
    if(b.size()>l.fileBytes) fail(CheckpointErrorCode::LimitExceeded,CheckpointField::Header);
    if(b.size()<96U || !std::equal(magic.begin(),magic.end(),b.begin()))
      fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Header,{},0U);
    if(get(b,8U,4U)!=1U) fail(CheckpointErrorCode::UnsupportedSchema,CheckpointField::Header,{},8U);
    const auto payload=get(b,16U,8U);
    if(get(b,12U,4U)!=0U || payload!=b.size()-96U)
      fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Header,{},12U);
    const auto digest=sourceRevision(b.first(b.size()-32U)).sha256;
    if(!std::equal(digest.begin(),digest.end(),b.end()-32))
      fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Digest,{},b.size()-32U);
    const auto nr=get(b,56U,4U), ns=get(b,60U,4U);
    if(nr>l.roots || ns>l.songs) fail(CheckpointErrorCode::LimitExceeded,CheckpointField::Header,{},56U);
    if(nr+ns>payload/32U) fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Header,{},56U);
    CatalogCheckpointProjection p;
    p.sequence=get(b,24U,8U); p.catalogRevision=get(b,32U,8U);
    p.nextRoot=get(b,40U,8U); p.nextSong=get(b,48U,8U);
    // First pass collects borrowed descriptors, checks padding/length/order, and
    // computes coexistence before any decoded text allocation. Descriptors are
    // count-bounded. Only the second pass builds owned strings.
    struct RootView { CheckpointRoot root; std::string_view hint; };
    struct SongView { std::uint64_t id,root; std::string_view locator,title,artist; };
    std::vector<RootView> roots; std::vector<SongView> songs;
    roots.reserve(static_cast<std::size_t>(nr)); songs.reserve(static_cast<std::size_t>(ns));
    Shape s; s.wire=b.size(); std::size_t at=64U; const auto end=b.size()-32U;
    const auto take=[&](std::size_t n,CheckpointField f,std::size_t i) {
      if(n>l.textBytes) fail(CheckpointErrorCode::LimitExceeded,f,i,at);
      if(at>end || n>end-at) fail(CheckpointErrorCode::InvalidCheckpoint,f,i,at);
      const std::string_view v(reinterpret_cast<const char*>(b.data()+at),n); at+=n;
      add(s.strings,n,l.stagedBytes,f); s.largest=std::max(s.largest,n); return v;
    };
    std::uint64_t previous=0U;
    for(std::size_t i=0U;i<nr;++i) {
      if(end-at<32U) fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Header,i,at);
      RootView v; v.root.id=get(b,at,8U); v.root.attachmentGeneration=get(b,at+8U,8U);
      if(v.root.id<=previous) fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Ordering,i,at);
      previous=v.root.id;
      if(b[at+16U]!=0U || b[at+17U]>1U) fail(CheckpointErrorCode::UnsupportedProfile,CheckpointField::Policy,i,at+16U);
      const auto index=get(b,at+20U,8U), len=get(b,at+28U,4U);
      if(b[at+18U]>1U || b[at+19U]>1U || (!b[at+18U] && index!=0U)
          || (b[at+19U] ? len==0U : len!=0U))
        fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Policy,i,at+18U);
      if(b[at+18U]) {
        if(index>std::numeric_limits<std::size_t>::max())
          fail(CheckpointErrorCode::UnsupportedRepresentation,CheckpointField::Policy,i,at+20U);
        v.root.policy.lyrics.trackIndex=static_cast<std::size_t>(index);
      }
      v.root.policy.lyrics.encoding=b[at+17U]?LyricTextEncoding::Tis620:LyricTextEncoding::Utf8;
      at+=32U; v.hint=take(static_cast<std::size_t>(len),CheckpointField::Hint,i); roots.push_back(std::move(v));
    }
    previous=0U;
    for(std::size_t i=0U;i<ns;++i) {
      if(end-at<32U) fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Header,i,at);
      const auto id=get(b,at,8U), root=get(b,at+8U,8U);
      if(id<=previous) fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Ordering,i,at);
      previous=id;
      if(b[at+16U]!=0U) fail(CheckpointErrorCode::UnsupportedProfile,CheckpointField::Policy,i,at+16U);
      if(get(b,at+17U,3U)!=0U) fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Policy,i,at+17U);
      const auto a=get(b,at+20U,4U), t=get(b,at+24U,4U), z=get(b,at+28U,4U);
      at+=32U;
      const auto locator=take(static_cast<std::size_t>(a),CheckpointField::Locator,i);
      const auto title=take(static_cast<std::size_t>(t),CheckpointField::Title,i);
      const auto artist=take(static_cast<std::size_t>(z),CheckpointField::Artist,i);
      songs.push_back({id,root,locator,title,artist});
    }
    if(at!=end) fail(CheckpointErrorCode::InvalidCheckpoint,CheckpointField::Header,{},at);
    ledger(s,l,alreadyOwned);
    p.roots.reserve(roots.size()); p.songs.reserve(songs.size());
    for(auto& v:roots) { if(!v.hint.empty()) v.root.hint=std::string(v.hint); p.roots.push_back(std::move(v.root)); }
    for(const auto& v:songs) {
      CheckpointSong r; r.id=v.id; r.root=v.root; r.locator=v.locator;
      if(!v.title.empty()) r.title=std::string(v.title);
      if(!v.artist.empty()) r.artist=std::string(v.artist);
      p.songs.push_back(std::move(r));
    }
    validate(p,l); return {std::move(p),{}};
  } catch(const Failure& e) { return {{},CheckpointError{e.code,CheckpointOperation::Decode,e.field,e.record,e.offset}};
  } catch(const std::bad_alloc&) { return {{},CheckpointError{CheckpointErrorCode::StorageFailure,CheckpointOperation::Decode,{},{},{}}}; }
}
} // namespace OpenHDK
