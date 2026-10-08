// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/CatalogCheckpointStore.hpp"
#include "tests/TestCheck.hpp"
#include <cstdlib>
#include <iostream>
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
namespace {
// In-memory namespace simulation only; no OS locking/durability claim.
struct Image {
  std::optional<CheckpointBytes> primary;
  bool locked=false,pending=false;
  std::uint64_t directory=1;
};
class Fake final:public CheckpointStoreProvider {
 public:
  explicit Fake(Image& i):image(i){}
  Image& image;
  struct Entry {StoreArtifactKind kind;CheckpointBytes bytes;bool synced=false,consumed=false;std::uint64_t lease=0;};
  std::map<std::uint64_t,Entry> entries;
  std::uint64_t next=1,directory=0,lease=0;
  std::optional<StoreOperation> fail;
  StorePublication publication=StorePublication::Published;
  bool uncertainNew=false,corruptRead=false,cleanupFails=false,partialCreate=false;
  bool* cancelAtPublish=nullptr;
  bool forbidAllocationAtPublish=false;
  std::array<StoreOperation,512> events{};std::size_t eventCount=0;
  void log(StoreOperation op) noexcept {if(eventCount<events.size())events[eventCount++]=op;}
  std::optional<StoreProviderError> error(StoreOperation op) noexcept {
    log(op);if(fail==op)return StoreProviderError{StoreErrorCode::StorageFailure,321};return {};
  }
  std::optional<StoreProviderError> acquire() override {
    if(auto e=error(StoreOperation::Open))return e;
    if(image.locked)return StoreProviderError{StoreErrorCode::Busy};
    image.locked=true;directory=image.directory;++lease;return {};
  }
  void release() noexcept override {log(StoreOperation::Close);image.locked=false;}
  std::optional<StoreProviderError> checkDirectory() noexcept override {
    if(directory!=image.directory)return StoreProviderError{StoreErrorCode::SourceChanged};
    return {};
  }
  StoreReadResult readPrimary(std::size_t limit) override {
    if(auto e=error(StoreOperation::Read))return {{},e};
    if(!image.primary)return {{},StoreProviderError{StoreErrorCode::NotFound}};
    if(image.primary->size()>limit)return {{},StoreProviderError{StoreErrorCode::LimitExceeded}};
    return {*image.primary,{}};
  }
  StoreArtifactResult createArtifact(StoreArtifactKind kind) override {
    const auto op=kind==StoreArtifactKind::Candidate?StoreOperation::CreateCandidate:StoreOperation::CreatePrior;
    if(auto e=error(op))return {{},e};
    const auto id=next++;entries.emplace(id,Entry{kind,{},false,false,lease});
    if(partialCreate)return {StoreArtifact{id},StoreProviderError{StoreErrorCode::StorageFailure,321}};
    return {StoreArtifact{id},{}};
  }
  std::optional<StoreProviderError> writeArtifact(StoreArtifact a,std::span<const std::uint8_t> b) override {
    auto& e=entries.at(a.identity);
    if(auto err=error(e.kind==StoreArtifactKind::Candidate?StoreOperation::WriteCandidate:StoreOperation::WritePrior))return err;
    e.bytes.assign(b.begin(),b.end());return {};
  }
  std::optional<StoreProviderError> syncArtifact(StoreArtifact a) noexcept override {
    auto& e=entries.at(a.identity);
    if(auto err=error(e.kind==StoreArtifactKind::Candidate?StoreOperation::SyncCandidate:StoreOperation::SyncPrior))return err;
    e.synced=true;return {};
  }
  StoreReadResult readArtifact(StoreArtifact a,std::size_t limit) override {
    const auto& e=entries.at(a.identity);
    if(auto err=error(e.kind==StoreArtifactKind::Candidate?StoreOperation::VerifyCandidate:StoreOperation::VerifyPrior))return {{},err};
    if(e.bytes.size()>limit)return {{},StoreProviderError{StoreErrorCode::LimitExceeded}};
    auto bytes=e.bytes;if(corruptRead)bytes.back()^=1U;return {std::move(bytes),{}};
  }
  StorePublicationResult publish(StoreArtifact a,bool absent) noexcept override {
    log(StoreOperation::Publish);
    if(forbidAllocationAtPublish)failAfter=0;
    auto& entry=entries.at(a.identity);
    if(!entry.synced || absent==image.primary.has_value())return {StorePublication::NotCommitted,StoreProviderError{StoreErrorCode::SourceChanged}};
    if(cancelAtPublish)*cancelAtPublish=true;
    if(publication==StorePublication::NotCommitted)return {publication,StoreProviderError{StoreErrorCode::StorageFailure,321}};
    if(publication==StorePublication::Published || uncertainNew) {
      image.primary=std::move(entry.bytes);entry.consumed=true;
    }
    if(publication==StorePublication::Uncertain)image.pending=true;
    return {publication,{}};
  }
  std::optional<StoreProviderError> syncPublication() noexcept override {
    if(auto e=error(StoreOperation::SyncPublication)){image.pending=true;return e;}return {};
  }
  std::optional<StoreProviderError> reconcilePublication(bool exists) noexcept override {
    if(auto e=error(StoreOperation::Reconcile))return e;
    if(!exists && image.pending)return StoreProviderError{StoreErrorCode::NotFound};
    image.pending=false;return {};
  }
  std::optional<StoreProviderError> cleanup(StoreArtifact a) noexcept override {
    log(StoreOperation::Cleanup);
    if(cleanupFails)return StoreProviderError{StoreErrorCode::StorageFailure,321};
    const auto found=entries.find(a.identity);
    if(found==entries.end() || found->second.lease!=lease)return StoreProviderError{StoreErrorCode::SourceChanged};
    entries.erase(found);return {}; // Erase owned capability only, never primary.
  }
};
CatalogCheckpointProjection projection() {
  CatalogCheckpointProjection p;p.catalogRevision=1;p.nextRoot=2;p.nextSong=2;
  p.roots.push_back({1,1,{},"/library"});p.songs.push_back({1,1,SourceMemberRole::PrimaryMidi,"song.kar",{}, {}});return p;
}
CheckpointBytes encode(const CatalogCheckpointProjection& p) {
  auto r=encodeCatalogCheckpoint(p);if(!r.succeeded())std::abort();return std::move(*r.value);
}
bool failed(const StoreSaveResult& r,StoreErrorCode c) {return !r.succeeded() && !r.expectation && !r.status && r.error && r.error->code==c;}
}
int main() {
  Image image;Fake fake(image);CatalogCheckpointStore store(fake);
  auto opened=store.open();OPENHDK_FAIL_IF(1,!opened.succeeded() || opened.projection || opened.expectation->token());
  auto p=projection();auto first=store.save(p,*opened.expectation);
  OPENHDK_FAIL_IF(2,!first.succeeded() || first.expectation->token()->sequence!=1 || !fake.entries.empty());
  const auto wire=*image.primary;
  auto noop=store.save(p,*first.expectation);OPENHDK_FAIL_IF(3,noop.status!=StoreSaveStatus::Unchanged || *image.primary!=wire);
  ++p.catalogRevision;p.songs[0].title="Title";
  auto second=store.save(p,*first.expectation);OPENHDK_FAIL_IF(4,!second.succeeded() || second.expectation->token()->sequence!=2);
  OPENHDK_FAIL_IF(5,!failed(store.save(p,*first.expectation),StoreErrorCode::StaleCheckpoint));
  Image other;Fake otherFake(other);CatalogCheckpointStore otherStore(otherFake);auto foreign=otherStore.open();
  OPENHDK_FAIL_IF(6,!failed(store.save(p,*foreign.expectation),StoreErrorCode::ForeignExpectation));
  Fake sameImage(image);CatalogCheckpointStore contender(sameImage);auto busy=contender.open();OPENHDK_FAIL_IF(7,!busy.error || busy.error->code!=StoreErrorCode::Busy || !image.locked);
  // History rejects rollback, reused IDs, root loss, policy/gen/hint changes.
  for(unsigned kind=0;kind<6;++kind) {
    auto next=p;++next.catalogRevision;
    switch(kind) {
      case 0:next.catalogRevision=p.catalogRevision;next.songs[0].title="Changed";break;
      case 1:next.nextSong=1;break;
      case 2:next.roots.clear();next.songs.clear();break;
      case 3:next.roots[0].attachmentGeneration=0;break;
      case 4:next.roots[0].hint="/elsewhere";break;
      case 5:next.roots[0].policy.lyrics.encoding=LyricTextEncoding::Tis620;break;
    }
    auto r=store.save(next,*second.expectation);OPENHDK_FAIL_IF(8,r.succeeded() || r.expectation || decodeCatalogCheckpoint(*image.primary).value->sequence!=2);
  }
  // Every pre-publication provider failure retains primary and cleans only stages.
  for(auto op:{StoreOperation::CreateCandidate,StoreOperation::WriteCandidate,StoreOperation::SyncCandidate,
      StoreOperation::VerifyCandidate,StoreOperation::CreatePrior,StoreOperation::WritePrior,
      StoreOperation::SyncPrior,StoreOperation::VerifyPrior,StoreOperation::Read}) {
    auto next=p;++next.catalogRevision;const auto old=*image.primary;fake.fail=op;
    auto r=store.save(next,*second.expectation);fake.fail.reset();
    OPENHDK_FAIL_IF(9,!failed(r,StoreErrorCode::StorageFailure) || *image.primary!=old || !fake.entries.empty() || store.faulted());
  }
  auto next=p;++next.catalogRevision;fake.corruptRead=true;
  OPENHDK_FAIL_IF(10,!failed(store.save(next,*second.expectation),StoreErrorCode::InvalidCheckpoint));fake.corruptRead=false;
  bool cancelled=false;StoreControl control{[&]{return cancelled;},[&](StoreCheckpoint c){if(c==StoreCheckpoint::CandidateVerified)cancelled=true;}};
  OPENHDK_FAIL_IF(11,!failed(store.save(next,*second.expectation,control),StoreErrorCode::Cancelled) || !fake.entries.empty());
  control={ {},[&](StoreCheckpoint c){if(c==StoreCheckpoint::BeforePublication)++image.directory;} };
  OPENHDK_FAIL_IF(12,!failed(store.save(next,*second.expectation,control),StoreErrorCode::SourceChanged));--image.directory;
  control={ {},[&](StoreCheckpoint c){if(c==StoreCheckpoint::CandidateVerified){auto edit=p;edit.sequence=7;image.primary=encode(edit);}} };
  OPENHDK_FAIL_IF(13,!failed(store.save(next,*second.expectation,control),StoreErrorCode::StaleCheckpoint));
  image.primary=encode(p);p.sequence=2;image.primary=encode(p);
  // Reentrant operation must not release the active writer lease.
  bool reentry=false;control={ {},[&](StoreCheckpoint c){if(c==StoreCheckpoint::BeforeStaging){auto r=store.close();reentry=r && r->code==StoreErrorCode::Busy;}} };
  auto success=store.save(next,*second.expectation,control);OPENHDK_FAIL_IF(14,!success.succeeded() || !reentry || !image.locked);
  cancelled=false;fake.cancelAtPublish=&cancelled;control={ [&]{return cancelled;},{} };++next.catalogRevision;
  auto afterCancel=store.save(next,*success.expectation,control);OPENHDK_FAIL_IF(15,!afterCancel.succeeded() || !cancelled);fake.cancelAtPublish=nullptr;
  fake.cleanupFails=true;++next.catalogRevision;auto warning=store.save(next,*afterCancel.expectation);
  OPENHDK_FAIL_IF(16,!warning.succeeded() || !warning.cleanupWarning || !image.primary);fake.cleanupFails=false;fake.entries.clear();
  // Uncertain publication preserves stages, faults the store, and requires reopen.
  for(bool useNew:{false,true}) {
    Image disk;disk.primary=encode(projection());Fake provider(disk);CatalogCheckpointStore s(provider);auto start=s.open();
    auto changed=projection();++changed.catalogRevision;provider.publication=StorePublication::Uncertain;provider.uncertainNew=useNew;
    auto r=s.save(changed,*start.expectation);
    OPENHDK_FAIL_IF(17,!failed(r,StoreErrorCode::CommitUncertain) || !s.faulted() || provider.entries.size()!=2 || !r.candidate || !r.prior);
    OPENHDK_FAIL_IF(18,!failed(s.save(changed,*start.expectation),StoreErrorCode::CommitUncertain));
    s.close();provider.fail=StoreOperation::Reconcile;auto rejected=s.open();
    OPENHDK_FAIL_IF(19,rejected.succeeded() || !s.faulted() || disk.locked);
    provider.fail.reset();auto reopened=s.open();OPENHDK_FAIL_IF(20,!reopened.succeeded() || s.faulted() || reopened.expectation->token()->sequence!=(useNew?2U:1U) || provider.entries.size()!=2);
  }
  for(auto mode:{StorePublication::NotCommitted,StorePublication::Published}) {
    Image disk;disk.primary=encode(projection());const auto old=*disk.primary;Fake provider(disk);CatalogCheckpointStore s(provider);auto start=s.open();auto changed=projection();++changed.catalogRevision;
    provider.publication=mode;if(mode==StorePublication::Published)provider.fail=StoreOperation::SyncPublication;
    auto r=s.save(changed,*start.expectation);
    OPENHDK_FAIL_IF(21,r.succeeded() || r.expectation);
    OPENHDK_FAIL_IF(22,mode==StorePublication::NotCommitted ? (*disk.primary!=old || s.faulted() || !provider.entries.empty()) : (!s.faulted() || provider.entries.size()!=2));
  }
  // Exact no-op is legal at exhausted sequence; changed projection cannot wrap.
  Image exhausted;auto max=projection();max.sequence=UINT64_MAX;exhausted.primary=encode(max);Fake ex(exhausted);CatalogCheckpointStore xs(ex);auto xo=xs.open();
  OPENHDK_FAIL_IF(23,xs.save(max,*xo.expectation).status!=StoreSaveStatus::Unchanged);++max.catalogRevision;
  OPENHDK_FAIL_IF(24,!failed(xs.save(max,*xo.expectation),StoreErrorCode::CounterExhausted));
  // Corrupt primary is never repaired or replaced with artifacts.
  Image corrupt;corrupt.primary=encode(projection());corrupt.primary->back()^=1U;Fake cf(corrupt);CatalogCheckpointStore cs(cf);auto co=cs.open();
  OPENHDK_FAIL_IF(25,co.succeeded() || !co.error || co.error->code!=StoreErrorCode::InvalidCheckpoint || corrupt.locked);
  Image bounded;Fake bf(bounded);CatalogCheckpointStore bs(bf);CatalogCheckpointLimits limits;limits.stagedBytes=100;auto bo=bs.open(limits);
  OPENHDK_FAIL_IF(26,!bo.succeeded() || !failed(bs.save(projection(),*bo.expectation),StoreErrorCode::LimitExceeded) || bounded.primary);
  limits.stagedBytes=0;Image invalid;Fake vf(invalid);CatalogCheckpointStore vs(vf);OPENHDK_FAIL_IF(27,vs.open(limits).succeeded() || vf.eventCount!=0);
  // Inject each allocation failure before publication; success never allocates
  // after the provider crosses the namespace boundary.
  bool reachedSuccess=false;
  for(std::ptrdiff_t fault=0;fault<120;++fault) {
    Image disk;disk.primary=encode(projection());const auto old=*disk.primary;
    Fake provider(disk);CatalogCheckpointStore s(provider);auto start=s.open();
    auto changed=projection();++changed.catalogRevision;
    failAfter=fault;auto r=s.save(changed,*start.expectation);failAfter=-1;
    if(r.succeeded()){reachedSuccess=true;break;}
    OPENHDK_FAIL_IF(28,!failed(r,StoreErrorCode::StorageFailure) || *disk.primary!=old || !provider.entries.empty() || s.faulted());
  }
  OPENHDK_FAIL_IF(29,!reachedSuccess);
  {
    Image disk;Fake provider(disk);CatalogCheckpointStore s(provider);auto start=s.open();auto changed=projection();
    provider.forbidAllocationAtPublish=true;auto r=s.save(changed,*start.expectation);failAfter=-1;
    OPENHDK_FAIL_IF(30,!r.succeeded() || !disk.primary);
  }
  // Exact shared ledger boundary for an empty create: two 96-byte wires coexist.
  for(std::size_t budget:{191U,192U}) {
    Image disk;Fake provider(disk);CatalogCheckpointStore s(provider);CatalogCheckpointLimits l;l.stagedBytes=budget;
    auto start=s.open(l);CatalogCheckpointProjection empty;auto r=s.save(empty,*start.expectation);
    OPENHDK_FAIL_IF(31,budget==192U ? !r.succeeded() : !failed(r,StoreErrorCode::LimitExceeded));
  }
  // A faulted store with a missing primary must not recreate it or adopt stages.
  {
    Image disk;Fake provider(disk);CatalogCheckpointStore s(provider);auto start=s.open();
    provider.publication=StorePublication::Uncertain;auto r=s.save(projection(),*start.expectation);
    OPENHDK_FAIL_IF(32,!failed(r,StoreErrorCode::CommitUncertain));s.close();
    auto reopened=s.open();OPENHDK_FAIL_IF(33,reopened.succeeded() || !s.faulted() || disk.primary || provider.entries.empty());
  }
  // Removed IDs remain retired; new rows must use the allocator high water.
  {
    Image disk;auto initial=projection();initial.nextSong=5;disk.primary=encode(initial);
    Fake provider(disk);CatalogCheckpointStore s(provider);auto start=s.open();
    auto removed=initial;++removed.catalogRevision;removed.songs.clear();
    auto saved=s.save(removed,*start.expectation);OPENHDK_FAIL_IF(34,!saved.succeeded());
    auto reuse=removed;++reuse.catalogRevision;reuse.songs=initial.songs;
    OPENHDK_FAIL_IF(35,!failed(s.save(reuse,*saved.expectation),StoreErrorCode::StaleCheckpoint));
    reuse.songs[0].id=5;reuse.nextSong=6;auto fresh=s.save(reuse,*saved.expectation);
    OPENHDK_FAIL_IF(36,!fresh.succeeded());
    auto relocated=reuse;++relocated.catalogRevision;relocated.songs[0].locator="moved.mid";
    relocated.roots[0].hint="/moved";++relocated.roots[0].attachmentGeneration;
    OPENHDK_FAIL_IF(37,!s.save(relocated,*fresh.expectation).succeeded());
  }
  // Foreign admission and ExpectedAbsent recheck perform no writes.
  {
    Image disk;Fake provider(disk);CatalogCheckpointStore s(provider);auto start=s.open();
    const auto calls=provider.eventCount;auto r=s.save(projection(),*foreign.expectation);
    OPENHDK_FAIL_IF(38,!failed(r,StoreErrorCode::ForeignExpectation) || provider.eventCount!=calls);
    disk.primary=encode(projection());r=s.save(projection(),*start.expectation);
    OPENHDK_FAIL_IF(39,!failed(r,StoreErrorCode::StaleCheckpoint) || !provider.entries.empty());
  }
  bool openSuccess=false;
  for(std::ptrdiff_t fault=0;fault<100;++fault) {
    Image disk;disk.primary=encode(projection());Fake provider(disk);CatalogCheckpointStore s(provider);
    failAfter=fault;auto r=s.open();failAfter=-1;
    if(r.succeeded()){openSuccess=true;break;}
    OPENHDK_FAIL_IF(40,!r.error || r.error->code!=StoreErrorCode::StorageFailure || disk.locked || s.isOpen());
  }
  OPENHDK_FAIL_IF(41,!openSuccess);
  // Unowned and abandoned capabilities are neither selected nor cleaned.
  {
    Image disk;Fake provider(disk);provider.entries.emplace(777,Fake::Entry{StoreArtifactKind::Candidate,encode(projection()),true,false,0});
    CatalogCheckpointStore s(provider);auto start=s.open();auto saved=s.save(projection(),*start.expectation);
    OPENHDK_FAIL_IF(42,!saved.succeeded() || !provider.entries.contains(777) || !provider.cleanup({777}));
    const std::array expected={StoreOperation::Open,StoreOperation::Read,StoreOperation::Reconcile,
      StoreOperation::Read,StoreOperation::CreateCandidate,StoreOperation::WriteCandidate,
      StoreOperation::SyncCandidate,StoreOperation::VerifyCandidate,StoreOperation::Read,
      StoreOperation::Publish,StoreOperation::SyncPublication,StoreOperation::Cleanup};
    OPENHDK_FAIL_IF(43,provider.eventCount<expected.size() || !std::equal(expected.begin(),expected.end(),provider.events.begin()));
  }
  {
    Image disk;Fake provider(disk);CatalogCheckpointStore s(provider);auto start=s.open();
    provider.partialCreate=true;auto r=s.save(projection(),*start.expectation);
    OPENHDK_FAIL_IF(44,!failed(r,StoreErrorCode::StorageFailure) || r.error->nativeError!=321 || !provider.entries.empty() || disk.primary);
  }
  std::cout<<"checkpoint protocol checks passed\n";
}
