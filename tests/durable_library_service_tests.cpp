// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/DurableLibraryService.hpp"
#include "library/LinuxCheckpointProvider.hpp"
#include "tests/TestCheck.hpp"
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <type_traits>
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
using Access = DurableLibraryServiceTestAccess;
namespace {
// Independent two-slot in-memory namespace model. There is no native I/O,
// lease/path acceptance or power-loss claim. Publication uses vector swap.
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
    if (banAllocations) failAfter = 0;
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
CatalogCheckpointProjection projection() {
  CatalogCheckpointProjection p; p.catalogRevision = 3; p.nextRoot = 17; p.nextSong = 23;
  p.roots.push_back({2, 7, {}, "/nonexistent/hint"});
  p.songs.push_back({4, 2, SourceMemberRole::PrimaryMidi, "song.kar", "Old", "Artist"}); return p;
}
CheckpointBytes encode(const CatalogCheckpointProjection& p) {
  auto r = encodeCatalogCheckpoint(p); if (!r.succeeded()) std::abort(); return std::move(*r.value);
}
std::unique_ptr<SongDiscovery> owner(const CatalogCheckpointProjection& p) {
  auto r = CatalogCheckpointRestore::fromProjection(p); if (!r.succeeded()) std::abort(); return std::move(r.owner);
}
CatalogCheckpointProjection disk(const Image& image) {
  auto r = decodeCatalogCheckpoint(*image.primary); if (!r.succeeded()) std::abort(); return std::move(*r.value);
}
bool failed(const DurableOverrideResult& result, DurableServiceErrorCode code) {
  return !result.succeeded() && !result.status && !result.snapshot && !result.receipt && result.error && result.error->code == code;
}
struct Observe {
  DurableLibraryService* service;
  std::shared_ptr<const CatalogSnapshot> old;
  bool publicationOld = false, syncOld = false;
  static void at(void* ctx, StoreOperation op) noexcept {
    auto& self = *static_cast<Observe*>(ctx);
    if (op == StoreOperation::Publish) self.publicationOld = self.service->snapshot() == self.old;
    if (op == StoreOperation::SyncPublication) self.syncOld = self.service->snapshot() == self.old;
  }
};
}
int main() {
  static_assert(!std::is_default_constructible_v<DurableLibraryService>);
  static_assert(!std::is_copy_constructible_v<DurableLibraryService>);
  static_assert(!std::is_convertible_v<std::unique_ptr<LinuxCheckpointProvider>, std::unique_ptr<DurableLibraryTestProvider>>);
  Image image; auto provider = std::make_unique<Fake>(image); auto* fake = provider.get();
  auto source = owner(projection()); const auto historical = source->snapshot(); const auto id = historical->songs[0].id;
  auto created = Access::create(std::move(source), std::move(provider));
  OPENHDK_FAIL_IF(1, !created.succeeded() || !created.receipt || created.receipt->token.sequence != 1 || !image.leased);
  auto& service = *created.service;
  OPENHDK_FAIL_IF(2, service.state() != DurableServiceState::Ready || service.snapshot() != historical);
  OPENHDK_FAIL_IF(3, !StoreDetail::equalProjection(disk(image), projection()));
  const auto prior = *image.primary; const auto reads = fake->reads;
  auto noop = service.replaceUserOverrides(id, {"Old", "Artist"});
  OPENHDK_FAIL_IF(4, !noop.succeeded() || noop.status != DurableOverrideStatus::NoChange || noop.snapshot != historical
      || noop.receipt->token != created.receipt->token || *image.primary != prior || fake->reads <= reads);
  Observe observe{&service, historical}; fake->observe = Observe::at; fake->context = &observe;
  const std::string title = "\xef\xbb\xbf Title e\xcc\x81 ";
  auto updated = service.replaceUserOverrides(id, {title, "ศิลปิน"});
  OPENHDK_FAIL_IF(5, !updated.succeeded() || updated.status != DurableOverrideStatus::Updated || !observe.publicationOld || !observe.syncOld);
  OPENHDK_FAIL_IF(6, service.snapshot() != updated.snapshot || updated.snapshot == historical || updated.snapshot->revision != historical->revision + 1);
  OPENHDK_FAIL_IF(7, updated.snapshot->songs[0].overrides->title->bytes() != title || updated.snapshot->songs[0].overrides->artist->bytes() != "ศิลปิน");
  const auto stored = disk(image);
  OPENHDK_FAIL_IF(8, stored.nextRoot != 17 || stored.nextSong != 23 || stored.roots[0].attachmentGeneration != 7
      || stored.songs[0].id != 4 || stored.songs[0].locator != "song.kar" || updated.receipt->token.sequence != 2);
  OPENHDK_FAIL_IF(9, historical->songs[0].overrides->title->bytes() != "Old"
      || updated.snapshot->songs[0].state != historical->songs[0].state || updated.snapshot->songs[0].metadata != historical->songs[0].metadata
      || updated.snapshot->songs[0].sourceRevision() != historical->songs[0].sourceRevision());
  auto before = service.snapshot(); auto bytes = *image.primary;
  auto invalid = service.replaceUserOverrides(id, {"", "New"});
  OPENHDK_FAIL_IF(10, !failed(invalid, DurableServiceErrorCode::MetadataFailure) || invalid.error->metadata->field != CatalogMetadataField::Title
      || service.snapshot() != before || *image.primary != bytes);
  const std::string malformed(1, static_cast<char>(0x80));
  auto badArtist = service.replaceUserOverrides(id, {"Valid", malformed});
  OPENHDK_FAIL_IF(11, !failed(badArtist, DurableServiceErrorCode::MetadataFailure)
      || badArtist.error->metadata->field != CatalogMetadataField::Artist || service.snapshot() != before);
  auto notFound = owner(CatalogCheckpointProjection{});
  Image emptyImage; auto empty = Access::create(std::move(notFound), std::make_unique<Fake>(emptyImage));
  OPENHDK_FAIL_IF(12, !empty.succeeded() || !failed(empty.service->replaceUserOverrides(id, {"X", {}}), DurableServiceErrorCode::MetadataFailure));
  auto cleared = service.replaceUserOverrides(id, {{}, {}});
  OPENHDK_FAIL_IF(13, !cleared.succeeded() || cleared.snapshot->songs[0].overrides || disk(image).songs[0].title || disk(image).songs[0].artist);
  fake->observe = nullptr;
  for (auto op : {StoreOperation::Read, StoreOperation::CreateCandidate, StoreOperation::WriteCandidate,
      StoreOperation::SyncCandidate, StoreOperation::VerifyCandidate, StoreOperation::CreatePrior,
      StoreOperation::WritePrior, StoreOperation::SyncPrior, StoreOperation::VerifyPrior}) {
    before = service.snapshot(); bytes = *image.primary; fake->fail = op;
    auto result = service.replaceUserOverrides(id, {"Failure", "Pair"}); fake->fail.reset();
    OPENHDK_FAIL_IF(14, !failed(result, DurableServiceErrorCode::StoreFailure) || service.snapshot() != before
        || *image.primary != bytes || service.state() != DurableServiceState::Ready || fake->slots[0].active || fake->slots[1].active);
  }
  fake->corruptVerify = true;
  OPENHDK_FAIL_IF(15, !failed(service.replaceUserOverrides(id, {"Corrupt", {}}), DurableServiceErrorCode::StoreFailure));
  fake->corruptVerify = false;
  before = service.snapshot(); bytes = *image.primary; fake->outcome = StorePublication::NotCommitted;
  OPENHDK_FAIL_IF(16, !failed(service.replaceUserOverrides(id, {"Reject", {}}), DurableServiceErrorCode::StoreFailure)
      || service.snapshot() != before || *image.primary != bytes || service.state() != DurableServiceState::Ready);
  fake->outcome = StorePublication::Published;
  bool cancelled = false;
  StoreControl control{[&] { return cancelled; }, [&](StoreCheckpoint phase) { if (phase == StoreCheckpoint::CandidateVerified) cancelled = true; }};
  OPENHDK_FAIL_IF(17, !failed(service.replaceUserOverrides(id, {"Cancel", {}}, control), DurableServiceErrorCode::StoreFailure)
      || service.snapshot() != before || *image.primary != bytes);
  bool nestedBusy = false, closeBusy = false;
  control = {{}, [&](StoreCheckpoint phase) {
    if (phase == StoreCheckpoint::BeforeStaging) {
      nestedBusy = failed(service.replaceUserOverrides(id, {"Nested", {}}), DurableServiceErrorCode::Busy);
      const auto e = service.close(); closeBusy = e && e->code == DurableServiceErrorCode::Busy;
    }
  }};
  OPENHDK_FAIL_IF(18, !service.replaceUserOverrides(id, {"Outer", {}}, control).succeeded() || !nestedBusy || !closeBusy || !image.leased);
  cancelled = false; fake->lateCancel = &cancelled; control = {[&] { return cancelled; }, {}};
  auto late = service.replaceUserOverrides(id, {"Late", {}}, control); fake->lateCancel = nullptr;
  OPENHDK_FAIL_IF(19, !late.succeeded() || !cancelled || service.snapshot() != late.snapshot);
  fake->cleanupWarning = true;
  auto warning = service.replaceUserOverrides(id, {"Warning", {}}); fake->cleanupWarning = false; fake->slots = {};
  OPENHDK_FAIL_IF(20, !warning.succeeded() || !warning.receipt->cleanupWarning || service.snapshot() != warning.snapshot || disk(image).songs[0].title != "Warning");
  fake->banAllocations = true;
  auto noAlloc = service.replaceUserOverrides(id, {"No allocation", "Both"}); failAfter = -1; fake->banAllocations = false;
  OPENHDK_FAIL_IF(21, !noAlloc.succeeded() || service.snapshot() != noAlloc.snapshot || disk(image).songs[0].title != "No allocation");
  // Allocation sweep covers stage/project/store. Every failed budget leaves the
  // baseline and disk exact; a subsequent explicit request consumes one revision.
  before = service.snapshot(); bytes = *image.primary; std::size_t throws = 0; bool passed = false;
  for (std::ptrdiff_t count = 0; count < 256; ++count) {
    failAfter = count; auto result = service.replaceUserOverrides(id, {"Sweep", "Pair"}); failAfter = -1;
    if (result.succeeded()) { passed = true; break; }
    ++throws;
    OPENHDK_FAIL_IF(22, !result.error || result.receipt || service.snapshot() != before || *image.primary != bytes || service.state() != DurableServiceState::Ready);
  }
  OPENHDK_FAIL_IF(23, !passed || throws < 10 || service.snapshot()->revision != before->revision + 1);
  for (bool useNew : {false, true}) {
    Image target; auto p = std::make_unique<Fake>(target); auto* providerPtr = p.get();
    auto started = Access::create(owner(projection()), std::move(p)); const auto old = started.service->snapshot();
    providerPtr->outcome = StorePublication::Uncertain; providerPtr->uncertainNew = useNew;
    auto uncertain = started.service->replaceUserOverrides(old->songs[0].id, {"Uncertain", {}});
    OPENHDK_FAIL_IF(24, !failed(uncertain, DurableServiceErrorCode::StoreFailure) || uncertain.error->store->code != StoreErrorCode::CommitUncertain
        || started.service->state() != DurableServiceState::RecoveryRequired || started.service->snapshot() != old
        || !uncertain.error->candidate || !uncertain.error->prior || !providerPtr->slots[0].active || !providerPtr->slots[1].active);
    OPENHDK_FAIL_IF(25, !failed(started.service->replaceUserOverrides(old->songs[0].id, {"Retry", {}}), DurableServiceErrorCode::RecoveryRequired));
    OPENHDK_FAIL_IF(26, started.service->close().has_value() || target.leased);
    auto recovery = Access::open(std::make_unique<Fake>(target));
    OPENHDK_FAIL_IF(27, !recovery.succeeded() || recovery.receipt || recovery.service->snapshot()->songs[0].state != CatalogState::Invalid
        || recovery.service->snapshot()->songs[0].sourceRevision() || recovery.service->snapshot() == old);
    OPENHDK_FAIL_IF(28, recovery.service->snapshot()->songs[0].overrides->title->bytes() != (useNew ? "Uncertain" : "Old")
        || old->songs[0].overrides->title->bytes() != "Old");
  }
  Image syncImage; auto syncProvider = std::make_unique<Fake>(syncImage); auto* syncFake = syncProvider.get();
  auto syncService = Access::create(owner(projection()), std::move(syncProvider)); const auto syncOld = syncService.service->snapshot();
  syncFake->fail = StoreOperation::SyncPublication;
  auto syncFailure = syncService.service->replaceUserOverrides(syncOld->songs[0].id, {"Visible", {}});
  OPENHDK_FAIL_IF(29, !failed(syncFailure, DurableServiceErrorCode::StoreFailure) || syncFailure.error->store->operation != StoreOperation::SyncPublication
      || syncService.service->snapshot() != syncOld || disk(syncImage).songs[0].title != "Visible");
  Image protocolImage; auto protocol = Access::create(owner(projection()), std::make_unique<Fake>(protocolImage));
  const auto protocolOld = protocol.service->snapshot(); Access::corruptAcknowledgment(*protocol.service);
  auto inconsistent = protocol.service->replaceUserOverrides(protocolOld->songs[0].id, {"Committed", {}});
  OPENHDK_FAIL_IF(30, !failed(inconsistent, DurableServiceErrorCode::ProtocolFault) || protocol.service->snapshot() != protocolOld
      || protocol.service->state() != DurableServiceState::RecoveryRequired || disk(protocolImage).songs[0].title != "Committed");
  Image staleImage; auto stale = Access::create(owner(projection()), std::make_unique<Fake>(staleImage)); const auto staleOld = stale.service->snapshot();
  auto external = disk(staleImage); ++external.sequence; staleImage.primary = encode(external);
  auto staleResult = stale.service->replaceUserOverrides(staleOld->songs[0].id, {"Old", "Artist"});
  OPENHDK_FAIL_IF(31, !failed(staleResult, DurableServiceErrorCode::StoreFailure) || staleResult.error->store->code != StoreErrorCode::StaleCheckpoint
      || stale.service->state() != DurableServiceState::RecoveryRequired || stale.service->snapshot() != staleOld);
  Image missing; auto missingOpen = Access::open(std::make_unique<Fake>(missing));
  OPENHDK_FAIL_IF(32, missingOpen.succeeded() || !missingOpen.error || missing.leased);
  Image corrupt; corrupt.primary = CheckpointBytes(96, 0); auto corruptOpen = Access::open(std::make_unique<Fake>(corrupt));
  OPENHDK_FAIL_IF(33, corruptOpen.succeeded() || !corruptOpen.error->store || corrupt.leased);
  Image exists; exists.primary = encode(projection()); const auto existsWire = *exists.primary;
  auto existsCreate = Access::create(owner(projection()), std::make_unique<Fake>(exists));
  OPENHDK_FAIL_IF(34, existsCreate.succeeded() || existsCreate.error->code != DurableServiceErrorCode::BaselineMismatch || *exists.primary != existsWire || exists.leased);
  auto badLimits = CatalogCheckpointLimits{}; badLimits.stagedBytes = 0;
  OPENHDK_FAIL_IF(35, Access::create(owner(projection()), std::make_unique<Fake>(missing), badLimits).succeeded());
  OPENHDK_FAIL_IF(36, Access::open(std::make_unique<Fake>(exists), {}, SIZE_MAX).succeeded() || exists.leased);
  Image createUncertain; auto cu = std::make_unique<Fake>(createUncertain); cu->outcome = StorePublication::Uncertain; cu->uncertainNew = true;
  auto factoryFailure = Access::create(owner(projection()), std::move(cu));
  OPENHDK_FAIL_IF(37, factoryFailure.succeeded() || !factoryFailure.error->store || factoryFailure.error->store->code != StoreErrorCode::CommitUncertain
      || !factoryFailure.error->candidate || createUncertain.leased || !createUncertain.primary);
  Image deniedRecovery; deniedRecovery.primary = encode(projection()); auto denied = std::make_unique<Fake>(deniedRecovery); denied->fail = StoreOperation::Reconcile;
  OPENHDK_FAIL_IF(38, Access::open(std::move(denied)).succeeded() || deniedRecovery.leased || protocolOld->songs[0].overrides->title->bytes() != "Old");
  auto exhausted = projection(); exhausted.sequence = UINT64_MAX; exhausted.catalogRevision = UINT64_MAX;
  exhausted.nextRoot = UINT64_MAX; exhausted.nextSong = UINT64_MAX; Image maxImage; maxImage.primary = encode(exhausted);
  auto maxService = Access::open(std::make_unique<Fake>(maxImage)); const auto maxSnapshot = maxService.service->snapshot();
  auto maxNoop = maxService.service->replaceUserOverrides(maxSnapshot->songs[0].id, {"Old", "Artist"});
  OPENHDK_FAIL_IF(39, !maxNoop.succeeded() || maxNoop.snapshot != maxSnapshot || maxNoop.receipt->token.sequence != UINT64_MAX);
  OPENHDK_FAIL_IF(40, !failed(maxService.service->replaceUserOverrides(maxSnapshot->songs[0].id, {"Changed", {}}), DurableServiceErrorCode::MetadataFailure)
      || maxService.service->snapshot() != maxSnapshot);
  // Find exact service coexistence boundary independently of its implementation.
  std::size_t boundary = 0;
  for (std::size_t limit = 96; limit < 2048; ++limit) {
    Image b; auto limits = CatalogCheckpointLimits{}; limits.stagedBytes = limit;
    auto attempt = Access::create(owner(projection()), std::make_unique<Fake>(b), limits);
    if (attempt.succeeded()) { boundary = limit; break; }
    OPENHDK_FAIL_IF(41, b.primary || b.leased);
  }
  OPENHDK_FAIL_IF(42, boundary == 0);
  auto atLimit = CatalogCheckpointLimits{}; atLimit.stagedBytes = boundary; Image atImage;
  auto at = Access::create(owner(projection()), std::make_unique<Fake>(atImage), atLimit);
  OPENHDK_FAIL_IF(43, !at.succeeded());
  Image belowImage; --atLimit.stagedBytes;
  OPENHDK_FAIL_IF(44, Access::create(owner(projection()), std::make_unique<Fake>(belowImage), atLimit).succeeded() || belowImage.primary);
  Image accountedImage; ++atLimit.stagedBytes;
  OPENHDK_FAIL_IF(45, Access::create(owner(projection()), std::make_unique<Fake>(accountedImage), atLimit, 1).succeeded() || accountedImage.primary);
  before = service.snapshot();
  OPENHDK_FAIL_IF(46, !failed(service.replaceUserOverrides(id, {"Budget", {}}, {}, SIZE_MAX), DurableServiceErrorCode::CheckpointFailure)
      || service.snapshot() != before);
  auto held = service.snapshot(); auto retainedResult = noAlloc.snapshot;
  OPENHDK_FAIL_IF(47, service.close().has_value() || service.state() != DurableServiceState::Closed || image.leased);
  OPENHDK_FAIL_IF(48, !failed(service.replaceUserOverrides(id, {"Closed", {}}), DurableServiceErrorCode::InvalidConfiguration));
  created.service.reset();
  OPENHDK_FAIL_IF(49, held->songs[0].overrides->title->bytes() != "Sweep" || retainedResult->songs[0].overrides->title->bytes() != "No allocation"
      || historical->songs[0].overrides->title->bytes() != "Old");
  Image seqImage; auto seqProjection = projection(); seqProjection.sequence = UINT64_MAX;
  seqImage.primary = encode(seqProjection); auto seqService = Access::open(std::make_unique<Fake>(seqImage));
  const auto seqOld = seqService.service->snapshot();
  auto seqFail = seqService.service->replaceUserOverrides(seqOld->songs[0].id, {"New", {}});
  OPENHDK_FAIL_IF(50, !failed(seqFail, DurableServiceErrorCode::StoreFailure) || seqFail.error->store->code != StoreErrorCode::CounterExhausted
      || seqService.service->snapshot() != seqOld || seqService.service->state() != DurableServiceState::Ready);
  std::size_t factoryThrows = 0; bool factoryPassed = false;
  for (std::ptrdiff_t count = 0; count < 256; ++count) {
    Image target; auto input = owner(projection()); auto p = std::make_unique<Fake>(target);
    failAfter = count; auto result = Access::create(std::move(input), std::move(p)); failAfter = -1;
    if (result.succeeded()) { factoryPassed = true; break; }
    ++factoryThrows;
    OPENHDK_FAIL_IF(51, !result.error || result.receipt || target.primary || target.leased);
  }
  OPENHDK_FAIL_IF(52, !factoryPassed || factoryThrows < 10);
  Image createBan; auto banned = std::make_unique<Fake>(createBan); banned->banAllocations = true;
  auto bannedCreate = Access::create(owner(projection()), std::move(banned)); failAfter = -1;
  OPENHDK_FAIL_IF(53, !bannedCreate.succeeded() || !bannedCreate.receipt || bannedCreate.service->state() != DurableServiceState::Ready);
  Image updateBoundaryImage; std::size_t updateBoundary = 0;
  for (std::size_t limit = boundary; limit < 4096; ++limit) {
    Image target; auto limits = CatalogCheckpointLimits{}; limits.stagedBytes = limit;
    auto attempt = Access::create(owner(projection()), std::make_unique<Fake>(target), limits);
    if (!attempt.succeeded()) continue;
    const auto old = attempt.service->snapshot(); const auto oldWire = *target.primary;
    auto changed = attempt.service->replaceUserOverrides(old->songs[0].id, {"New title", "New artist"});
    if (changed.succeeded()) { updateBoundary = limit; break; }
    OPENHDK_FAIL_IF(54, !changed.error || target.primary != oldWire || attempt.service->snapshot() != old
        || attempt.service->state() != DurableServiceState::Ready);
  }
  OPENHDK_FAIL_IF(55, updateBoundary <= boundary);
  auto updateLimits = CatalogCheckpointLimits{}; updateLimits.stagedBytes = updateBoundary;
  auto ub = Access::create(owner(projection()), std::make_unique<Fake>(updateBoundaryImage), updateLimits);
  OPENHDK_FAIL_IF(56, !ub.succeeded() || !ub.service->replaceUserOverrides(ub.service->snapshot()->songs[0].id, {"New title", "New artist"}).succeeded());
  Image over; auto ob = Access::create(owner(projection()), std::make_unique<Fake>(over), updateLimits);
  const auto obOld = ob.service->snapshot(); const auto obWire = *over.primary;
  OPENHDK_FAIL_IF(57, ob.service->replaceUserOverrides(obOld->songs[0].id, {"New title", "New artist"}, {}, 1).succeeded()
      || ob.service->snapshot() != obOld || *over.primary != obWire);
  Image shortText; auto textLimits = CatalogCheckpointLimits{}; textLimits.textBytes = 32;
  auto text = Access::create(owner(projection()), std::make_unique<Fake>(shortText), textLimits);
  const std::string longText(33, 'x');
  OPENHDK_FAIL_IF(58, !text.succeeded() || !failed(text.service->replaceUserOverrides(text.service->snapshot()->songs[0].id, {longText, {}}), DurableServiceErrorCode::MetadataFailure));
  Image contended; auto firstOwner = Access::create(owner(projection()), std::make_unique<Fake>(contended));
  auto contender = Access::open(std::make_unique<Fake>(contended));
  OPENHDK_FAIL_IF(59, !firstOwner.succeeded() || contender.succeeded() || contender.error->store->code != StoreErrorCode::Busy || !contended.leased);
  OPENHDK_FAIL_IF(60, Access::create(nullptr, std::make_unique<Fake>(missing)).succeeded()
      || Access::open(nullptr).succeeded());
  // Prepared source buffers remain owned. Override publication preserves Ready
  // validation/source tokens and lineage; restore remains a fresh Invalid owner.
  const auto rootPath = std::filesystem::temp_directory_path() /
      ("openhdk-durable-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  if (!std::filesystem::create_directory(rootPath)) throw std::runtime_error("fixture directory failed");
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
  } cleanup{rootPath};
  const std::vector<std::uint8_t> midi{'M','T','h','d',0,0,0,6,0,0,0,1,0,3,'M','T','r','k',0,0,0,9,0,0xff,5,1,'A',0,0xff,0x2f,0};
  { std::ofstream out(rootPath / "ready.kar", std::ios::binary);
    out.write(reinterpret_cast<const char*>(midi.data()), static_cast<std::streamsize>(midi.size())); }
  auto readyOwner = std::make_unique<SongDiscovery>(); auto root = readyOwner->registerRoot(rootPath);
  OPENHDK_FAIL_IF(61, !root.root || !readyOwner->scan(*root.root).succeeded());
  auto readySnapshot = readyOwner->snapshot(); auto readyId = readySnapshot->songs[0].id;
  auto prepared = readyOwner->prepare(readySnapshot, readyId);
  OPENHDK_FAIL_IF(62, !prepared.succeeded());
  Image readyImage; auto readyService = Access::create(std::move(readyOwner), std::make_unique<Fake>(readyImage));
  auto readyEdit = readyService.service->replaceUserOverrides(readyId, {"User title", {}});
  OPENHDK_FAIL_IF(63, !readyEdit.succeeded() || readyEdit.snapshot->songs[0].state != CatalogState::Ready
      || readyEdit.snapshot->songs[0].sourceRevision() != readySnapshot->songs[0].sourceRevision()
      || readyEdit.snapshot->songs[0].metadata != readySnapshot->songs[0].metadata
      || prepared.prepared->song().overrides);
  readyService.service.reset();
  OPENHDK_FAIL_IF(64, prepared.prepared->lyrics()->cues().size() != 1 || readyEdit.snapshot->songs[0].overrides->title->bytes() != "User title");


  // Private owner fence also rejects in the independent fake namespace.
  Image fenceImage;auto fp=std::make_unique<Fake>(fenceImage);auto* fraw=fp.get();
  auto fence=Access::create(owner(projection()),std::move(fp));
  OPENHDK_FAIL_IF(65,!fence.succeeded());
  auto fenceOld=fence.service->snapshot();const auto fenceWire=*fenceImage.primary;const auto fencePublications=fraw->publications;
  StoreControl fenceControl;fenceControl.checkpoint=[&](StoreCheckpoint point){if(point==StoreCheckpoint::BeforePublication)Access::failAdmission(*fence.service,true);};
  auto rejectedFence=fence.service->replaceUserOverrides(fenceOld->songs[0].id,{"Fence rejects",{}},fenceControl);
  OPENHDK_FAIL_IF(66,rejectedFence.succeeded() || !rejectedFence.error->binding || rejectedFence.error->store->outcome!=StoreOutcome::NotCommitted
      || *fenceImage.primary!=fenceWire || fraw->publications!=fencePublications || fence.service->snapshot()!=fenceOld);
  Image noopImage;auto noopFence=Access::create(owner(projection()),std::make_unique<Fake>(noopImage));
  auto noopOld=noopFence.service->snapshot();const auto noopWire=*noopImage.primary;
  StoreControl noopControl;noopControl.cancelled=[&]{Access::failAdmission(*noopFence.service,true);return false;};
  auto noopRejected=noopFence.service->replaceUserOverrides(noopOld->songs[0].id,{"Old","Artist"},noopControl);
  OPENHDK_FAIL_IF(67,noopRejected.succeeded() || !noopRejected.error->binding || *noopImage.primary!=noopWire || noopFence.service->snapshot()!=noopOld);
  OPENHDK_FAIL_IF(68,noopFence.service->state()!=DurableServiceState::RecoveryRequired);
}
