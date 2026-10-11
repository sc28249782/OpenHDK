// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/LibraryCliSession.hpp"
#include "tests/TestCheck.hpp"
#include <sstream>
#include <fstream>
#include <chrono>
namespace { std::ptrdiff_t failAfter = -1; }
#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif
TEST_NOINLINE void* operator new(std::size_t n) {
  if (failAfter == 0) throw std::bad_alloc();
  if (failAfter > 0) --failAfter;
  if (auto p = std::malloc(n ? n : 1U)) return p;
  throw std::bad_alloc();
}
TEST_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
TEST_NOINLINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
#undef TEST_NOINLINE
using namespace OpenHDK;
using Access=DurableLibraryServiceTestAccess;
namespace {
struct Image { std::optional<CheckpointBytes> primary; bool leased = false; };
struct Fake final : DurableLibraryTestProvider {
  explicit Fake(Image& target) : image(target) {}
  Image& image;
  struct Slot { CheckpointBytes bytes; bool active = false, synced = false; };
  std::array<Slot, 2> slots;
  std::optional<StoreOperation> fail;
  StorePublication outcome = StorePublication::Published;
  bool uncertainNew = false, corruptVerify = false, cleanupWarning = false, banAllocations = false;
  bool* lateCancel = nullptr;
  void (*observe)(void*, StoreOperation) noexcept = nullptr;
  void* context = nullptr;
  std::size_t reads = 0, publications = 0, syncs = 0;
  std::optional<StoreProviderError> fault(StoreOperation op) noexcept {
    if (fail == op) return StoreProviderError{StoreErrorCode::StorageFailure, 123};
    return {};
  }
  std::optional<StoreProviderError> acquire() override {
    if (auto e = fault(StoreOperation::Open)) return e;
    if (image.leased) return StoreProviderError{StoreErrorCode::Busy};
    image.leased = true; return {};
  }
  void release() noexcept override { image.leased = false; }
  std::size_t retainedBytes() const noexcept override { return 5U; }
  std::optional<StoreProviderError> checkDirectory() noexcept override { return {}; }
  StoreReadResult readPrimary(std::size_t limit) override {
    ++reads;
    if (auto e = fault(StoreOperation::Read)) return {{}, e};
    if (!image.primary) return {{}, StoreProviderError{StoreErrorCode::NotFound}};
    if (image.primary->size() > limit) return {{}, StoreProviderError{StoreErrorCode::LimitExceeded}};
    return {*image.primary, {}};
  }
  StoreArtifactResult createArtifact(StoreArtifactKind kind) override {
    const auto i = kind == StoreArtifactKind::Candidate ? 0U : 1U;
    if (auto e = fault(i == 0 ? StoreOperation::CreateCandidate : StoreOperation::CreatePrior)) return {{}, e};
    if (slots[i].active) return {{}, StoreProviderError{StoreErrorCode::Busy}};
    slots[i].active = true; return {StoreArtifact{i + 1U}, {}};
  }
  std::optional<StoreProviderError> writeArtifact(StoreArtifact artifact, std::span<const std::uint8_t> bytes) override {
    if (auto e = fault(artifact.identity == 1 ? StoreOperation::WriteCandidate : StoreOperation::WritePrior)) return e;
    slots[artifact.identity - 1U].bytes.assign(bytes.begin(), bytes.end()); return {};
  }
  std::optional<StoreProviderError> syncArtifact(StoreArtifact artifact) noexcept override {
    if (auto e = fault(artifact.identity == 1 ? StoreOperation::SyncCandidate : StoreOperation::SyncPrior)) return e;
    slots[artifact.identity - 1U].synced = true; return {};
  }
  StoreReadResult readArtifact(StoreArtifact artifact, std::size_t limit) override {
    if (auto e = fault(artifact.identity == 1 ? StoreOperation::VerifyCandidate : StoreOperation::VerifyPrior)) return {{}, e};
    const auto& slot = slots[artifact.identity - 1U];
    if (slot.bytes.size() > limit) return {{}, StoreProviderError{StoreErrorCode::LimitExceeded}};
    auto bytes = slot.bytes; if (corruptVerify) bytes.back() ^= 1U; return {std::move(bytes), {}};
  }
  StorePublicationResult publish(StoreArtifact artifact, bool absent) noexcept override {
    ++publications;
    if (observe) observe(context, StoreOperation::Publish);
    if(banAllocations) failAfter=0;

    if (lateCancel) *lateCancel = true;
    if (absent == image.primary.has_value() || !slots[artifact.identity - 1U].synced)
      return {StorePublication::NotCommitted, StoreProviderError{StoreErrorCode::SourceChanged}};
    if (outcome == StorePublication::Published || uncertainNew) {
      if (!image.primary) image.primary.emplace();
      image.primary->swap(slots[artifact.identity - 1U].bytes);
    }
    return {outcome, outcome == StorePublication::NotCommitted
        ? std::optional<StoreProviderError>{{StoreErrorCode::StorageFailure, 123}} : std::nullopt};
  }
  std::optional<StoreProviderError> syncPublication() noexcept override {
    ++syncs; if (observe) observe(context, StoreOperation::SyncPublication);
    return fault(StoreOperation::SyncPublication);
  }
  std::optional<StoreProviderError> reconcilePublication(bool) noexcept override { return fault(StoreOperation::Reconcile); }
  std::optional<StoreProviderError> cleanup(StoreArtifact artifact) noexcept override {
    if (cleanupWarning) return StoreProviderError{StoreErrorCode::StorageFailure, 123};
    slots[artifact.identity - 1U] = {}; return {};
  }
};

struct FailBuffer : std::streambuf {
  std::streamsize xsputn(const char*,std::streamsize) override {return 0;}
  int overflow(int) override {return traits_type::eof();}
};
bool has(const LibraryCliResponse& r,std::string_view text) {
  return r.output.bytes && r.output.bytes->find(text)!=std::string::npos;
}
CatalogCheckpointProjection disk(const Image& image) {
  auto result=decodeCatalogCheckpoint(*image.primary);
  if(!result.succeeded()) std::abort();
  return std::move(*result.value);
}
}
int main() {
  const auto dir=std::filesystem::temp_directory_path()/
    ("openhdk-cli-session-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(dir);
  struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}} cleanup{dir};
  const auto a=dir/"songs",b=dir/"moved";
  std::filesystem::create_directory(a);
  const std::vector<std::uint8_t> midi{'M','T','h','d',0,0,0,6,0,0,0,1,0,3,'M','T','r','k',0,0,0,9,0,0xff,5,1,'A',0,0xff,0x2f,0};
  {std::ofstream out(a/"Live.Set.kar",std::ios::binary);out.write(reinterpret_cast<const char*>(midi.data()),midi.size());}
  Image image;auto provider=std::make_unique<Fake>(image);auto* p=provider.get();
  auto created=Access::create(std::make_unique<SongDiscovery>(),std::move(provider));
  OPENHDK_FAIL_IF(1,!created.succeeded() || !created.receipt);
  auto* service=created.service.get();LibraryCliSession session(std::move(created.service));
  const auto original=service->snapshot();
  OPENHDK_FAIL_IF(2,!has(session.dispatch("roots"),"\"count\":0"));
  OPENHDK_FAIL_IF(3,!has(session.dispatch("help"),"prepare"));
  auto registered=session.dispatch("register \""+a.generic_string()+"\"");
  OPENHDK_FAIL_IF(4,registered.commit!=LibraryCliCommitOutcome::Saved || !has(registered,"\"root\":1"));
  OPENHDK_FAIL_IF(5,original->roots.size()!=0 || service->snapshot()->roots.size()!=1);
  OPENHDK_FAIL_IF(6,!has(session.dispatch("roots"),"\"attached\":true"));
  auto scanned=session.dispatch("scan 1");
  OPENHDK_FAIL_IF(7,scanned.commit!=LibraryCliCommitOutcome::Saved || disk(image).songs.size()!=1);
  const auto rev=disk(image).catalogRevision;
  OPENHDK_FAIL_IF(8,session.dispatch("scan 1").commit!=LibraryCliCommitOutcome::Saved || disk(image).catalogRevision!=rev+1);
  auto listing=session.dispatch("songs 1 0 100");
  OPENHDK_FAIL_IF(9,!has(listing,"Live.Set") || !has(listing,"\"id\":1") || has(listing,a.generic_string()));
  OPENHDK_FAIL_IF(10,!has(session.dispatch("songs 1 99 10"),"\"count\":0"));
  OPENHDK_FAIL_IF(11,session.dispatch("songs 1 0 101").exit!=1);
  const auto wire=*image.primary;const auto publications=p->publications;
  OPENHDK_FAIL_IF(12,session.dispatch("remove 999").exit!=1 || image.primary!=wire || p->publications!=publications);
  OPENHDK_FAIL_IF(13,session.dispatch("scan 999").exit!=1 || image.primary!=wire);
  OPENHDK_FAIL_IF(14,session.dispatch("overrides 1 Title").exit!=1 || image.primary!=wire);
  OPENHDK_FAIL_IF(15,session.dispatch("overrides 1 \"\" -").exit!=1 || image.primary!=wire);
  auto changed=session.dispatch("overrides 1 \"ไทย\" \"-\"");
  OPENHDK_FAIL_IF(16,changed.commit!=LibraryCliCommitOutcome::Saved || disk(image).songs[0].title!="ไทย" || disk(image).songs[0].artist!="-");
  OPENHDK_FAIL_IF(17,session.dispatch("overrides 1 \"ไทย\" \"-\"").commit!=LibraryCliCommitOutcome::NoChange);
  OPENHDK_FAIL_IF(18,!has(session.dispatch("songs 1 0 1"),"ไทย"));
  OPENHDK_FAIL_IF(19,session.dispatch("overrides 1 - -").commit!=LibraryCliCommitOutcome::Saved || disk(image).songs[0].title || disk(image).songs[0].artist);
  p->cleanupWarning=true;
  OPENHDK_FAIL_IF(20,!has(session.dispatch("overrides 1 New Artist"),"cleanup_warning"));
  p->cleanupWarning=false;
  // Fake cleanup-warning intentionally retains slots; release those harness-owned slots.
  p->slots={};
  p->fail=StoreOperation::WriteCandidate;const auto prior=*image.primary;
  auto failed=session.dispatch("overrides 1 Failed Artist");
  OPENHDK_FAIL_IF(21,failed.commit!=LibraryCliCommitOutcome::Rejected || image.primary!=prior || has(failed,"\"status\""));
  OPENHDK_FAIL_IF(22,!has(failed,"store_operation") || service->state()!=DurableServiceState::Ready);
  p->fail.reset();
  OPENHDK_FAIL_IF(23,session.dispatch("relocate 1 1 Live.Set.kar").commit!=LibraryCliCommitOutcome::Saved || service->snapshot()->songs[0].state!=CatalogState::Invalid);
  OPENHDK_FAIL_IF(24,session.dispatch("remove 1").commit!=LibraryCliCommitOutcome::Saved || !std::filesystem::exists(a/"Live.Set.kar"));
  OPENHDK_FAIL_IF(25,session.dispatch("scan 1").commit!=LibraryCliCommitOutcome::Saved || disk(image).songs[0].id!=2 || disk(image).songs[0].title);
  std::filesystem::rename(a,b);
  auto repair=session.dispatch("reattach 1 \""+b.generic_string()+"\"");
  OPENHDK_FAIL_IF(26,repair.commit!=LibraryCliCommitOutcome::Saved || disk(image).roots[0].attachmentGeneration!=2);
  OPENHDK_FAIL_IF(27,session.dispatch("reattach 1 \""+b.generic_string()+"\"").commit!=LibraryCliCommitOutcome::NoChange);
  OPENHDK_FAIL_IF(28,session.dispatch("scan 1").commit!=LibraryCliCommitOutcome::Saved);
  OPENHDK_FAIL_IF(29,!has(session.dispatch("prepare 2"),"\"type\":\"prepared\""));
  OPENHDK_FAIL_IF(30,session.dispatch("remove +2").exit!=1);
  auto contender=Access::open(std::make_unique<Fake>(image));
  OPENHDK_FAIL_IF(31,!contender.error || LibraryCliSession::factoryExit(*contender.error)!=75);
  const auto closedId=service->snapshot()->roots[0].id;service->close();
  OPENHDK_FAIL_IF(32,session.dispatch("roots").exit!=76 || service->rootAttached(closedId));
  auto reopened=Access::open(std::make_unique<Fake>(image));
  OPENHDK_FAIL_IF(33,!reopened.succeeded() || reopened.receipt);
  auto* restored=reopened.service.get();LibraryCliSession openSession(std::move(reopened.service));
  OPENHDK_FAIL_IF(34,!has(openSession.dispatch("roots"),"\"attached\":false") || restored->snapshot()->songs[0].state!=CatalogState::Invalid);
  OPENHDK_FAIL_IF(35,openSession.dispatch("scan 1").commit!=LibraryCliCommitOutcome::Rejected);
  OPENHDK_FAIL_IF(36,openSession.dispatch("reattach 1 \""+b.generic_string()+"\"").commit!=LibraryCliCommitOutcome::Saved || disk(image).roots[0].attachmentGeneration!=3);
  auto existingCreate=Access::create(std::make_unique<SongDiscovery>(),std::make_unique<Fake>(image));
  OPENHDK_FAIL_IF(37,!existingCreate.error);
  {std::ofstream bad(b/"broken.kar",std::ios::binary);bad<<"bad";}
  auto diagnostic=openSession.dispatch("scan 1");std::filesystem::remove(b/"broken.kar");
  OPENHDK_FAIL_IF(52,diagnostic.commit!=LibraryCliCommitOutcome::Saved || !has(diagnostic,"parse_offset") || !has(diagnostic,"discovery_operation"));
  restored->close();
  Image outputImage;auto outputProvider=std::make_unique<Fake>(outputImage);auto* op=outputProvider.get();
  auto owner=std::make_unique<SongDiscovery>();owner->registerRoot(b);owner->scan(owner->snapshot()->roots[0].id);
  auto outputService=Access::create(std::move(owner),std::move(outputProvider));
  LibraryCliSession delivery(std::move(outputService.service));
  FailBuffer failure;std::ostream broken(&failure);std::istringstream commands("overrides 1 Receipt Lost\nremove 1\n");
  const auto before=op->publications; (void)before;
  OPENHDK_FAIL_IF(38,delivery.run(commands,broken)!=74 || delivery.lastCommit()!=LibraryCliCommitOutcome::Saved);
  OPENHDK_FAIL_IF(39,disk(outputImage).songs[0].title!="Receipt" || disk(outputImage).songs.empty() || outputImage.leased);
  Image uncertainImage;auto uncertainProvider=std::make_unique<Fake>(uncertainImage);auto* up=uncertainProvider.get();
  auto uncertainOwner=std::make_unique<SongDiscovery>();uncertainOwner->registerRoot(b);uncertainOwner->scan(uncertainOwner->snapshot()->roots[0].id);
  auto uncertainService=Access::create(std::move(uncertainOwner),std::move(uncertainProvider));
  up->fail=StoreOperation::SyncPublication;
  LibraryCliSession uncertain(std::move(uncertainService.service));std::istringstream uncertainCommands("overrides 1 Maybe -\nremove 1\n");std::ostringstream uncertainOutput;
  OPENHDK_FAIL_IF(40,uncertain.run(uncertainCommands,uncertainOutput)!=76 || uncertain.lastCommit()!=LibraryCliCommitOutcome::Uncertain || disk(uncertainImage).songs.empty());
  Image eofImage;auto eofService=Access::create(std::make_unique<SongDiscovery>(),std::make_unique<Fake>(eofImage));
  LibraryCliSession eof(std::move(eofService.service));std::istringstream input(std::string(16385,'x')+"\nroots\nquit\nregister ignored");std::ostringstream output;
  OPENHDK_FAIL_IF(41,eof.run(input,output)!=1 || eofImage.leased || output.str().find("page")==std::string::npos || !disk(eofImage).roots.empty());
  Image emptyImage;auto emptyService=Access::create(std::make_unique<SongDiscovery>(),std::make_unique<Fake>(emptyImage));
  LibraryCliSession empty(std::move(emptyService.service));std::istringstream emptyInput("roots");std::ostringstream emptyOutput;
  OPENHDK_FAIL_IF(42,empty.run(emptyInput,emptyOutput)!=0 || emptyImage.leased || emptyOutput.str().find("page")==std::string::npos);
  Image absent;auto absentOpen=Access::open(std::make_unique<Fake>(absent));
  OPENHDK_FAIL_IF(43,!absentOpen.error || absent.primary);

  Image noChangeImage;auto noChangeProvider=std::make_unique<Fake>(noChangeImage);
  auto noChangeOwner=std::make_unique<SongDiscovery>();noChangeOwner->registerRoot(b);noChangeOwner->scan(noChangeOwner->snapshot()->roots[0].id);
  auto noChangeService=Access::create(std::move(noChangeOwner),std::move(noChangeProvider));
  LibraryCliSession noChange(std::move(noChangeService.service));const auto noChangeWire=*noChangeImage.primary;
  std::istringstream noChangeCommands("overrides 1 - -\nremove 1\n");std::ostream failedNoChange(&failure);
  OPENHDK_FAIL_IF(44,noChange.run(noChangeCommands,failedNoChange)!=74 || noChange.lastCommit()!=LibraryCliCommitOutcome::NoChange || noChangeImage.primary!=noChangeWire);
  Image allocationImage;auto allocationProvider=std::make_unique<Fake>(allocationImage);auto* ap=allocationProvider.get();
  auto allocationService=Access::create(std::make_unique<SongDiscovery>(),std::move(allocationProvider));
  LibraryCliSession allocation(std::move(allocationService.service));ap->banAllocations=true;
  std::istringstream allocationCommands("register \""+b.generic_string()+"\"\nremove 1\n");std::ostringstream allocationOutput;
  const auto allocationCode=allocation.run(allocationCommands,allocationOutput);failAfter=-1;
  OPENHDK_FAIL_IF(45,allocationCode!=74 || allocation.lastCommit()!=LibraryCliCommitOutcome::Saved || disk(allocationImage).roots.size()!=1 || allocationImage.leased);
  Image limitedImage;auto limits=CatalogCheckpointLimits{};limits.stagedBytes=LibraryCliSession::kCallerBytes-1;
  auto limitedService=Access::create(std::make_unique<SongDiscovery>(),std::make_unique<Fake>(limitedImage),limits);
  OPENHDK_FAIL_IF(46,!limitedService.succeeded());
  LibraryCliSession limited(std::move(limitedService.service));
  OPENHDK_FAIL_IF(47,!has(limited.dispatch("roots"),"LimitExceeded"));
  Image badInputImage;auto badInputService=Access::create(std::make_unique<SongDiscovery>(),std::make_unique<Fake>(badInputImage));
  LibraryCliSession badInput(std::move(badInputService.service));std::istringstream badInputStream;badInputStream.setstate(std::ios::badbit);std::ostringstream badInputOutput;
  OPENHDK_FAIL_IF(48,badInput.run(badInputStream,badInputOutput)!=74 || badInputImage.leased);
  OPENHDK_FAIL_IF(49,!has(failed,"native_error") || has(failed,"candidate") || has(failed,"prior"));
  bool sweepCompleted=false;
  for(std::ptrdiff_t n=0;n<256;++n) {
    Image sweepImage;auto sweepOwner=std::make_unique<SongDiscovery>();sweepOwner->registerRoot(b);
    sweepOwner->scan(sweepOwner->snapshot()->roots[0].id);
    auto sweepService=Access::create(std::move(sweepOwner),std::make_unique<Fake>(sweepImage));
    const auto sweepWire=*sweepImage.primary;
    LibraryCliSession sweep(std::move(sweepService.service));
    std::istringstream sweepCommands("overrides 1 New Artist\n");std::ostringstream sweepOutput;
    failAfter=n;const auto code=sweep.run(sweepCommands,sweepOutput);failAfter=-1;
    const auto projection=disk(sweepImage);
    OPENHDK_FAIL_IF(50,sweepImage.leased || (sweep.lastCommit()==LibraryCliCommitOutcome::Saved
        ? projection.songs[0].title!="New" : sweepImage.primary!=sweepWire));
    if(code==0) {sweepCompleted=true;break;}
  }
  OPENHDK_FAIL_IF(51,!sweepCompleted);
  // Independent JSON validation consumes these complete, bounded responses.
  std::cout<<*listing.output.bytes<<uncertainOutput.str()<<output.str()<<emptyOutput.str();
  return 0;
}
