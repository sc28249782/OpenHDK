// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/CatalogCheckpointCapture.hpp"
#include "library/CatalogCheckpointRestore.hpp"
#include "library/CatalogCheckpointStore.hpp"
#include "library/LinuxCheckpointProvider.hpp"

namespace OpenHDK {
enum class DurableServiceState { Unbound, Ready, RecoveryRequired, Closed };
enum class DurableServiceOperation { Create, Open, ReplaceOverrides, Close, RegisterRoot, ReattachRoot };
enum class DurableServiceErrorCode {
  InvalidConfiguration, Busy, RecoveryRequired, MetadataFailure, CheckpointFailure,
  StoreFailure, StorageFailure, BaselineMismatch, ProtocolFault, NativeAdmissionFailure, DiscoveryFailure
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
  std::optional<DiscoveryDiagnostic> discovery;
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
struct DurableRootRegistrationResult {
  std::optional<RootId> root;
  std::shared_ptr<const CatalogSnapshot> snapshot;
  std::optional<DurableCommitReceipt> receipt;
  std::optional<DurableServiceError> error;
  bool succeeded() const noexcept { return root && snapshot && receipt && !error; }
};
struct DurableRootReattachmentResult {
  std::optional<RootReattachmentStatus> status;
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
      lastAdmissionError_.reset(); lastAdmissionRoot_.reset();
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
  // Experimental root transactions: new runtime evidence/acceptance is required.
  DurableRootRegistrationResult registerRoot(const std::filesystem::path& path,
      RootSourcePolicy policy = {}, const StoreControl& control = {}, std::size_t caller = 0U) {
    return rootTransaction(path, policy, control, caller, {});
  }
  DurableRootReattachmentResult reattachRoot(RootId root, const std::filesystem::path& path,
      const StoreControl& control = {}, std::size_t caller = 0U) {
    const auto old = snapshot();
    auto r = rootTransaction(path, {}, control, caller, root);
    std::optional<RootReattachmentStatus> status;
    if (r.succeeded()) status = r.snapshot == old ? RootReattachmentStatus::Unchanged : RootReattachmentStatus::Updated;
    return {status, std::move(r.snapshot), std::move(r.receipt), std::move(r.error)};
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
  std::uint64_t admissionChecks_=0;
  bool forcedAdmissionFailure_ = false;
#endif
  DurableRootRegistrationResult rootTransaction(const std::filesystem::path& path,
      RootSourcePolicy policy, const StoreControl& control, std::size_t caller, std::optional<RootId> target) {
    const auto op = target ? DurableServiceOperation::ReattachRoot : DurableServiceOperation::RegisterRoot;
    const auto failure = [](DurableServiceError e) {
      return DurableRootRegistrationResult{{}, nullptr, {}, std::move(e)};
    };
    Guard guard(busy_);
    if (!guard.held) return failure(error(DurableServiceErrorCode::Busy, op));
    if (state_ != DurableServiceState::Ready)
      return failure(error(state_ == DurableServiceState::RecoveryRequired
          ? DurableServiceErrorCode::RecoveryRequired : DurableServiceErrorCode::InvalidConfiguration, op));
    if (!isValidRootSourcePolicy(policy))
      return failure(error(DurableServiceErrorCode::InvalidConfiguration, op));
    try {
      lastAdmissionError_.reset(); lastAdmissionRoot_.reset();
      if (!baselineMatches()) {
        state_ = DurableServiceState::RecoveryRequired;
        return failure(error(DurableServiceErrorCode::BaselineMismatch, op));
      }
      if (!nativeProvider_ && admissionCheck(this)) {
        state_ = DurableServiceState::RecoveryRequired; return failure(admissionError(op));
      }
      if (target && std::none_of(owner_->roots_.begin(), owner_->roots_.end(),
          [&](const auto& root) { return root.id == *target; })) {
        auto e = error(DurableServiceErrorCode::DiscoveryFailure, op);
        e.discovery = SongDiscovery::diagnostic(DiscoveryError::InvalidRoot, DiscoveryOperation::ReattachRoot);
        return failure(e);
      }
      const auto cancel = [&] {
        if (control.cancelled && control.cancelled())
          StoreDetail::fail(StoreErrorCode::Cancelled, StoreOperation::Save);
      };
      cancel();
      if (!target && owner_->roots_.size() >= limits_.roots)
        CheckpointDetail::fail(CheckpointErrorCode::LimitExceeded, CheckpointField::Header);
      // Preflight copied paths/hints and catalog copy before staging allocation.
      // Registration retains shared metadata/override records once; distinct
      // snapshot locator copies and root path copies each consume payload.
      auto preflight = externalBytes(caller, true);
      CheckpointDetail::add(preflight, pathBytes(), limits_.stagedBytes);
      auto budget = MetadataPayloadBudget::create(limits_.stagedBytes, preflight);
      if (!budget || !chargeCatalogPayload(*budget, *owner_->snapshot(), 2U))
        CheckpointDetail::fail(CheckpointErrorCode::LimitExceeded, CheckpointField::None);
      constexpr auto unitBytes = sizeof(std::filesystem::path::value_type);
      if (path.native().size() > limits_.textBytes / unitBytes)
        CheckpointDetail::fail(CheckpointErrorCode::LimitExceeded, CheckpointField::Hint);
      if (!budget->charge(path.native().size() * unitBytes))
        CheckpointDetail::fail(CheckpointErrorCode::LimitExceeded, CheckpointField::Hint);
      StagedOwner staged{std::unique_ptr<SongDiscovery>(new SongDiscovery(*owner_)), {}, {}, {}};
      // Retain a prospective target before discovery mutates the private owner.
      // The original admission remains owned for rollback/descriptor accounting.
      if (auto e = stageAdmission(staged, path, target, caller, op)) return failure(*e);
      std::optional<RootId> registered = target;
      bool changed = true;
      if (target) {
        RootReattachmentControl discoveryControl{control.cancelled, {}};
        auto reattached = staged.owner->reattachRoot(*target, path, discoveryControl);
        if (!reattached.succeeded()) {
          auto e = error(DurableServiceErrorCode::DiscoveryFailure, op); e.discovery = std::move(reattached.error);
          return failure(std::move(e));
        }
        changed = reattached.status == RootReattachmentStatus::Updated;
      } else {
        auto registration = staged.owner->registerRoot(path, policy);
        if (!registration.root) {
          auto e = error(DurableServiceErrorCode::DiscoveryFailure, op); e.discovery = std::move(registration.error);
          return failure(std::move(e));
        }
        registered = registration.root;
      }
      if (auto e = associateAdmission(staged, registered, target, changed, op)) return failure(*e);
      cancel();
      // Capture charges the candidate's complete owner/snapshot and fresh
      // projection. The old baseline, paths and copied old locator payload
      // coexist; shared metadata is already charged in the candidate capture.
      auto retained = externalBytes(caller, true);
      for (const auto& song : owner_->snapshot()->songs)
        CheckpointDetail::add(retained, song.locator().size(), limits_.stagedBytes);
      // Reattachment clears candidate source metadata. Retired records remain
      // owned by the old snapshot until publication/rollback, so charge them.
      auto retainedBudget=MetadataPayloadBudget::create(limits_.stagedBytes,retained);
      std::set<const CatalogSourceMetadata*> sharedMetadata;
      std::set<const CatalogUserOverrides*> sharedOverrides;
      for(const auto& song:staged.owner->snapshot()->songs) {
        if(song.metadata)sharedMetadata.insert(song.metadata.get());
        if(song.overrides)sharedOverrides.insert(song.overrides.get());
      }
      for(const auto& song:owner_->snapshot()->songs)
        if(!retainedBudget || !chargeSourceMetadata(*retainedBudget,song.metadata,sharedMetadata)
            || !chargeUserOverrides(*retainedBudget,song.overrides,sharedOverrides))
          CheckpointDetail::fail(CheckpointErrorCode::LimitExceeded,CheckpointField::None);
      retained=retainedBudget->used();
      CheckpointDetail::add(retained, candidateAdmissionBytes(staged), limits_.stagedBytes);
      auto captured = changed ? CatalogCheckpointCapture::acquire(*staged.owner, limits_, retained)
          : CatalogCaptureResult{baseline_, {}};
      if (!captured.succeeded()) {
        auto e = error(DurableServiceErrorCode::CheckpointFailure, op);
        e.checkpoint = captured.error; return failure(e);
      }
      staged.captured = std::move(captured.captured);
      // During save, the coordinator charges the candidate projection and the
      // provider. This ledger charges both owners and only the old projection.
      auto savedRetained = retainedBytes(caller, staged.owner->snapshot(), true);
      CheckpointDetail::add(savedRetained, rootPathBytes(*staged.owner), limits_.stagedBytes);
      CheckpointDetail::add(savedRetained, candidateAdmissionBytes(staged), limits_.stagedBytes);
      DurableRootRegistrationResult result{registered, changed ? staged.owner->snapshot() : snapshot(), {}, {}};
      PendingAdmission pending(*this, staged);
      auto saved = store_.save(staged.captured->projection(), *expectation_, control, savedRetained);
#ifdef OPENHDK_ENABLE_TEST_SEAMS
      if (corruptAcknowledgment_ && saved.succeeded()) saved.capturedRevision.reset();
#endif
      if (!saved.succeeded()) return failure(*saveFailure(saved, op).error);
      if (saved.status != (changed ? StoreSaveStatus::Saved : StoreSaveStatus::Unchanged) || saved.capturedRevision != result.snapshot->revision
          || !saved.expectation->token()) {
        state_ = DurableServiceState::RecoveryRequired;
        auto e = error(DurableServiceErrorCode::ProtocolFault, op);
        e.cleanupWarning = saved.cleanupWarning; e.candidate = saved.candidate; e.prior = saved.prior;
        return failure(e);
      }
      result.receipt = DurableCommitReceipt{*saved.expectation->token(), *saved.capturedRevision, saved.cleanupWarning};
      // No allocation, I/O, cancellation or hook between confirmed commit and
      // context installation. Old contexts retire afterward by nonthrowing
      // destruction. One owner swap installs lineage, maps and both counters.
      if (changed) {
        owner_.swap(staged.owner);
        baseline_.swap(staged.captured);
        if (staged.admission) {
          nativeAdmission_.swap(staged.admission); admissionRoots_.swap(staged.roots);
        }
      }
      expectation_ = std::move(saved.expectation);
      return result;
    } catch (const StoreDetail::Failure& e) {
      auto failureError = error(DurableServiceErrorCode::StoreFailure, op);
      failureError.store = e.error; return failure(failureError);
    } catch (const CheckpointDetail::Failure& e) {
      auto failureError = error(DurableServiceErrorCode::CheckpointFailure, op);
      failureError.checkpoint = CheckpointError{e.code, CheckpointOperation::Capture, e.field, e.record, e.offset};
      return failure(failureError);
    } catch (const std::bad_alloc&) { return failure(error(DurableServiceErrorCode::StorageFailure, op)); }
  }
  struct StagedOwner {
    std::unique_ptr<SongDiscovery> owner;
    std::shared_ptr<const CapturedCatalogCheckpoint> captured;
    std::shared_ptr<const NativeStoreAdmission> admission;
    std::array<std::optional<RootId>,32> roots{};
  };
  // Fixed borrowed context only during coordinator.save; cleared on every exit.
  struct PendingAdmission {
    DurableLibraryService& service;
    PendingAdmission(DurableLibraryService& s, const StagedOwner& staged) noexcept : service(s) {
      if (staged.admission) service.pendingAdmission_ = &staged;
    }
    ~PendingAdmission() { service.pendingAdmission_ = nullptr; }
  };
  std::size_t candidateAdmissionBytes(const StagedOwner& staged) const noexcept {
#ifdef __linux__
    if (staged.admission) {
      std::size_t n=0;
      for (std::size_t i=0;i<staged.admission->count_;++i) {
        const auto& guard=staged.admission->roots_[i]; bool shared=false;
        for (std::size_t j=0;j<nativeAdmission_->count_;++j) shared |= guard==nativeAdmission_->roots_[j];
        if (!shared) n+=guard->path.size();
      }
      return n;
    }
#else
    (void)staged;
#endif
    return 0;
  }
  std::optional<DurableServiceError> stageAdmission(StagedOwner& staged, const std::filesystem::path& path,
      std::optional<RootId> target, std::size_t caller, DurableServiceOperation op) {
    if (!nativeAdmission_) return {};
#ifdef __linux__
    if(auto e=NativeStoreAdmission::authority(nativeProvider_->lease_,nativeAdmission_->binding_,NativeBindingOperation::Stage)) {
      lastAdmissionError_=e;state_=DurableServiceState::RecoveryRequired;return admissionError(op);
    }
    std::error_code ec; auto canonical=std::filesystem::canonical(path,ec);
    if(ec) {
      auto e=error(DurableServiceErrorCode::DiscoveryFailure,op);
      e.discovery=SongDiscovery::diagnostic(DiscoveryError::InvalidRoot,
          target?DiscoveryOperation::ReattachRoot:DiscoveryOperation::RegisterRoot); return e;
    }
    std::array<NativeStoreAdmission::ProspectiveRoot,32> rows{};
    std::size_t count=0;std::optional<std::size_t> retired;
    for(std::size_t i=0;i<nativeAdmission_->count_;++i) {
      const auto found=std::find_if(owner_->roots_.begin(),owner_->roots_.end(),
          [&](const auto& r){return admissionRoots_[i]==r.id;});
      if(found==owner_->roots_.end() || !found->attached || nativeAdmission_->roots_[i]->path!=found->path.native()) {
        state_=DurableServiceState::RecoveryRequired;return error(DurableServiceErrorCode::BaselineMismatch,op);
      }
      rows[i]={found->path.native(),i};staged.roots[i]=found->id;++count;
      if(target==found->id && canonical!=found->path) {rows[i]={canonical.native(),{}};retired=i;}
    }
    if(static_cast<std::size_t>(std::count_if(owner_->roots_.begin(),owner_->roots_.end(),
        [](const auto& r){return r.attached;}))!=count) {
      state_=DurableServiceState::RecoveryRequired;return error(DurableServiceErrorCode::BaselineMismatch,op);
    }
    const bool targetAttached=target && std::any_of(owner_->roots_.begin(),owner_->roots_.end(),
        [&](const auto& r){return r.id==*target && r.attached;});
    if(!targetAttached) {
      if(count==rows.size()) {
        lastAdmissionError_=NativeBindingError{NativeBindingErrorCode::LimitExceeded,NativeBindingOperation::Stage,{},{}};
        return admissionError(op);
      }
      rows[count]={canonical.native(),{}};staged.roots[count]=target;++count;
    }
    // The primitive charges binding+old guards itself; exclude them here.
    auto owned=retainedBytes(caller,staged.owner->snapshot(),true);
    CheckpointDetail::add(owned,rootPathBytes(*staged.owner),limits_.stagedBytes);
    CheckpointDetail::add(owned,provider_->retainedBytes(),limits_.stagedBytes);
    owned-=nativeAdmission_->retainedBytes();
    CheckpointDetail::add(owned,canonical.native().size(),limits_.stagedBytes);
    auto candidate=NativeStoreAdmission::stage(nativeProvider_->lease_,*nativeAdmission_,
        std::span<const NativeStoreAdmission::ProspectiveRoot>(rows).first(count),retired,nativeAdmission_->limits_,owned);
    if(candidate.error) {
      lastAdmissionError_=candidate.error;lastAdmissionRoot_.reset();
      if(candidate.error->rootIndex && *candidate.error->rootIndex<count)lastAdmissionRoot_=staged.roots[*candidate.error->rootIndex];
      // Configuration/target rejection may stay Ready, but old authority/drift
      // must never be adopted. Only the designated retired guard is exempt.
      bool drift=NativeStoreAdmission::authority(nativeProvider_->lease_,nativeAdmission_->binding_,NativeBindingOperation::Recheck).has_value();
      for(std::size_t i=0;i<nativeAdmission_->count_;++i)if(retired!=i)
        drift |= nativeAdmission_->checkRoot(i,NativeBindingOperation::Recheck).has_value();
      if(drift)state_=DurableServiceState::RecoveryRequired;
      return admissionError(op);
    }
    staged.admission=std::move(candidate.admission);return {};
#else
    (void)staged;(void)path;(void)target;(void)caller;
    return error(DurableServiceErrorCode::InvalidConfiguration,op);
#endif
  }
  std::optional<DurableServiceError> associateAdmission(StagedOwner& staged,std::optional<RootId> registered,
      std::optional<RootId> target,bool changed,DurableServiceOperation op) {
    if (!staged.admission) return {};
#ifdef __linux__
    std::size_t count=0;
    // Derive mappings from the staged owner, never from caller-supplied IDs.
    for(const auto& root:staged.owner->roots_)if(root.attached) {
      std::size_t index=count++;
      // First attachment may append after existing active guards even when its
      // restored record precedes them. Locate by prior RootId or fresh slot.
      auto at=std::find(staged.roots.begin(),staged.roots.end(),std::optional<RootId>(root.id));
      if(at!=staged.roots.end()) index=static_cast<std::size_t>(at-staged.roots.begin());
      else if(!target && registered==root.id)index=staged.admission->count_-1;
      if(index>=staged.admission->count_ || staged.admission->roots_[index]->path!=root.path.native()) {
        state_=DurableServiceState::RecoveryRequired;return error(DurableServiceErrorCode::BaselineMismatch,op);
      }
      staged.roots[index]=root.id;
      const auto old=std::find_if(owner_->snapshot()->roots.begin(),owner_->snapshot()->roots.end(),[&](const auto& r){return r.id==root.id;});
      const auto next=std::find_if(staged.owner->snapshot()->roots.begin(),staged.owner->snapshot()->roots.end(),[&](const auto& r){return r.id==root.id;});
      if(next==staged.owner->snapshot()->roots.end() || (old!=owner_->snapshot()->roots.end()
          && (old->policy!=next->policy || next->attachmentGeneration!=old->attachmentGeneration+(target==root.id && changed?1U:0U)))) {
        state_=DurableServiceState::RecoveryRequired;return error(DurableServiceErrorCode::BaselineMismatch,op);
      }
    }
    if(count!=staged.admission->count_) {state_=DurableServiceState::RecoveryRequired;return error(DurableServiceErrorCode::BaselineMismatch,op);}
#else
    (void)registered;(void)target;(void)changed;(void)op;
#endif
    return {};
  }
  struct Guard {
    std::atomic_flag& flag; bool held;
    explicit Guard(std::atomic_flag& f) noexcept : flag(f), held(!f.test_and_set(std::memory_order_acquire)) {}
    ~Guard() { if (held) flag.clear(std::memory_order_release); }
  };
  DurableLibraryService(std::unique_ptr<SongDiscovery> owner,
      std::unique_ptr<CheckpointStoreProvider> provider, CatalogCheckpointLimits limits)
      : provider_(std::move(provider)), owner_(std::move(owner)), store_(*provider_), limits_(limits) {}
  static DurableServiceError error(DurableServiceErrorCode code, DurableServiceOperation op) noexcept {
    return {code, op, {}, {}, {}, {}, {}, {}, {}, {}, {}};
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
  std::size_t rootPathBytes(const SongDiscovery& owner) const {
    std::size_t total = 0U;
    for (const auto& root : owner.roots_) {
      const auto units = root.path.native().size();
      constexpr auto unitBytes = sizeof(std::filesystem::path::value_type);
      if (units > limits_.stagedBytes / unitBytes)
        CheckpointDetail::fail(CheckpointErrorCode::LimitExceeded, CheckpointField::Hint);
      CheckpointDetail::add(total, units * unitBytes, limits_.stagedBytes);
      if (root.savedHint) CheckpointDetail::add(total, root.savedHint->size(), limits_.stagedBytes);
    }
    return total;
  }
  std::size_t pathBytes() const {
    auto total = rootPathBytes(*owner_);
    CheckpointDetail::add(total, admissionExtraBytes(), limits_.stagedBytes);
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
    e.root=lastAdmissionRoot_;
    return e;
  }
  static std::optional<StoreProviderError> admissionCheck(void* context) noexcept {
    auto& s=*static_cast<DurableLibraryService*>(context);
#ifdef OPENHDK_ENABLE_TEST_SEAMS
    ++s.admissionChecks_;
    if(s.forcedAdmissionFailure_) {
      s.lastAdmissionError_=NativeBindingError{NativeBindingErrorCode::SourceChanged,NativeBindingOperation::Recheck,{},{}};
      return StoreProviderError{StoreErrorCode::SourceChanged,0};
    }
#endif
    if(!s.nativeAdmission_)return {};
    const auto& admission = s.pendingAdmission_ ? s.pendingAdmission_->admission : s.nativeAdmission_;
    const auto& roots = s.pendingAdmission_ ? s.pendingAdmission_->roots : s.admissionRoots_;
    s.lastAdmissionError_=admission->recheck(s.nativeProvider_->lease_);
    s.lastAdmissionRoot_.reset();
    if(s.lastAdmissionError_ && s.lastAdmissionError_->rootIndex && *s.lastAdmissionError_->rootIndex<roots.size())
      s.lastAdmissionRoot_=roots[*s.lastAdmissionError_->rootIndex];
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
    if(admitted.error){lastAdmissionError_=admitted.error;
      if(admitted.error->rootIndex && *admitted.error->rootIndex<count)lastAdmissionRoot_=admissionRoots_[*admitted.error->rootIndex];
      return admissionError(op);}
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
  std::optional<RootId> lastAdmissionRoot_;
  const StagedOwner* pendingAdmission_=nullptr;
  // Field order guarantees store destruction/releases before provider destruction.
  std::unique_ptr<CheckpointStoreProvider> provider_;
  std::unique_ptr<SongDiscovery> owner_;
  CatalogCheckpointStore store_;
  CatalogCheckpointLimits limits_;
  std::shared_ptr<const CapturedCatalogCheckpoint> baseline_;
  std::optional<StoreExpectation> expectation_;
  DurableServiceState state_ = DurableServiceState::Unbound;
  std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
  static_assert(std::is_nothrow_swappable_v<std::shared_ptr<const NativeStoreAdmission>>);
  static_assert(std::is_nothrow_swappable_v<decltype(admissionRoots_)>);
  static_assert(std::is_nothrow_move_constructible_v<DurableRootReattachmentResult>);
  static_assert(std::is_nothrow_swappable_v<std::unique_ptr<SongDiscovery>>);
  static_assert(std::is_nothrow_swappable_v<decltype(baseline_)>);
  static_assert(std::is_nothrow_move_constructible_v<DurableRootRegistrationResult>);
  static_assert(std::is_nothrow_swappable_v<std::shared_ptr<const CatalogSnapshot>>);
  static_assert(std::is_nothrow_move_constructible_v<DurableOverrideResult>);
  static_assert(std::is_nothrow_move_constructible_v<DurableServiceOpenResult>);
  static_assert(std::is_nothrow_move_assignable_v<decltype(expectation_)>);
  static_assert(std::is_nothrow_copy_assignable_v<decltype(baseline_)>);
  static_assert(std::is_nothrow_move_assignable_v<std::optional<DurableCommitReceipt>>);
};
#ifdef OPENHDK_ENABLE_TEST_SEAMS
struct DurableLibraryServiceTestAccess {
  static DurableRootRegistrationResult registerRoot(DurableLibraryService& s,
      const std::filesystem::path& path, RootSourcePolicy policy = {},
      const StoreControl& control = {}, std::size_t caller = 0U) {
    return s.registerRoot(path, policy, control, caller);
  }
  static const SongDiscovery& owner(const DurableLibraryService& s) noexcept { return *s.owner_; }
  static std::optional<bool> attached(const DurableLibraryService& s, RootId id) noexcept {
    for (const auto& root : s.owner_->roots_) if (root.id == id) return root.attached;
    return {};
  }

  static bool pendingAdmission(const DurableLibraryService& s) noexcept {return s.pendingAdmission_!=nullptr;}
  static std::uint64_t admissionChecks(const DurableLibraryService& s) noexcept {return s.admissionChecks_;}
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
