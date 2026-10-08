// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/CatalogCheckpointRestore.hpp"
#include "lyrics/LyricMediaConsumer.hpp"
#include "tests/TestCheck.hpp"
#include "tests/fixtures/checkpoint_vectors.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <new>
#include <stdexcept>
#include <type_traits>
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
TEST_NOINLINE void operator delete(void* p) noexcept {std::free(p);}
TEST_NOINLINE void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
#undef TEST_NOINLINE
using namespace OpenHDK;
namespace fs=std::filesystem;
using Bytes=std::vector<std::uint8_t>;
Bytes unhex(std::string_view s) {
  const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
  Bytes b; for(std::size_t i=0;i<s.size();i+=2) b.push_back(static_cast<std::uint8_t>(digit(s[i])*16+digit(s[i+1])));
  return b;
}
struct TemporaryRoot {
  fs::path path=fs::temp_directory_path()/("openhdk-restore-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryRoot(){if(!fs::create_directory(path)) throw std::runtime_error("fixture directory failed");}
  ~TemporaryRoot(){std::error_code ec;fs::remove_all(path,ec);}
};
void write(const fs::path& p) {
  const Bytes bytes{'M','T','h','d',0,0,0,6,0,0,0,1,0,3,'M','T','r','k',0,0,0,9,0,0xff,5,1,'A',0,0xff,0x2f,0};
  std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
  if(!f) throw std::runtime_error("fixture write failed");
}
bool error(const CatalogRestoreResult& r,CheckpointErrorCode code) {return r.owner || !r.error || r.error->code!=code;}
CatalogCheckpointProjection projection(std::string hint) {
  CatalogCheckpointProjection p;p.catalogRevision=77U;p.nextRoot=100U;p.nextSong=1000U;
  p.roots.push_back({2U,9U,{LibrarySourceMode::SmfKar,{LyricTextEncoding::Utf8,0U}},std::move(hint)});
  p.songs.push_back({4U,2U,SourceMemberRole::PrimaryMidi,"song.kar"," Title e\xcc\x81 ","ศิลปิน"});
  return p;
}
int main() {
  static_assert(!std::is_constructible_v<RootId,std::uint64_t>);
  static_assert(!std::is_constructible_v<SongId,std::uint64_t>);
  static_assert(!std::is_copy_constructible_v<SongDiscovery>);
  const auto rich=unhex(CheckpointFixtures::rich);
  auto restored=CatalogCheckpointRestore::fromBytes(rich);
  OPENHDK_FAIL_IF(1,!restored.succeeded());
  const auto richSnapshot=restored.owner->snapshot();
  OPENHDK_FAIL_IF(2,richSnapshot->revision!=77U || richSnapshot->roots.size()!=2U || richSnapshot->songs.size()!=2U
      || richSnapshot->roots[0].attachmentGeneration!=9U || richSnapshot->roots[1].attachmentGeneration!=UINT64_MAX
      || richSnapshot->roots[0].policy.lyrics.encoding!=LyricTextEncoding::Tis620
      || richSnapshot->roots[0].policy.lyrics.trackIndex!=7U);
  for(const auto& s:richSnapshot->songs)
    OPENHDK_FAIL_IF(3,s.state!=CatalogState::Invalid || s.sourceRevision() || s.metadata);
  OPENHDK_FAIL_IF(4,richSnapshot->songs[0].overrides->title->bytes()!=" Title/Artist\t "
      || richSnapshot->songs[0].overrides->artist->bytes()!="ศิลปิน");
  const auto info=restored.owner->rootAttachment(richSnapshot->roots[0].id);
  OPENHDK_FAIL_IF(5,!info || info->attached || info->savedDirectoryHint!="E:/เพลง");
  const auto richDisplay=restored.owner->display(richSnapshot,richSnapshot->songs[0].id);
  OPENHDK_FAIL_IF(6,!richDisplay.succeeded() || richDisplay.display->title.text!=" Title/Artist\t ");
  // A real existing saved hint still grants no scan/preparation access.
  TemporaryRoot temp;write(temp.path/"song.kar");
  auto p=projection(LibraryFilesystem::utf8(temp.path)); const auto original=p;
  auto a=CatalogCheckpointRestore::fromProjection(p);
  auto b=CatalogCheckpointRestore::fromProjection(p);
  OPENHDK_FAIL_IF(7,!a.succeeded() || !b.succeeded() || p!=original);
  const auto before=a.owner->snapshot();const auto root=before->roots[0].id;const auto id=before->songs[0].id;
  unsigned calls=0U;
  DiscoveryControl scanControl;scanControl.cancelled=[&]{++calls;return false;};scanControl.checkpoint=[&](auto){++calls;};
  const auto blocked=a.owner->scan(root,{},scanControl);
  OPENHDK_FAIL_IF(8,blocked.succeeded() || blocked.error->code!=DiscoveryError::InvalidRoot || calls!=0U || a.owner->snapshot()!=before);
  PreparationControl prepControl;prepControl.cancelled=[&]{++calls;return false;};prepControl.checkpoint=[&](auto){++calls;};
  auto prepared=a.owner->prepare(before,id,{},prepControl);
  OPENHDK_FAIL_IF(9,prepared.succeeded() || prepared.error->code!=PreparationErrorCode::InvalidRoot || calls!=0U);
  // IDs may coincide numerically but lineage does not, even for identical data.
  const auto foreign=b.owner->snapshot();
  OPENHDK_FAIL_IF(10,foreign->songs[0].id!=id || foreign->roots[0].id!=root
      || a.owner->display(foreign,id).succeeded());
  const auto foreignPrep=a.owner->prepare(foreign,id,{},prepControl);
  OPENHDK_FAIL_IF(11,foreignPrep.succeeded() || foreignPrep.error->code!=PreparationErrorCode::InvalidConfiguration || calls!=0U);
  // Explicit first attach, including the saved hint: +1 generation and revision.
  auto attached=a.owner->reattachRoot(root,temp.path);
  const auto after=a.owner->snapshot();
  OPENHDK_FAIL_IF(12,!attached.succeeded() || attached.status!=RootReattachmentStatus::Updated || after==before
      || after->revision!=78U || after->roots[0].attachmentGeneration!=10U
      || !a.owner->rootAttachment(root)->attached || after->songs[0].id!=id
      || after->songs[0].state!=CatalogState::Invalid || after->songs[0].sourceRevision());
  OPENHDK_FAIL_IF(13,a.owner->reattachRoot(root,temp.path).status!=RootReattachmentStatus::Unchanged || a.owner->snapshot()!=after);
  const auto stale=a.owner->prepare(before,id);
  OPENHDK_FAIL_IF(14,stale.succeeded() || stale.error->code!=PreparationErrorCode::SourceChanged);
  OPENHDK_FAIL_IF(15,!a.owner->scan(root).succeeded());
  const auto ready=a.owner->snapshot();
  OPENHDK_FAIL_IF(16,ready->songs[0].id!=id || ready->songs[0].state!=CatalogState::Ready || !ready->songs[0].sourceRevision()
      || ready->songs[0].overrides!=before->songs[0].overrides || ready->roots[0].attachmentGeneration!=10U);
  prepared=a.owner->prepare(ready,id);
  OPENHDK_FAIL_IF(17,!prepared.succeeded() || prepared.prepared->displayMetadata().title.text!=" Title e\xcc\x81 "
      || prepared.prepared->effectivePolicy().trackIndex!=0U);
  LyricMediaConsumer consumer;OPENHDK_FAIL_IF(18,!consumer.prepare(prepared.prepared->lyrics()));
  auto batch=consumer.advance(0U);
  OPENHDK_FAIL_IF(19,!batch.succeeded() || batch.batch().cues().size()!=1U);
  // Restoring again never touches the already-running logical owner or buffers.
  const auto oldPrepared=prepared.prepared;
  auto fresh=CatalogCheckpointRestore::fromProjection(p);
  OPENHDK_FAIL_IF(20,!fresh.succeeded() || a.owner->snapshot()!=ready
      || oldPrepared->lyrics()->cues()[0].decoded!="A" || fresh.owner->rootAttachment(root)->attached);
  const auto rejectOld=fresh.owner->prepare(ready,id,{},prepControl);
  OPENHDK_FAIL_IF(21,rejectOld.succeeded() || rejectOld.error->code!=PreparationErrorCode::InvalidConfiguration || calls!=0U);
  // Allocators keep gaps: next new root/song use 100/1000, not max retained +1.
  auto expected=p;expected.roots[0].id=100U;expected.songs[0].id=1000U;expected.songs[0].root=100U;
  expected.nextRoot=101U;expected.nextSong=1001U;
  auto probe=CatalogCheckpointRestore::fromProjection(expected);
  TemporaryRoot another;const auto newRoot=a.owner->registerRoot(another.path);
  OPENHDK_FAIL_IF(22,!probe.succeeded() || !newRoot.root || *newRoot.root!=probe.owner->snapshot()->roots[0].id);
  write(temp.path/"z.mid"); OPENHDK_FAIL_IF(23,!a.owner->scan(root).succeeded());
  const auto grown=a.owner->snapshot();
  const auto newSong=std::find_if(grown->songs.begin(),grown->songs.end(),[](const auto& s){return s.locator()=="z.mid";});
  OPENHDK_FAIL_IF(24,newSong==grown->songs.end() || newSong->id!=probe.owner->snapshot()->songs[0].id || newSong->id==id);
  // Missing/rescan and a second root attachment preserve override identity.
  fs::remove(temp.path/"song.kar");OPENHDK_FAIL_IF(25,!a.owner->scan(root).succeeded());
  OPENHDK_FAIL_IF(26,a.owner->snapshot()->songs[0].state!=CatalogState::Missing
      || a.owner->snapshot()->songs[0].overrides!=before->songs[0].overrides);
  TemporaryRoot moved;write(moved.path/"song.kar");
  OPENHDK_FAIL_IF(27,a.owner->reattachRoot(root,moved.path).status!=RootReattachmentStatus::Updated || !a.owner->scan(root).succeeded());
  const auto movedSnapshot=a.owner->snapshot();
  OPENHDK_FAIL_IF(28,movedSnapshot->songs[0].id!=id || movedSnapshot->songs[0].overrides!=before->songs[0].overrides);
  // Existing roots of another restored entry are independent, duplicate hints
  // are legal data; actual simultaneous overlapping attachments are rejected.
  auto duplicates=p;duplicates.roots.push_back({8U,1U,{},p.roots[0].hint});
  auto multiple=CatalogCheckpointRestore::fromProjection(duplicates);
  const auto multi=multiple.owner->snapshot();
  OPENHDK_FAIL_IF(29,!multiple.owner->reattachRoot(multi->roots[0].id,temp.path).succeeded());
  const auto secondAttach=multiple.owner->reattachRoot(multi->roots[1].id,temp.path);
  OPENHDK_FAIL_IF(30,secondAttach.succeeded() || secondAttach.error->code!=DiscoveryError::AmbiguousPath
      || multiple.owner->rootAttachment(multi->roots[1].id)->attached);
  // Hint existence is never required and hint spelling is not canonicalized.
  auto hint=p;hint.roots[0].hint="Z:/missing/../advisory";
  auto hints=CatalogCheckpointRestore::fromProjection(hint);
  OPENHDK_FAIL_IF(31,!hints.succeeded() || hints.owner->rootAttachment(root)->savedDirectoryHint!=hint.roots[0].hint);
  // Cancellation/bad target on first attachment leaves the owner wholly unbound.
  RootReattachmentControl cancel;cancel.cancelled=[] {return true;};
  auto unchanged=fresh.owner->snapshot();
  OPENHDK_FAIL_IF(32,fresh.owner->reattachRoot(root,temp.path,cancel).succeeded() || fresh.owner->snapshot()!=unchanged
      || fresh.owner->rootAttachment(root)->attached);
  OPENHDK_FAIL_IF(33,fresh.owner->reattachRoot(root,temp.path/"absent").succeeded() || fresh.owner->snapshot()!=unchanged);
  // Exhausted history is readable, while changed operations fail without wrap.
  auto exhausted=p;exhausted.catalogRevision=UINT64_MAX;exhausted.nextRoot=UINT64_MAX;exhausted.nextSong=UINT64_MAX;
  exhausted.roots[0].attachmentGeneration=UINT64_MAX;
  auto history=CatalogCheckpointRestore::fromProjection(exhausted);const auto historical=history.owner->snapshot();
  OPENHDK_FAIL_IF(34,!history.succeeded() || historical->revision!=UINT64_MAX || !history.owner->display(historical,id).succeeded());
  OPENHDK_FAIL_IF(35,history.owner->registerRoot(temp.path).error->code!=DiscoveryError::RevisionExhausted
      || history.owner->reattachRoot(root,temp.path).error->code!=DiscoveryError::RevisionExhausted
      || history.owner->snapshot()!=historical);
  OPENHDK_FAIL_IF(36,history.owner->replaceUserOverrides(id,{*p.songs[0].title,*p.songs[0].artist}).status!=CatalogOverrideStatus::Unchanged);
  OPENHDK_FAIL_IF(37,history.owner->replaceUserOverrides(id,{"changed",{}}).error->code!=CatalogMetadataErrorCode::RevisionExhausted
      || history.owner->snapshot()!=historical);
  exhausted=p;exhausted.nextRoot=UINT64_MAX;exhausted.nextSong=UINT64_MAX;
  auto noIds=CatalogCheckpointRestore::fromProjection(exhausted);
  OPENHDK_FAIL_IF(38,!noIds.owner->reattachRoot(root,moved.path).succeeded() || !noIds.owner->scan(root).succeeded());
  const auto high=noIds.owner->snapshot();write(moved.path/"new.mid");
  const auto failedScan=noIds.owner->scan(root);
  OPENHDK_FAIL_IF(39,failedScan.succeeded() || failedScan.error->code!=DiscoveryError::RevisionExhausted || noIds.owner->snapshot()!=high);
  // Validate caller-built values again, without partially returned owners.
  auto bad=p;bad.songs[0].root=99U;
  OPENHDK_FAIL_IF(40,error(CatalogCheckpointRestore::fromProjection(bad),CheckpointErrorCode::InvalidCheckpoint));
  bad=p;bad.songs[0].title="";
  OPENHDK_FAIL_IF(41,error(CatalogCheckpointRestore::fromProjection(bad),CheckpointErrorCode::InvalidText));
  auto corrupt=rich;corrupt.back()^=1U;
  const auto rejected=CatalogCheckpointRestore::fromBytes(corrupt);
  OPENHDK_FAIL_IF(42,error(rejected,CheckpointErrorCode::InvalidCheckpoint) || rejected.error->operation!=CheckpointOperation::Decode);
  // Exact/beyond logical coexistence: 3 hint bytes twice + scratch 3*3 = 15.
  CatalogCheckpointProjection small;small.nextRoot=2U;small.roots.push_back({1U,1U,{},"abc"});
  CatalogCheckpointLimits limits;limits.stagedBytes=15U;
  OPENHDK_FAIL_IF(43,!CatalogCheckpointRestore::fromProjection(small,limits).succeeded());
  --limits.stagedBytes;OPENHDK_FAIL_IF(44,error(CatalogCheckpointRestore::fromProjection(small,limits),CheckpointErrorCode::LimitExceeded));
  limits.stagedBytes=25U;
  OPENHDK_FAIL_IF(45,!CatalogCheckpointRestore::fromProjection(small,limits,10U).succeeded()
      || error(CatalogCheckpointRestore::fromProjection(small,limits,11U),CheckpointErrorCode::LimitExceeded));
  const auto encoded=encodeCatalogCheckpoint(small);
  limits.stagedBytes=146U; // 131 wire + 15 restore; codec peak is only 140.
  OPENHDK_FAIL_IF(46,!CatalogCheckpointRestore::fromBytes(*encoded.value,limits).succeeded());
  --limits.stagedBytes;const auto over=CatalogCheckpointRestore::fromBytes(*encoded.value,limits);
  OPENHDK_FAIL_IF(47,error(over,CheckpointErrorCode::LimitExceeded) || over.error->operation!=CheckpointOperation::Restore);
  // Allocation sweep covers validation, fresh lineage, metadata copies and roots.
  bool complete=false;
  for(std::ptrdiff_t n=0;n<300;++n) {
    failAfter=n;auto result=CatalogCheckpointRestore::fromProjection(p);failAfter=-1;
    if(result.succeeded()){complete=true;break;}
    OPENHDK_FAIL_IF(48,error(result,CheckpointErrorCode::StorageFailure) || p!=original);
  }
  OPENHDK_FAIL_IF(49,!complete);complete=false;
  for(std::ptrdiff_t n=0;n<300;++n) {
    failAfter=n;auto result=CatalogCheckpointRestore::fromBytes(rich);failAfter=-1;
    if(result.succeeded()){complete=true;break;}
    OPENHDK_FAIL_IF(50,error(result,CheckpointErrorCode::StorageFailure));
  }
  OPENHDK_FAIL_IF(51,!complete);
  // Query/display/snapshot/prepared/batches remain owning after old owner dies.
  const auto savedInfo=a.owner->rootAttachment(root);
  const auto ownedDisplay=a.owner->display(ready,id);
  a.owner.reset();consumer.stop();
  OPENHDK_FAIL_IF(52,batch.batch().cues()[0].decoded!="A" || oldPrepared->lyrics()->cues()[0].decoded!="A"
      || before->songs[0].overrides->title->bytes()!=" Title e\xcc\x81 " || !savedInfo->savedDirectoryHint
      || !ownedDisplay.succeeded() || ownedDisplay.display->title.text!=" Title e\xcc\x81 ");
  // Empty/no-hint restore; absent overrides remain absent, fallback is owned.
  auto empty=CatalogCheckpointRestore::fromBytes(unhex(CheckpointFixtures::empty));
  OPENHDK_FAIL_IF(53,!empty.succeeded() || empty.owner->snapshot()->revision!=0U
      || !empty.owner->snapshot()->songs.empty() || !empty.owner->snapshot()->roots.empty());
  auto noHint=p;noHint.roots[0].hint.reset();noHint.songs[0].title.reset();noHint.songs[0].artist.reset();
  auto plain=CatalogCheckpointRestore::fromProjection(noHint);
  const auto plainSnap=plain.owner->snapshot();
  const auto fallback=plain.owner->display(plainSnap,id);
  OPENHDK_FAIL_IF(54,!fallback.succeeded() || fallback.display->title.text!="song"
      || fallback.display->title.origin!=CatalogDisplayOrigin::FilenameFallback
      || plainSnap->songs[0].overrides || plain.owner->rootAttachment(root)->savedDirectoryHint);
  // All entry points reject invalid configuration without returned owners.
  limits={};limits.stagedBytes=0U;
  OPENHDK_FAIL_IF(55,error(CatalogCheckpointRestore::fromProjection(p,limits),CheckpointErrorCode::InvalidConfiguration)
      || error(CatalogCheckpointRestore::fromBytes(rich,limits),CheckpointErrorCode::InvalidConfiguration));
  // Saved hints are charged on later operations too, not given a second budget.
  limits={};auto charged=CatalogCheckpointRestore::fromProjection(small);
  const auto smallRoot=charged.owner->snapshot()->roots[0].id;
  CatalogMetadataLimits displayLimits;displayLimits.stagedBytes=2U;
  auto hintBudget=p;hintBudget.roots[0].hint=std::string(100U,'x');
  auto budgetOwner=CatalogCheckpointRestore::fromProjection(hintBudget);
  displayLimits.stagedBytes=99U;
  OPENHDK_FAIL_IF(56,budgetOwner.owner->display(budgetOwner.owner->snapshot(),id,displayLimits).succeeded());
  OPENHDK_FAIL_IF(57,!charged.owner->reattachRoot(smallRoot,another.path).succeeded());
  DiscoveryLimits tinyScan;tinyScan.catalog.stagedLocatorBytes=2U;
  const auto limited=charged.owner->scan(smallRoot,tinyScan);
  OPENHDK_FAIL_IF(58,limited.succeeded() || limited.error->code!=DiscoveryError::LimitExceeded);
  // Cancellation after staging still leaves first attachment unpublished.
  bool stop=false;RootReattachmentControl stagedCancel;
  stagedCancel.cancelled=[&]{return stop;};stagedCancel.checkpoint=[&](auto phase){if(phase==RootReattachmentCheckpoint::BeforeCommit)stop=true;};
  const auto unbound=plain.owner->snapshot();
  const auto cancelled=plain.owner->reattachRoot(root,temp.path,stagedCancel);
  OPENHDK_FAIL_IF(59,cancelled.succeeded() || cancelled.error->code!=DiscoveryError::Cancelled
      || plain.owner->snapshot()!=unbound || plain.owner->rootAttachment(root)->attached);
  // Sweep first-attachment allocation failures; the saved hint is not activated
  // early and neither generation nor catalog revision is partially published.
  complete=false;
  for(std::ptrdiff_t n=0;n<300;++n) {
    failAfter=n;auto result=plain.owner->reattachRoot(root,temp.path);failAfter=-1;
    if(result.succeeded()){complete=true;break;}
    OPENHDK_FAIL_IF(60,!result.error || result.error->code!=DiscoveryError::StorageFailure
        || plain.owner->snapshot()!=unbound || plain.owner->rootAttachment(root)->attached);
  }
  OPENHDK_FAIL_IF(61,!complete || !plain.owner->rootAttachment(root)->attached);
  // Orphaned snapshots keep their origin alive and cannot match a later owner.
  const auto orphan=fresh.owner->prepare(ready,id,{},prepControl);
  OPENHDK_FAIL_IF(62,orphan.succeeded() || orphan.error->code!=PreparationErrorCode::InvalidConfiguration || calls!=0U);
  auto check=fresh.owner->snapshot();
  const auto budgetSnapshot=budgetOwner.owner->snapshot();
  CatalogMetadataLimits tinyOverride;tinyOverride.stagedBytes=99U;
  const auto failedOverride=budgetOwner.owner->replaceUserOverrides(id,{"new",{}},tinyOverride);
  OPENHDK_FAIL_IF(63,failedOverride.succeeded() || failedOverride.error->code!=CatalogMetadataErrorCode::LimitExceeded
      || budgetOwner.owner->snapshot()!=budgetSnapshot || fresh.owner->snapshot()!=check);
  return 0;
}
