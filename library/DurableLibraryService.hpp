// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/CatalogCheckpointCapture.hpp"
#include "library/CatalogCheckpointRestore.hpp"
#include "library/CatalogCheckpointStore.hpp"
#include "library/LinuxCheckpointProvider.hpp"

namespace OpenHDK {
enum class DurableServiceState { Unbound, Ready, RecoveryRequired, Closed };
enum class DurableServiceOperation { Create, Open, ReplaceOverrides, Close };
enum class DurableServiceErrorCode {
  InvalidConfiguration, Busy, RecoveryRequired, MetadataFailure, CheckpointFailure,
  StoreFailure, StorageFailure, BaselineMismatch, ProtocolFault, NativeAdmissionFailure
};
struct DurableServiceError {
  DurableServiceErrorCode code;
  DurableServiceOperation operation;
  std::optional<CatalogMetadataError> metadata;
  std::optional<CheckpointError> checkpoint;
  std::optional<StoreError> store;
  std::optional<StoreProviderError> cleanupWarning;
  std::optional<StoreArtifact> candidate, prior;
  std::optional<NativeBindingError> binding;
  std::optional<RootId> root;
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
// these fake factories. Experimental Linux factories use separate owning admission.
class DurableLibraryTestProvider : public CheckpointStoreProvider {};
struct DurableLibraryServiceTestAccess;
#endif

class DurableLibraryService {
 public:
  // Experimental: requires fresh revision-bound native evidence/acceptance.
  // Explicit Create transfers the owner; Open restores an unattached fresh owner.
  static DurableServiceOpenResult createLinux(std::unique_ptr<SongDiscovery> owner,
      std::string parent,std::string primary,CatalogCheckpointLimits limits={},
      NativeStoreAdmissionLimits admission={},std::size_t alreadyOwned=0) {
    return nativeFactory(std::move(owner),std::move(parent),std::move(primary),true,limits,admission,alreadyOwned,false);
  }
  static DurableServiceOpenResult openLinux(std::string parent,std::string primary,
      CatalogCheckpointLimits limits={},NativeStoreAdmissionLimits admission={},std::size_t alreadyOwned=0) {
    return nativeFactory(nullptr,std::move(parent),std::move(primary),false,limits,admission,alreadyOwned,false);
  }
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
      lastAdmissionError_.reset();
      if (auto e=admissionCheck(this)) {
        (void)e;state_=DurableServiceState::RecoveryRequired;return failed(admissionError(op));
      }
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
    nativeAdmission_.reset();
    state_ = DurableServiceState::Closed; return {};
    // No reopen-in-place: explicit recovery constructs a fresh service/lineage.
  }
 private:
#ifdef OPENHDK_ENABLE_TEST_SEAMS
  friend struct DurableLibraryServiceTestAccess;
  bool corruptAcknowledgment_ = false;
  bool forcedAdmissionFailure_ = false;
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
    return {code, op, {}, {}, {}, {}, {}, {}, {}, {}};
  }
  static DurableOverrideResult failed(DurableServiceError e) noexcept { return {{}, nullptr, {}, e}; }
  DurableOverrideResult saveFailure(const StoreSaveResult& saved, DurableServiceOperation op) noexcept {
    if (lastAdmissionError_) {
      state_=DurableServiceState::RecoveryRequired;auto e=admissionError(op);
      e.store=saved.error;e.cleanupWarning=saved.cleanupWarning;e.candidate=saved.candidate;e.prior=saved.prior;return failed(e);
    }
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
    CheckpointDetail::add(total,admissionExtraBytes(),limits_.stagedBytes);
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
      std::size_t alreadyOwned, bool native=false,NativeStoreAdmissionLimits admission={}) {
    const auto op = create ? DurableServiceOperation::Create : DurableServiceOperation::Open;
    if (!provider || (create && !owner) || !CheckpointDetail::validLimits(limits) || alreadyOwned > limits.stagedBytes)
      return {nullptr, {}, error(DurableServiceErrorCode::InvalidConfiguration, op)};
    try {
      auto service = std::unique_ptr<DurableLibraryService>(new DurableLibraryService(std::move(owner), std::move(provider), limits));
      if(native)service->nativeProvider_=static_cast<LinuxCheckpointProvider*>(service->provider_.get());
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
      if(native) {
        if(auto e=service->prepareAdmission(admission,op,alreadyOwned,opened.projection?CheckpointDetail::shape(*opened.projection,limits).strings:0U))
          return {nullptr,{},*e};
        retained=alreadyOwned;
        CheckpointDetail::add(retained,service->provider_->retainedBytes(),limits.stagedBytes);
        CheckpointDetail::add(retained,service->admissionExtraBytes(),limits.stagedBytes);
        if(opened.projection)CheckpointDetail::add(retained,CheckpointDetail::shape(*opened.projection,limits).strings,limits.stagedBytes);
        service->installAdmissionFence();
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
  void installAdmissionFence() noexcept {store_.setAdmissionFence(this,admissionCheck);}
  std::size_t admissionExtraBytes() const noexcept {
    return nativeAdmission_ ? nativeAdmission_->retainedBytes()-nativeAdmission_->binding_->retainedBytes() : 0U;
  }
  DurableServiceError admissionError(DurableServiceOperation op) const noexcept {
    auto e=error(DurableServiceErrorCode::NativeAdmissionFailure,op);e.binding=lastAdmissionError_;
    if(e.binding && e.binding->rootIndex && *e.binding->rootIndex<admissionRoots_.size())
      e.root=admissionRoots_[*e.binding->rootIndex];
    return e;
  }
  static std::optional<StoreProviderError> admissionCheck(void* context) noexcept {
    auto& s=*static_cast<DurableLibraryService*>(context);
#ifdef OPENHDK_ENABLE_TEST_SEAMS
    if(s.forcedAdmissionFailure_) {
      s.lastAdmissionError_=NativeBindingError{NativeBindingErrorCode::SourceChanged,NativeBindingOperation::Recheck,{},{}};
      return StoreProviderError{StoreErrorCode::SourceChanged,0};
    }
#endif
    if(!s.nativeAdmission_)return {};
    s.lastAdmissionError_=s.nativeAdmission_->recheck(s.nativeProvider_->lease_);
    if(!s.lastAdmissionError_)return {};
    return StoreProviderError{StoreErrorCode::SourceChanged,s.lastAdmissionError_->providerError?
        s.lastAdmissionError_->providerError->nativeError:0};
  }
  std::optional<DurableServiceError> prepareAdmission(NativeStoreAdmissionLimits limits,DurableServiceOperation op,
      std::size_t caller,std::size_t decodedStrings) {
#ifdef __linux__
    std::array<std::string_view,32> paths{};std::size_t count=0;
    for(const auto& root:owner_->roots_)if(root.attached) {
      if(count>=limits.activeRoots || count>=paths.size()) {
        lastAdmissionError_=NativeBindingError{NativeBindingErrorCode::LimitExceeded,NativeBindingOperation::Admit,{},{}};
        return admissionError(op);
      }
      paths[count]=root.path.native();admissionRoots_[count]=root.id;++count;
    }
    auto retained=retainedBytes(caller,owner_->snapshot(),false);
    CheckpointDetail::add(retained,provider_->retainedBytes(),limits_.stagedBytes);
    CheckpointDetail::add(retained,decodedStrings,limits_.stagedBytes);
    limits.stagedBytes=std::min(limits.stagedBytes,limits_.stagedBytes);
    auto binding=NativeStoreAdmission::exportBinding(nativeProvider_->lease_,limits,retained);
    if(binding.error){lastAdmissionError_=binding.error;return admissionError(op);}
    auto admitted=NativeStoreAdmission::admit(nativeProvider_->lease_,std::move(binding.binding),
        std::span<const std::string_view>(paths).first(count),limits,retained);
    if(admitted.error){lastAdmissionError_=admitted.error;return admissionError(op);}
    nativeAdmission_=std::move(admitted.admission);return {};
#else
    (void)limits;(void)caller;(void)decodedStrings;return error(DurableServiceErrorCode::InvalidConfiguration,op);
#endif
  }
  static DurableServiceOpenResult nativeFactory(std::unique_ptr<SongDiscovery> owner,
      std::string parent,std::string primary,bool create,CatalogCheckpointLimits limits,
      NativeStoreAdmissionLimits admission,std::size_t caller,bool bypass) {
    const auto op=create?DurableServiceOperation::Create:DurableServiceOperation::Open;
    if(!CheckpointDetail::validLimits(limits) || !NativeStoreAdmission::valid(admission) || caller>limits.stagedBytes || (create && !owner))
      return {nullptr,{},error(DurableServiceErrorCode::InvalidConfiguration,op)};
    try {
      auto provider=std::make_unique<LinuxCheckpointProvider>(std::move(parent),std::move(primary));
#ifdef OPENHDK_ENABLE_TEST_SEAMS
      if(bypass)LinuxCheckpointProviderTestAccess::filesystem(*provider);
#else
      (void)bypass;
#endif
      return initialize(std::move(owner),std::move(provider),create,limits,caller,true,admission);
    } catch(const std::bad_alloc&) {return {nullptr,{},error(DurableServiceErrorCode::StorageFailure,op)};}
  }
  LinuxCheckpointProvider* nativeProvider_=nullptr; // Owned by provider_; never injected publicly.
  std::shared_ptr<const NativeStoreAdmission> nativeAdmission_;
  std::array<std::optional<RootId>,32> admissionRoots_{};
  std::optional<NativeBindingError> lastAdmissionError_;
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
  static DurableServiceOpenResult createNative(std::unique_ptr<SongDiscovery> owner,std::string parent,std::string primary,
      CatalogCheckpointLimits limits={},NativeStoreAdmissionLimits admission={},std::size_t caller=0) {
    return DurableLibraryService::nativeFactory(std::move(owner),std::move(parent),std::move(primary),true,limits,admission,caller,true);
  }
  static DurableServiceOpenResult openNative(std::string parent,std::string primary,CatalogCheckpointLimits limits={},NativeStoreAdmissionLimits admission={}) {
    return DurableLibraryService::nativeFactory(nullptr,std::move(parent),std::move(primary),false,limits,admission,0,true);
  }

  static DurableServiceOpenResult create(std::unique_ptr<SongDiscovery> owner,
      std::unique_ptr<DurableLibraryTestProvider> provider, CatalogCheckpointLimits limits = {}, std::size_t alreadyOwned = 0U) {
    return DurableLibraryService::initialize(std::move(owner), std::move(provider), true, limits, alreadyOwned);
  }
  static DurableServiceOpenResult open(std::unique_ptr<DurableLibraryTestProvider> provider,
      CatalogCheckpointLimits limits = {}, std::size_t alreadyOwned = 0U) {
    return DurableLibraryService::initialize(nullptr, std::move(provider), false, limits, alreadyOwned);
  }
  static void failAdmission(DurableLibraryService& s,bool fail) noexcept {
    s.forcedAdmissionFailure_=fail;s.installAdmissionFence();
  }
  static void nativeFault(DurableLibraryService& s,LinuxProviderFault fault) {LinuxCheckpointProviderTestAccess::fault(*s.nativeProvider_,fault);}
  static void nativeAfterPublication(DurableLibraryService& s,void (*hook)() noexcept) {LinuxCheckpointProviderTestAccess::afterPublication(*s.nativeProvider_,hook);}
  static void corruptAcknowledgment(DurableLibraryService& service) { service.corruptAcknowledgment_ = true; }
};
#endif
} // namespace OpenHDK
