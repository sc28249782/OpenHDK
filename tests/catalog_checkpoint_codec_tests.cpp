// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/CatalogCheckpointCodec.hpp"
#include "tests/TestCheck.hpp"
#include "tests/fixtures/checkpoint_vectors.hpp"
#include <cstdlib>
#include <new>
namespace { std::ptrdiff_t failAfter=-1; }
#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif
TEST_NOINLINE void* operator new(std::size_t n) {
  if(failAfter==0) throw std::bad_alloc();
  if(failAfter>0) --failAfter;
  if(auto p=std::malloc(n?n:1U)) return p;
  throw std::bad_alloc();
}
TEST_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
TEST_NOINLINE void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p,std::size_t) noexcept { ::operator delete(p); }
#undef TEST_NOINLINE
using namespace OpenHDK;
using Bytes=std::vector<std::uint8_t>;
Bytes unhex(std::string_view s) {
  const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
  Bytes b; for(std::size_t i=0;i<s.size();i+=2) b.push_back(static_cast<std::uint8_t>(digit(s[i])*16+digit(s[i+1])));
  return b;
}
void patch(Bytes& b,std::size_t at,std::uint64_t n,unsigned width=1U) {
  for(unsigned i=0;i<width;++i) b[at+i]=static_cast<std::uint8_t>(n>>(8U*i));
  const auto digest=sourceRevision(std::span<const std::uint8_t>(b).first(b.size()-32U)).sha256;
  std::copy(digest.begin(),digest.end(),b.end()-32);
}
bool error(const auto& r,CheckpointErrorCode c) {return r.value || !r.error || r.error->code!=c;}
int main() {
  const auto empty=unhex(CheckpointFixtures::empty), root=unhex(CheckpointFixtures::rootOnly), rich=unhex(CheckpointFixtures::rich);
  const auto e=decodeCatalogCheckpoint(empty), r=decodeCatalogCheckpoint(root), d=decodeCatalogCheckpoint(rich);
  OPENHDK_FAIL_IF(1,!e.succeeded() || !r.succeeded() || !d.succeeded());
  OPENHDK_FAIL_IF(2,encodeCatalogCheckpoint(*e.value).value!=empty || encodeCatalogCheckpoint(*r.value).value!=root
      || encodeCatalogCheckpoint(*d.value).value!=rich);
  OPENHDK_FAIL_IF(3,d.value->sequence!=0x0102030405060708ULL || d.value->catalogRevision!=77U
      || d.value->nextRoot!=100U || d.value->nextSong!=1000U || d.value->roots[0].id!=2U
      || d.value->roots[0].policy.lyrics.trackIndex!=7U || d.value->roots[0].hint!="E:/เพลง"
      || d.value->roots[1].attachmentGeneration!=UINT64_MAX || d.value->songs[0].title!=" Title/Artist\t "
      || d.value->songs[0].artist!="ศิลปิน" || d.value->songs[1].locator!="e\xcc\x81.mid"
      || d.value->songs[1].title!="É\r\n");
  auto reversed=*d.value;
  std::reverse(reversed.roots.begin(),reversed.roots.end()); std::reverse(reversed.songs.begin(),reversed.songs.end());
  OPENHDK_FAIL_IF(4,encodeCatalogCheckpoint(reversed).value!=rich); // canonicalization without mutation
  OPENHDK_FAIL_IF(5,reversed.roots[0].id!=8U || reversed.songs[0].id!=90U);
  for(std::size_t n=0U;n<rich.size();++n)
    OPENHDK_FAIL_IF(6,decodeCatalogCheckpoint(std::span<const std::uint8_t>(rich).first(n)).succeeded());
  auto b=rich; b.push_back(0U); OPENHDK_FAIL_IF(7,decodeCatalogCheckpoint(b).succeeded());
  b=rich; b[100]^=1U; OPENHDK_FAIL_IF(8,error(decodeCatalogCheckpoint(b),CheckpointErrorCode::InvalidCheckpoint));
  for(auto schema:{0U,2U,UINT32_MAX}) { b=rich; patch(b,8U,schema,4U);
    OPENHDK_FAIL_IF(9,error(decodeCatalogCheckpoint(b),CheckpointErrorCode::UnsupportedSchema)); }
  b=rich; patch(b,16U,UINT64_MAX,8U); OPENHDK_FAIL_IF(10,decodeCatalogCheckpoint(b).succeeded());
  for(auto offset:{0U,12U,24U}) { b=empty; patch(b,offset,offset==24U?0U:1U);
    OPENHDK_FAIL_IF(11,decodeCatalogCheckpoint(b).succeeded()); }
  for(auto offset:{64U,72U}) { b=root; patch(b,offset,0U,8U);
    OPENHDK_FAIL_IF(12,decodeCatalogCheckpoint(b).succeeded()); }
  for(auto offset:{80U,81U}) { b=root; patch(b,offset,2U);
    OPENHDK_FAIL_IF(13,error(decodeCatalogCheckpoint(b),CheckpointErrorCode::UnsupportedProfile)); }
  for(auto offset:{82U,83U}) { b=root; patch(b,offset,2U);
    OPENHDK_FAIL_IF(14,decodeCatalogCheckpoint(b).succeeded()); }
  b=root; patch(b,84U,1U,8U); OPENHDK_FAIL_IF(15,decodeCatalogCheckpoint(b).succeeded());
  b=root; patch(b,83U,1U); OPENHDK_FAIL_IF(16,decodeCatalogCheckpoint(b).succeeded());
  auto p=*d.value;
  const auto rejects=[&](auto mutate,CheckpointErrorCode expected) {
    auto copy=p; mutate(copy); return error(encodeCatalogCheckpoint(copy),expected);
  };
  OPENHDK_FAIL_IF(17,rejects([](auto& q){q.roots[1].id=q.roots[0].id;},CheckpointErrorCode::InvalidCheckpoint));
  OPENHDK_FAIL_IF(18,rejects([](auto& q){q.songs[1].id=q.songs[0].id;},CheckpointErrorCode::InvalidCheckpoint));
  OPENHDK_FAIL_IF(19,rejects([](auto& q){q.songs[0].root=1U;},CheckpointErrorCode::InvalidCheckpoint));
  OPENHDK_FAIL_IF(20,rejects([](auto& q){q.nextRoot=8U;},CheckpointErrorCode::InvalidCheckpoint)
      || rejects([](auto& q){q.nextSong=90U;},CheckpointErrorCode::InvalidCheckpoint));
  OPENHDK_FAIL_IF(21,rejects([](auto& q){q.songs[1].root=2U;q.songs[1].locator="LIVE.SET.KAR";},CheckpointErrorCode::InvalidCheckpoint));
  OPENHDK_FAIL_IF(22,rejects([](auto& q){q.songs[0].role=static_cast<SourceMemberRole>(99);},CheckpointErrorCode::UnsupportedProfile));
  for(const auto& invalid:std::vector<std::string>{"", "../x.mid", "/a", "a//b", "a\\b", "C:x", "\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80"})
    OPENHDK_FAIL_IF(23,rejects([&](auto& q){q.songs[0].locator=invalid;},CheckpointErrorCode::InvalidText));
  for(const auto& invalid:std::vector<std::string>{"",std::string("a\0b",3),"\xc2","\x01"})
    OPENHDK_FAIL_IF(24,rejects([&](auto& q){q.songs[0].title=invalid;},CheckpointErrorCode::InvalidText));
  for(const auto& invalid:std::vector<std::string>{"", "a\t", "a\r", "a\x7f", "\xed\xa0\x80"})
    OPENHDK_FAIL_IF(25,rejects([&](auto& q){q.roots[0].hint=invalid;},CheckpointErrorCode::InvalidText));
  // Fixed rich offsets derived independently from Python fixture text lengths.
  const std::size_t secondRoot=96U+std::string("E:/เพลง").size();
  const std::size_t firstSong=secondRoot+32U+std::string("E:/เพลง").size();
  b=rich; patch(b,secondRoot,2U,8U); OPENHDK_FAIL_IF(26,decodeCatalogCheckpoint(b).succeeded());
  b=rich; patch(b,firstSong+17U,1U); OPENHDK_FAIL_IF(27,decodeCatalogCheckpoint(b).succeeded());
  b=rich; patch(b,firstSong+16U,1U); OPENHDK_FAIL_IF(28,error(decodeCatalogCheckpoint(b),CheckpointErrorCode::UnsupportedProfile));
  b=rich; patch(b,firstSong+20U,UINT32_MAX,4U); OPENHDK_FAIL_IF(29,error(decodeCatalogCheckpoint(b),CheckpointErrorCode::LimitExceeded));
  b=rich; patch(b,firstSong+20U,0U,4U); OPENHDK_FAIL_IF(30,decodeCatalogCheckpoint(b).succeeded());
  b=rich; patch(b,firstSong+8U,77U,8U); OPENHDK_FAIL_IF(31,decodeCatalogCheckpoint(b).succeeded());
  // Max counters are retained, not recomputed from rows; SIZE_MAX policy is data.
  p.nextRoot=UINT64_MAX; p.nextSong=UINT64_MAX; p.catalogRevision=UINT64_MAX;
  p.roots[0].policy.lyrics.trackIndex=std::numeric_limits<std::size_t>::max();
  auto encoded=encodeCatalogCheckpoint(p); OPENHDK_FAIL_IF(32,!encoded.succeeded() || decodeCatalogCheckpoint(*encoded.value).value!=p);
  if constexpr(sizeof(std::size_t)<sizeof(std::uint64_t)) {
    b=rich; patch(b,84U,UINT64_MAX,8U);
    OPENHDK_FAIL_IF(33,error(decodeCatalogCheckpoint(b),CheckpointErrorCode::UnsupportedRepresentation));
  }
  CatalogCheckpointLimits limits;
  for(unsigned which=0U;which<5U;++which) {
    auto l=limits; auto* field=which==0U?&l.fileBytes:which==1U?&l.roots:which==2U?&l.songs:which==3U?&l.textBytes:&l.stagedBytes;
    *field=0U; OPENHDK_FAIL_IF(34,error(encodeCatalogCheckpoint(p,l),CheckpointErrorCode::InvalidConfiguration)
        || error(decodeCatalogCheckpoint(rich,l),CheckpointErrorCode::InvalidConfiguration));
    *field=which==1U?1025U:which==2U?100001U:which==3U?4097U:limits.kBytes+1U;
    OPENHDK_FAIL_IF(35,error(encodeCatalogCheckpoint(p,l),CheckpointErrorCode::InvalidConfiguration));
  }
  limits.fileBytes=rich.size(); OPENHDK_FAIL_IF(36,!decodeCatalogCheckpoint(rich,limits).succeeded());
  --limits.fileBytes; OPENHDK_FAIL_IF(37,error(decodeCatalogCheckpoint(rich,limits),CheckpointErrorCode::LimitExceeded));
  limits={}; limits.roots=1U; OPENHDK_FAIL_IF(38,error(decodeCatalogCheckpoint(rich,limits),CheckpointErrorCode::LimitExceeded));
  limits={}; limits.songs=1U; OPENHDK_FAIL_IF(39,error(encodeCatalogCheckpoint(p,limits),CheckpointErrorCode::LimitExceeded));
  // An exact 4096-byte field, then cap+1. Expanded input is independent of encoder.
  p=*r.value; p.roots[0].hint=std::string(4096U,'x');
  OPENHDK_FAIL_IF(40,!encodeCatalogCheckpoint(p).succeeded()); p.roots[0].hint->push_back('x');
  OPENHDK_FAIL_IF(41,error(encodeCatalogCheckpoint(p),CheckpointErrorCode::LimitExceeded));
  p=*r.value; p.roots[0].hint="abc";
  // Wire=131, retained strings=3, scratch=2*3 => exact coexistence=140.
  limits={};limits.stagedBytes=140U;
  encoded=encodeCatalogCheckpoint(p,limits); OPENHDK_FAIL_IF(42,!encoded.succeeded() || !decodeCatalogCheckpoint(*encoded.value,limits).succeeded());
  limits.stagedBytes=139U; OPENHDK_FAIL_IF(43,error(encodeCatalogCheckpoint(p,limits),CheckpointErrorCode::LimitExceeded)
      || error(decodeCatalogCheckpoint(*encoded.value,limits),CheckpointErrorCode::LimitExceeded));
  limits.stagedBytes=150U; OPENHDK_FAIL_IF(44,!encodeCatalogCheckpoint(p,limits,10U).succeeded()
      || error(encodeCatalogCheckpoint(p,limits,11U),CheckpointErrorCode::LimitExceeded)
      || error(decodeCatalogCheckpoint(*encoded.value,limits,11U),CheckpointErrorCode::LimitExceeded));
  // Sweep all allocation points until full success; no partial output or mutation.
  const auto original=*d.value; bool reached=false;
  for(std::ptrdiff_t n=0;n<300;++n) {
    failAfter=n; auto out=encodeCatalogCheckpoint(original); failAfter=-1;
    if(out.succeeded()) {reached=true;break;}
    OPENHDK_FAIL_IF(45,error(out,CheckpointErrorCode::StorageFailure) || original!=*d.value);
  }
  OPENHDK_FAIL_IF(46,!reached); reached=false;
  for(std::ptrdiff_t n=0;n<300;++n) {
    failAfter=n; auto out=decodeCatalogCheckpoint(rich); failAfter=-1;
    if(out.succeeded()) {reached=true;break;}
    OPENHDK_FAIL_IF(47,error(out,CheckpointErrorCode::StorageFailure) || rich!=unhex(CheckpointFixtures::rich));
  }
  OPENHDK_FAIL_IF(48,!reached);
  // Canonical numeric ordering is mandatory on decode, including distinct IDs.
  b=rich; patch(b,64U,8U,8U); patch(b,secondRoot,2U,8U);
  OPENHDK_FAIL_IF(49,error(decodeCatalogCheckpoint(b),CheckpointErrorCode::InvalidCheckpoint));
  // All bytes, including sequence and override text, belong to digest coverage.
  for(std::size_t at=0U;at<rich.size();++at) {
    b=rich; b[at]^=0x80U;
    OPENHDK_FAIL_IF(50,decodeCatalogCheckpoint(b).succeeded());
  }
  b=rich; patch(b,40U,8U,8U); OPENHDK_FAIL_IF(51,decodeCatalogCheckpoint(b).succeeded());
  b=rich; patch(b,firstSong+32U,0xc0U);
  OPENHDK_FAIL_IF(52,error(decodeCatalogCheckpoint(b),CheckpointErrorCode::InvalidText));
  b=rich; patch(b,firstSong+32U+std::string("Live.Set.kar").size(),1U);
  const auto badText=decodeCatalogCheckpoint(b);
  OPENHDK_FAIL_IF(53,error(badText,CheckpointErrorCode::InvalidText)
      || badText.error->field!=CheckpointField::Title || badText.error->record!=0U
      || badText.error->byteOffset!=0U || badText.error->operation!=CheckpointOperation::Decode);
  p=*r.value; p.roots[0].policy.lyrics.trackIndex=0U;
  encoded=encodeCatalogCheckpoint(p);
  OPENHDK_FAIL_IF(54,!encoded.succeeded() || decodeCatalogCheckpoint(*encoded.value).value!=p);
  limits={}; limits.roots=2U; limits.songs=2U;
  OPENHDK_FAIL_IF(55,!decodeCatalogCheckpoint(rich,limits).succeeded());
  p=*r.value; p.roots[0].hint="abc"; limits={}; limits.textBytes=3U;
  encoded=encodeCatalogCheckpoint(p,limits);
  OPENHDK_FAIL_IF(56,!encoded.succeeded() || !decodeCatalogCheckpoint(*encoded.value,limits).succeeded());
  --limits.textBytes; OPENHDK_FAIL_IF(57,error(encodeCatalogCheckpoint(p,limits),CheckpointErrorCode::LimitExceeded)
      || error(decodeCatalogCheckpoint(*encoded.value,limits),CheckpointErrorCode::LimitExceeded));
  // Empty checkpoint also consumes its wire bytes in the shared allowance.
  limits={}; limits.stagedBytes=96U;
  OPENHDK_FAIL_IF(58,!decodeCatalogCheckpoint(empty,limits).succeeded());
  --limits.stagedBytes; OPENHDK_FAIL_IF(59,error(decodeCatalogCheckpoint(empty,limits),CheckpointErrorCode::LimitExceeded));
  return 0;
}
