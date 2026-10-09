// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/CatalogCheckpointCapture.hpp"
#include "library/CatalogCheckpointRestore.hpp"
#include "library/CatalogCheckpointStore.hpp"

namespace OpenHDK {
enum class DurableServiceState { Unbound, Ready, RecoveryRequired, Closed };
enum class DurableServiceOperation { Create, Open, ReplaceOverrides, Close };
enum class DurableServiceErrorCode {
  InvalidConfiguration, Busy, RecoveryRequired, MetadataFailure, CheckpointFailure,
  StoreFailure, StorageFailure, BaselineMismatch, ProtocolFault
};
struct DurableServiceError {
  DurableServiceErrorCode code;
  DurableServiceOperation operation;
  std::optional<CatalogMetadataError> metadata;
  std::optional<CheckpointError> checkpoint;
  std::optional<StoreError> store;
  std::optional<StoreProviderError> cleanupWarning;
  std::optional<StoreArtifact> candidate, prior;
};
enum class DurableOverrideStatus { Updated, NoChange };
struct DurableCommitReceipt {
  StoreCommitToken token;
  std::uint64_t capturedRevision;
  std::optional<StoreProviderError> cleanupWarning;
};
struct DurableOverrideResult {
  std::optional<DurableOverrideStatus> status;
  std::shared_ptr<const CatalogSnapshot> snapshot;
  std::optional<DurableCommitReceipt> receipt;
  std::optional<DurableServiceError> error;
  bool succeeded() const noexcept { return status && snapshot && receipt && !error; }
};
class DurableLibraryService;
struct DurableServiceOpenResult {
  std::unique_ptr<DurableLibraryService> service;
  std::optional<DurableCommitReceipt> receipt; // Create receipt; open is reconciliation, not a new save.
  std::optional<DurableServiceError> error;
  bool succeeded() const noexcept { return service != nullptr && !error; }
};
#ifdef OPENHDK_ENABLE_TEST_SEAMS
// In-memory test provider category only. Native providers cannot be passed to
// these factories. No production factory exists until native binding admission.
class DurableLibraryTestProvider : public CheckpointStoreProvider {};
struct DurableLibraryServiceTestAccess;
#endif

class DurableLibraryService {
 public:
  DurableLibraryService(const DurableLibraryService&) = delete;
  DurableLibraryService& operator=(const DurableLibraryService&) = delete;
  // Queries use the serialized control path. Only returned snapshots may be
  // shared across threads. There is no mutable owner/store/token injection API.
  DurableServiceState state() const noexcept { return state_; }
  std::shared_ptr<const CatalogSnapshot> snapshot() const noexcept { return owner_->snapshot(); }
  DurableOverrideResult replaceUserOverrides(SongId id, CatalogOverrideRequest request,
      const StoreControl& control = {}, std::size_t alreadyOwned = 0U) {
    constexpr auto op = DurableServiceOperation::ReplaceOverrides;
    Guard guard(busy_);
    if (!guard.held) return failed(error(DurableServiceErrorCode::Busy, op));
    if (state_ != DurableServiceState::Ready)
      return failed(error(state_ == DurableServiceState::RecoveryRequired
          ? DurableServiceErrorCode::RecoveryRequired : DurableServiceErrorCode::InvalidConfiguration, op));
    try {
      if (!baselineMatches()) {
        state_ = DurableServiceState::RecoveryRequired;
        return failed(error(DurableServiceErrorCode::BaselineMismatch, op));
      }
      // Reuse the catalog transaction in a private same-lineage staging view.
      // Root maps/counters stay with the unchanged baseline; no live publication.
      auto stagingBytes = externalBytes(alreadyOwned, true);
      SongCatalog staged(owner_->catalog_.snapshot_, owner_->catalog_.nextRoot_, owner_->catalog_.nextSong_);
      auto metadata = staged.replaceUserOverrides(id, request,
          {limits_.textBytes, limits_.stagedBytes}, stagingBytes);
      if (!metadata.succeeded()) {
        auto e = error(DurableServiceErrorCode::MetadataFailure, op); e.metadata = metadata.error;
        return failed(e);
      }
      const bool changed = metadata.status == CatalogOverrideStatus::Updated;
      const auto next = staged.snapshot();
      const auto candidate = changed ? project(next, alreadyOwned) : baseline_;
      const auto retained = retainedBytes(alreadyOwned, next, changed);
      // All result storage exists before save can publish. Fixed receipt fields
      // and shared_ptr/expectation moves do not allocate after acknowledgment.
      DurableOverrideResult result{changed ? DurableOverrideStatus::Updated : DurableOverrideStatus::NoChange,
          next, {}, {}};
      auto saved = store_.save(candidate->projection(), *expectation_, control, retained);
#ifdef OPENHDK_ENABLE_TEST_SEAMS
      if (corruptAcknowledgment_ && saved.succeeded()) saved.capturedRevision.reset();
#endif
      if (!saved.succeeded()) return saveFailure(saved, op);
      if (saved.status != (changed ? StoreSaveStatus::Saved : StoreSaveStatus::Unchanged)
          || saved.capturedRevision != next->revision || !saved.expectation->token()) {
        state_ = DurableServiceState::RecoveryRequired;
        auto e = error(DurableServiceErrorCode::ProtocolFault, op);
        e.cleanupWarning = saved.cleanupWarning; e.candidate = saved.candidate; e.prior = saved.prior;
        return failed(e); // Disk outcome is not described as rolled back.
      }
      result.receipt = DurableCommitReceipt{*saved.expectation->token(), *saved.capturedRevision, saved.cleanupWarning};
      if (changed) {
        auto publication = next;
        owner_->catalog_.snapshot_.swap(publication);
        baseline_ = candidate;
      }
      expectation_ = std::move(saved.expectation);
      return result;
    } catch (const CheckpointDetail::Failure& e) {
      auto failure = error(DurableServiceErrorCode::CheckpointFailure, op);
      failure.checkpoint = CheckpointError{e.code, CheckpointOperation::Capture, e.field, e.record, e.offset};
      return failed(failure);
    } catch (const std::bad_alloc&) { return failed(error(DurableServiceErrorCode::StorageFailure, op)); }
  }
  std::optional<DurableServiceError> close() noexcept {
    Guard guard(busy_);
    if (!guard.held) return error(DurableServiceErrorCode::Busy, DurableServiceOperation::Close);
    if (auto e = store_.close()) {
      auto failure = error(DurableServiceErrorCode::StoreFailure, DurableServiceOperation::Close);
      failure.store = e; return failure;
    }
    state_ = DurableServiceState::Closed; return {};
    // No reopen-in-place: explicit recovery constructs a fresh service/lineage.
  }
 private:
#ifdef OPENHDK_ENABLE_TEST_SEAMS
  friend struct DurableLibraryServiceTestAccess;
  bool corruptAcknowledgment_ = false;
#endif
  struct Guard {
    std::atomic_flag& flag; bool held;
    explicit Guard(std::atomic_flag& f) noexcept : flag(f), held(!f.test_and_set(std::memory_order_acquire)) {}
    ~Guard() { if (held) flag.clear(std::memory_order_release); }
  };
  DurableLibraryService(std::unique_ptr<SongDiscovery> owner,
      std::unique_ptr<CheckpointStoreProvider> provider, CatalogCheckpointLimits limits)
      : provider_(std::move(provider)), owner_(std::move(owner)), store_(*provider_), limits_(limits) {}
  static DurableServiceError error(DurableServiceErrorCode code, DurableServiceOperation op) noexcept {
    return {code, op, {}, {}, {}, {}, {}, {}};
  }
  static DurableOverrideResult failed(DurableServiceError e) noexcept { return {{}, nullptr, {}, e}; }
  DurableOverrideResult saveFailure(const StoreSaveResult& saved, DurableServiceOperation op) noexcept {
    if (!saved.error || saved.error->code == StoreErrorCode::CommitUncertain
        || saved.error->code == StoreErrorCode::StaleCheckpoint)
      state_ = DurableServiceState::RecoveryRequired;
    auto e = error(saved.error ? DurableServiceErrorCode::StoreFailure : DurableServiceErrorCode::ProtocolFault, op);
    e.store = saved.error; e.cleanupWarning = saved.cleanupWarning; e.candidate = saved.candidate; e.prior = saved.prior;
    return failed(e);
  }
  bool baselineMatches() const noexcept {
    return baseline_ && expectation_ && owner_->snapshot() == baseline_->snapshot()
        && owner_->catalog_.nextRoot_ == baseline_->projection().nextRoot
        && owner_->catalog_.nextSong_ == baseline_->projection().nextSong;
  }
  std::size_t pathBytes() const {
    std::size_t total = 0U;
    for (const auto& root : owner_->roots_) {
      const auto units = root.path.native().size();
      constexpr auto unitBytes = sizeof(std::filesystem::path::value_type);
      if (units > limits_.stagedBytes / unitBytes)
        CheckpointDetail::fail(CheckpointErrorCode::LimitExceeded, CheckpointField::Hint);
      CheckpointDetail::add(total, units * unitBytes, limits_.stagedBytes);
      if (root.savedHint) CheckpointDetail::add(total, root.savedHint->size(), limits_.stagedBytes);
    }
    return total;
  }
  std::size_t projectionBytes() const { return baseline_ ? CheckpointDetail::shape(baseline_->projection(), limits_).strings : 0U; }
  std::size_t externalBytes(std::size_t caller, bool provider) const {
    auto n = caller;
    CheckpointDetail::add(n, projectionBytes(), limits_.stagedBytes);
    CheckpointDetail::add(n, pathBytes(), limits_.stagedBytes);
    if (provider) CheckpointDetail::add(n, provider_->retainedBytes(), limits_.stagedBytes);
    return n;
  }
  // Store input projection is charged by the coordinator. Old baseline strings
  // count here only for an actual change; no-op/create use the same input object.
  std::size_t retainedBytes(std::size_t caller, std::shared_ptr<const CatalogSnapshot> staged,
      bool oldProjection) const {
    auto available = MetadataPayloadBudget::create(limits_.stagedBytes, caller);
    if (!available) CheckpointDetail::fail(CheckpointErrorCode::LimitExceeded, CheckpointField::None);
    auto budget = *available;
    const auto charge = [&](std::size_t n) {
      if (!budget.charge(n)) CheckpointDetail::fail(CheckpointErrorCode::LimitExceeded, CheckpointField::None);
    };
    charge(pathBytes()); if (oldProjection) charge(projectionBytes());
    std::set<const CatalogSourceMetadata*> metadata;
    std::set<const CatalogUserOverrides*> overrides;
    const auto snapshotCharge = [&](const CatalogSnapshot& snapshot) {
      for (const auto& song : snapshot.songs) {
        charge(song.locator().size());
        if (!chargeSourceMetadata(budget, song.metadata, metadata)
            || !chargeUserOverrides(budget, song.overrides, overrides))
          CheckpointDetail::fail(CheckpointErrorCode::LimitExceeded, CheckpointField::None);
      }
    };
    const auto current = owner_->snapshot(); snapshotCharge(*current);
    if (staged != current) snapshotCharge(*staged);
    return budget.used();
  }
  std::shared_ptr<const CapturedCatalogCheckpoint> project(
      std::shared_ptr<const CatalogSnapshot> staged, std::size_t caller) const {
    using namespace CheckpointDetail;
    const auto& old = baseline_->projection();
    if (!std::equal(staged->roots.begin(), staged->roots.end(),
        owner_->snapshot()->roots.begin(), owner_->snapshot()->roots.end(),
        [](const auto& a, const auto& b) { return a.id == b.id
            && a.attachmentGeneration == b.attachmentGeneration && a.policy == b.policy; })
        || staged->songs.size() != old.songs.size())
      fail(CheckpointErrorCode::InvalidCheckpoint, CheckpointField::Generation);
    auto n = retainedBytes(caller, staged, true);
    add(n, provider_->retainedBytes(), limits_.stagedBytes);
    std::size_t largest = 0U;
    const auto output = [&](std::size_t size) {
      if (size > limits_.textBytes) fail(CheckpointErrorCode::LimitExceeded, CheckpointField::None);
      add(n, size, limits_.stagedBytes); largest = std::max(largest, size);
    };
    for (const auto& root : old.roots) if (root.hint) output(root.hint->size());
    for (const auto& song : staged->songs) {
      output(song.locator().size());
      if (song.overrides) {
        if (song.overrides->title) output(song.overrides->title->bytes().size());
        if (song.overrides->artist) output(song.overrides->artist->bytes().size());
      }
    }
    add(n, largest, limits_.stagedBytes); add(n, largest, limits_.stagedBytes);
    CatalogCheckpointProjection p;
    p.catalogRevision = staged->revision; p.nextRoot = old.nextRoot; p.nextSong = old.nextSong;
    p.roots = old.roots; p.songs.reserve(staged->songs.size());
    for (std::size_t i = 0U; i < staged->songs.size(); ++i) {
      const auto& song = staged->songs[i];
      // Override-only staging retains each source key and identity in row order.
      const auto& previous = owner_->snapshot()->songs[i];
      if (song.id != previous.id || song.root != previous.root || song.locator() != previous.locator())
        fail(CheckpointErrorCode::InvalidCheckpoint, CheckpointField::Id);
      CheckpointSong row{old.songs[i].id, old.songs[i].root, old.songs[i].role, song.locator(), {}, {}};
      if (song.overrides) {
        if (song.overrides->title) row.title = song.overrides->title->bytes();
        if (song.overrides->artist) row.artist = song.overrides->artist->bytes();
      }
      p.songs.push_back(std::move(row));
    }
    (void)shape(p, limits_); validate(p, limits_);
    return std::shared_ptr<const CapturedCatalogCheckpoint>(new CapturedCatalogCheckpoint(std::move(staged), std::move(p)));
  }
  static DurableServiceOpenResult initialize(std::unique_ptr<SongDiscovery> owner,
      std::unique_ptr<CheckpointStoreProvider> provider, bool create, CatalogCheckpointLimits limits,
      std::size_t alreadyOwned) {
    const auto op = create ? DurableServiceOperation::Create : DurableServiceOperation::Open;
    if (!provider || (create && !owner) || !CheckpointDetail::validLimits(limits) || alreadyOwned > limits.stagedBytes)
      return {nullptr, {}, error(DurableServiceErrorCode::InvalidConfiguration, op)};
    try {
      auto service = std::unique_ptr<DurableLibraryService>(new DurableLibraryService(std::move(owner), std::move(provider), limits));
      auto opened = service->store_.open(limits, create
          ? service->retainedBytes(alreadyOwned, service->owner_->snapshot(), false) : alreadyOwned);
      if (!opened.succeeded()) {
        auto e = error(DurableServiceErrorCode::StoreFailure, op); e.store = opened.error;
        return {nullptr, {}, e};
      }
      if (create && opened.projection) return {nullptr, {}, error(DurableServiceErrorCode::BaselineMismatch, op)};
      if (!create && !opened.projection) {
        auto e = error(DurableServiceErrorCode::StoreFailure, op);
        e.store = StoreError{StoreErrorCode::NotFound, StoreOperation::Read, StoreOutcome::NotCommitted, 0, {}};
        return {nullptr, {}, e};
      }
      auto retained = alreadyOwned;
      CheckpointDetail::add(retained, service->provider_->retainedBytes(), limits.stagedBytes);
      if (!create) {
        auto restored = CatalogCheckpointRestore::fromProjection(*opened.projection, limits, retained);
        if (!restored.succeeded()) {
          auto e = error(DurableServiceErrorCode::CheckpointFailure, op); e.checkpoint = restored.error;
          return {nullptr, {}, e};
        }
        service->owner_ = std::move(restored.owner);
        CheckpointDetail::add(retained, CheckpointDetail::shape(*opened.projection, limits).strings, limits.stagedBytes);
      }
      const auto captured = CatalogCheckpointCapture::acquire(*service->owner_, limits, retained);
      if (!captured.succeeded()) {
        auto e = error(DurableServiceErrorCode::CheckpointFailure, op); e.checkpoint = captured.error;
        return {nullptr, {}, e};
      }
      if (!create && !StoreDetail::equalProjection(captured.captured->projection(), *opened.projection))
        return {nullptr, {}, error(DurableServiceErrorCode::BaselineMismatch, op)};
      opened.projection.reset(); // No retained decoded strings at initial save.
      service->baseline_ = captured.captured; service->expectation_ = std::move(opened.expectation);
      DurableServiceOpenResult result{std::move(service), {}, {}};
      if (create) {
        auto& target = *result.service;
        const auto bytes = target.retainedBytes(alreadyOwned, target.snapshot(), false);
        auto saved = target.store_.save(target.baseline_->projection(), *target.expectation_, {}, bytes);
        if (!saved.succeeded()) return {nullptr, {}, target.saveFailure(saved, op).error};
        if (saved.status != StoreSaveStatus::Saved || saved.capturedRevision != target.snapshot()->revision
            || !saved.expectation->token()) return {nullptr, {}, error(DurableServiceErrorCode::ProtocolFault, op)};
        result.receipt = DurableCommitReceipt{*saved.expectation->token(), *saved.capturedRevision, saved.cleanupWarning};
        target.expectation_ = std::move(saved.expectation);
      }
      result.service->state_ = DurableServiceState::Ready; return result;
    } catch (const CheckpointDetail::Failure& e) {
      auto failure = error(DurableServiceErrorCode::CheckpointFailure, op);
      failure.checkpoint = CheckpointError{e.code, CheckpointOperation::Capture, e.field, e.record, e.offset};
      return {nullptr, {}, failure};
    } catch (const std::bad_alloc&) { return {nullptr, {}, error(DurableServiceErrorCode::StorageFailure, op)}; }
  }
  // Field order guarantees store destruction/releases before provider destruction.
  std::unique_ptr<CheckpointStoreProvider> provider_;
  std::unique_ptr<SongDiscovery> owner_;
  CatalogCheckpointStore store_;
  CatalogCheckpointLimits limits_;
  std::shared_ptr<const CapturedCatalogCheckpoint> baseline_;
  std::optional<StoreExpectation> expectation_;
  DurableServiceState state_ = DurableServiceState::Unbound;
  std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
  static_assert(std::is_nothrow_swappable_v<std::shared_ptr<const CatalogSnapshot>>);
  static_assert(std::is_nothrow_move_constructible_v<DurableOverrideResult>);
  static_assert(std::is_nothrow_move_constructible_v<DurableServiceOpenResult>);
  static_assert(std::is_nothrow_move_assignable_v<decltype(expectation_)>);
  static_assert(std::is_nothrow_copy_assignable_v<decltype(baseline_)>);
  static_assert(std::is_nothrow_move_assignable_v<std::optional<DurableCommitReceipt>>);
};
#ifdef OPENHDK_ENABLE_TEST_SEAMS
struct DurableLibraryServiceTestAccess {
  static DurableServiceOpenResult create(std::unique_ptr<SongDiscovery> owner,
      std::unique_ptr<DurableLibraryTestProvider> provider, CatalogCheckpointLimits limits = {}, std::size_t alreadyOwned = 0U) {
    return DurableLibraryService::initialize(std::move(owner), std::move(provider), true, limits, alreadyOwned);
  }
  static DurableServiceOpenResult open(std::unique_ptr<DurableLibraryTestProvider> provider,
      CatalogCheckpointLimits limits = {}, std::size_t alreadyOwned = 0U) {
    return DurableLibraryService::initialize(nullptr, std::move(provider), false, limits, alreadyOwned);
  }
  static void corruptAcknowledgment(DurableLibraryService& service) { service.corruptAcknowledgment_ = true; }
};
#endif
} // namespace OpenHDK
