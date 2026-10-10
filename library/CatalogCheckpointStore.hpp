// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/CatalogCheckpointCodec.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <type_traits>

namespace OpenHDK {
using CheckpointBytes = std::vector<std::uint8_t>;
struct StoreCommitToken {
  std::uint64_t sequence;
  std::array<std::uint8_t,32> digest;
  bool operator==(const StoreCommitToken&) const = default;
};
enum class StoreErrorCode {
  InvalidConfiguration, InvalidCheckpoint, UnsupportedSchema, UnsupportedProfile,
  UnsupportedRepresentation, InvalidText, LimitExceeded, NotFound, Busy,
  ForeignExpectation, StaleCheckpoint, CounterExhausted, Cancelled,
  StorageFailure, SourceChanged, UnsupportedStorage, CommitUncertain
};
enum class StoreOperation {
  Open, Read, Reconcile, Save, CreateCandidate, WriteCandidate, SyncCandidate,
  VerifyCandidate, CreatePrior, WritePrior, SyncPrior, VerifyPrior, Recheck,
  Publish, SyncPublication, Cleanup, Close
};
enum class StoreOutcome { NotCommitted, Uncertain };
struct StoreProviderError { StoreErrorCode code; std::int64_t nativeError = 0; };
struct StoreError {
  StoreErrorCode code;
  StoreOperation operation;
  StoreOutcome outcome = StoreOutcome::NotCommitted;
  std::int64_t nativeError = 0;
  std::optional<CheckpointError> checkpoint;
};
struct StoreArtifact { std::uint64_t identity; bool operator==(const StoreArtifact&) const = default; };
enum class StoreArtifactKind { Candidate, PriorCopy };
struct StoreArtifactResult { std::optional<StoreArtifact> artifact; std::optional<StoreProviderError> error; };
struct StoreReadResult { std::optional<CheckpointBytes> bytes; std::optional<StoreProviderError> error; };
enum class StorePublication { Published, NotCommitted, Uncertain };
struct StorePublicationResult { StorePublication outcome; std::optional<StoreProviderError> error; };

// Provider evidence is separate from coordinator regression tests.
// acquire MUST combine nonblocking in-process ownership and native lock/identity
// validation for the complete provider lifetime. release MUST NOT unlink locks.
// Artifact capabilities identify only this lease's exclusively created regular
// files; cleanup must recheck ownership and MUST NEVER delete the primary.
// Reads bound allocation before growth. Provider staging may throw bad_alloc;
// publication/synchronization/reconciliation/cleanup must not throw or allocate.
// A NotCommitted publication requires proof the namespace remained unchanged;
// all other indeterminate failures MUST report Uncertain. Providers must preserve
// artifacts on uncertainty and must not promote temps/backups on reopen.
class CheckpointStoreProvider {
 public:
  virtual ~CheckpointStoreProvider() = default;
  virtual std::optional<StoreProviderError> acquire() = 0;
  virtual void release() noexcept = 0;
  virtual std::size_t retainedBytes() const noexcept {return 0U;}
  virtual std::optional<StoreProviderError> checkDirectory() noexcept = 0;
  virtual StoreReadResult readPrimary(std::size_t limit) = 0;
  virtual StoreArtifactResult createArtifact(StoreArtifactKind kind) = 0;
  virtual std::optional<StoreProviderError> writeArtifact(StoreArtifact artifact,
      std::span<const std::uint8_t> bytes) = 0;
  virtual std::optional<StoreProviderError> syncArtifact(StoreArtifact artifact) noexcept = 0;
  virtual StoreReadResult readArtifact(StoreArtifact artifact,std::size_t limit) = 0;
  virtual StorePublicationResult publish(StoreArtifact candidate,bool expectedAbsent) noexcept = 0;
  virtual std::optional<StoreProviderError> syncPublication() noexcept = 0;
  virtual std::optional<StoreProviderError> reconcilePublication(bool primaryExists) noexcept = 0;
  virtual std::optional<StoreProviderError> cleanup(StoreArtifact artifact) noexcept = 0;
};
class CatalogCheckpointStore;
class StoreExpectation {
 public:
  const std::optional<StoreCommitToken>& token() const noexcept { return token_; }
 private:
  friend class CatalogCheckpointStore;
  StoreExpectation(std::shared_ptr<const unsigned char> origin,std::optional<StoreCommitToken> token) noexcept
      : origin_(std::move(origin)),token_(token) {}
  std::shared_ptr<const unsigned char> origin_;
  std::optional<StoreCommitToken> token_; // Absent means ExpectedAbsent.
};
struct StoreOpenResult {
  std::optional<CatalogCheckpointProjection> projection; // Absent means no primary.
  std::optional<StoreExpectation> expectation;
  std::optional<StoreError> error;
  bool succeeded() const noexcept { return expectation.has_value() && !error; }
};
enum class StoreSaveStatus { Saved, Unchanged };
struct StoreSaveResult {
  std::optional<StoreSaveStatus> status;
  std::optional<StoreExpectation> expectation;
  std::optional<std::uint64_t> capturedRevision;
  std::optional<StoreError> error;
  std::optional<StoreProviderError> cleanupWarning;
  std::optional<StoreArtifact> candidate,prior;
  bool succeeded() const noexcept { return status.has_value() && expectation.has_value() && !error; }
};
enum class StoreCheckpoint { BeforeStaging, CandidateVerified, PriorVerified, BeforePublication };
struct StoreControl {
  // Serialized control path only. Hooks must not throw or mutate borrowed input.
  std::function<bool()> cancelled;
  std::function<void(StoreCheckpoint)> checkpoint;
};
namespace StoreDetail {
struct Failure { StoreError error; };
[[noreturn]] inline void fail(StoreErrorCode c,StoreOperation op,std::int64_t native=0) {
  throw Failure{{c,op,StoreOutcome::NotCommitted,native,{}}};
}
inline void check(std::optional<StoreProviderError> e,StoreOperation op) { if(e) fail(e->code,op,e->nativeError); }
inline StoreErrorCode code(CheckpointErrorCode c) noexcept {
  switch(c) {
    case CheckpointErrorCode::InvalidConfiguration:return StoreErrorCode::InvalidConfiguration;
    case CheckpointErrorCode::InvalidCheckpoint:return StoreErrorCode::InvalidCheckpoint;
    case CheckpointErrorCode::UnsupportedSchema:return StoreErrorCode::UnsupportedSchema;
    case CheckpointErrorCode::UnsupportedProfile:return StoreErrorCode::UnsupportedProfile;
    case CheckpointErrorCode::UnsupportedRepresentation:return StoreErrorCode::UnsupportedRepresentation;
    case CheckpointErrorCode::InvalidText:return StoreErrorCode::InvalidText;
    case CheckpointErrorCode::LimitExceeded:return StoreErrorCode::LimitExceeded;
    case CheckpointErrorCode::StorageFailure:return StoreErrorCode::StorageFailure;
  }
  return StoreErrorCode::InvalidCheckpoint;
}
[[noreturn]] inline void codec(CheckpointError e,StoreOperation op) {
  throw Failure{{code(e.code),op,StoreOutcome::NotCommitted,0,e}};
}
inline StoreCommitToken token(std::span<const std::uint8_t> bytes,std::uint64_t seq) noexcept {
  StoreCommitToken t{seq,{}};std::copy(bytes.end()-32,bytes.end(),t.digest.begin());return t;
}
inline void charge(std::size_t& n,std::size_t extra,std::size_t limit,StoreOperation op) {
  if(n>limit || extra>limit-n) fail(StoreErrorCode::LimitExceeded,op);
  n+=extra;
}
// Numeric ordering is not identity. Compare keyed rows without string copies.
inline bool equalProjection(const CatalogCheckpointProjection& a,const CatalogCheckpointProjection& b) {
  if(a.catalogRevision!=b.catalogRevision || a.nextRoot!=b.nextRoot || a.nextSong!=b.nextSong
      || a.roots.size()!=b.roots.size() || a.songs.size()!=b.songs.size()) return false;
  std::map<std::uint64_t,const CheckpointRoot*> roots;
  std::map<std::uint64_t,const CheckpointSong*> songs;
  for(const auto& r:a.roots) roots.emplace(r.id,&r);
  for(const auto& r:a.songs) songs.emplace(r.id,&r);
  for(const auto& r:b.roots) { const auto f=roots.find(r.id);if(f==roots.end() || *f->second!=r)return false; }
  for(const auto& r:b.songs) { const auto f=songs.find(r.id);if(f==songs.end() || *f->second!=r)return false; }
  return true; // Store publication sequence deliberately excluded.
}
inline void history(const CatalogCheckpointProjection& old,const CatalogCheckpointProjection& next) {
  if(next.catalogRevision<=old.catalogRevision || next.nextRoot<old.nextRoot || next.nextSong<old.nextSong)
    fail(StoreErrorCode::StaleCheckpoint,StoreOperation::Save);
  std::map<std::uint64_t,const CheckpointRoot*> roots;
  std::map<std::uint64_t,const CheckpointSong*> songs;
  for(const auto& r:old.roots) roots.emplace(r.id,&r);
  for(const auto& r:old.songs) songs.emplace(r.id,&r);
  std::size_t retained=0U;
  for(const auto& r:next.roots) {
    const auto f=roots.find(r.id);
    if(f==roots.end()) {if(r.id<old.nextRoot)fail(StoreErrorCode::StaleCheckpoint,StoreOperation::Save);}
    else {++retained;const auto& previous=*f->second;
      if(r.attachmentGeneration<previous.attachmentGeneration || r.policy!=previous.policy
          || (r.attachmentGeneration==previous.attachmentGeneration && r.hint!=previous.hint))
        fail(StoreErrorCode::StaleCheckpoint,StoreOperation::Save);
    }
  }
  if(retained!=old.roots.size())fail(StoreErrorCode::StaleCheckpoint,StoreOperation::Save);
  for(const auto& r:next.songs) if(!songs.contains(r.id) && r.id<old.nextSong)
    fail(StoreErrorCode::StaleCheckpoint,StoreOperation::Save);
}
} // namespace StoreDetail

// Detached protocol coordinator, not a persistent library service. Its tickets
// bind only this store object, NOT SongDiscovery snapshot lineage/path mappings.
// Future live capture/admission must validate those before calling this protocol.
// All calls/queries use one control path. Provider must outlive this object.
// Constructor allocation may throw; failed operations return no acknowledgment.
class CatalogCheckpointStore {
 public:
  explicit CatalogCheckpointStore(CheckpointStoreProvider& provider)
      : provider_(provider),origin_(std::make_shared<const unsigned char>(0U)) {}
  ~CatalogCheckpointStore() { if(open_)provider_.release(); }
  CatalogCheckpointStore(const CatalogCheckpointStore&)=delete;
  CatalogCheckpointStore& operator=(const CatalogCheckpointStore&)=delete;
  bool faulted() const noexcept {return faulted_;}
  bool isOpen() const noexcept {return open_;}
  StoreOpenResult open(CatalogCheckpointLimits limits={},std::size_t alreadyOwned=0U) {
    Guard guard(busy_);if(!guard.held)return {{},{},error(StoreErrorCode::Busy,StoreOperation::Open)};
    if(open_)return {{},{},error(faulted_?StoreErrorCode::CommitUncertain:StoreErrorCode::Busy,StoreOperation::Open)};
    if(!CheckpointDetail::validLimits(limits) || alreadyOwned>limits.stagedBytes)
      return {{},{},error(StoreErrorCode::InvalidConfiguration,StoreOperation::Open)};
    bool leased=false;
    try {
      StoreDetail::check(provider_.acquire(),StoreOperation::Open);leased=true;
      StoreDetail::charge(alreadyOwned,provider_.retainedBytes(),limits.stagedBytes,StoreOperation::Open);
      StoreDetail::check(provider_.checkDirectory(),StoreOperation::Open);
      auto input=readPrimary(limits,alreadyOwned,StoreOperation::Read);
      std::optional<CatalogCheckpointProjection> projection;
      std::optional<StoreCommitToken> acknowledged;
      if(input) {
        auto decoded=decodeCatalogCheckpoint(*input,limits,alreadyOwned);
        if(!decoded.succeeded())StoreDetail::codec(*decoded.error,StoreOperation::Read);
        acknowledged=StoreDetail::token(*input,decoded.value->sequence);
        projection=std::move(decoded.value);
      } else if(faulted_)StoreDetail::fail(StoreErrorCode::NotFound,StoreOperation::Reconcile);
      StoreDetail::check(provider_.reconcilePublication(input.has_value()),StoreOperation::Reconcile);
      // All allocating result data has been prepared before enabling writes.
      StoreOpenResult result{std::move(projection),StoreExpectation(origin_,acknowledged),{}};
      ack_=acknowledged;limits_=limits;open_=true;faulted_=false;return result;
    } catch(const StoreDetail::Failure& e) {if(leased)provider_.release();return {{},{},e.error};
    } catch(const std::bad_alloc&) {if(leased)provider_.release();return {{},{},error(StoreErrorCode::StorageFailure,StoreOperation::Open)};}
  }
  std::optional<StoreError> close() noexcept {
    Guard guard(busy_);if(!guard.held)return error(StoreErrorCode::Busy,StoreOperation::Close);
    if(open_)provider_.release();
    open_=false;ack_.reset();return {};
    // Fault latch survives close; validated reopen plus reconciliation clears it.
  }
  StoreSaveResult save(const CatalogCheckpointProjection& projection,const StoreExpectation& expected,
      const StoreControl& control={},std::size_t alreadyOwned=0U) {
    Guard guard(busy_);if(!guard.held)return failed(error(StoreErrorCode::Busy,StoreOperation::Save));
    if(!open_)return failed(error(StoreErrorCode::InvalidConfiguration,StoreOperation::Save));
    if(faulted_)return failed(error(StoreErrorCode::CommitUncertain,StoreOperation::Save));
    if(expected.origin_!=origin_)return failed(error(StoreErrorCode::ForeignExpectation,StoreOperation::Save));
    if(expected.token_!=ack_)return failed(error(StoreErrorCode::StaleCheckpoint,StoreOperation::Save));
    Artifacts artifacts(provider_);
    auto operation=StoreOperation::Save;
    try {
      const auto shape=CheckpointDetail::shape(projection,limits_);
      std::size_t base=alreadyOwned;
      StoreDetail::charge(base,provider_.retainedBytes(),limits_.stagedBytes,operation);
      StoreDetail::charge(base,shape.strings,limits_.stagedBytes,operation);
      auto peak=base;StoreDetail::charge(peak,shape.largest,limits_.stagedBytes,operation);
      StoreDetail::charge(peak,shape.largest,limits_.stagedBytes,operation);
      CheckpointDetail::validate(projection,limits_);
      cancel(control);StoreDetail::check(provider_.checkDirectory(),operation);
      auto primary=readPrimary(limits_,base,StoreOperation::Read);
      std::optional<StoreCommitToken> current;
      std::optional<CatalogCheckpointProjection> prior;
      if(primary) {
        auto decoded=decodeCatalogCheckpoint(*primary,limits_,base);
        if(!decoded.succeeded())StoreDetail::codec(*decoded.error,StoreOperation::Read);
        current=StoreDetail::token(*primary,decoded.value->sequence);prior=std::move(decoded.value);
      }
      if(current!=ack_)StoreDetail::fail(StoreErrorCode::StaleCheckpoint,StoreOperation::Read);
      if(prior) {
        const auto oldShape=CheckpointDetail::shape(*prior,limits_);
        auto retained=base;StoreDetail::charge(retained,primary->size(),limits_.stagedBytes,operation);
        StoreDetail::charge(retained,oldShape.strings,limits_.stagedBytes,operation);
        if(StoreDetail::equalProjection(*prior,projection)) {
          checkAdmission();
          return {StoreSaveStatus::Unchanged,StoreExpectation(origin_,ack_),projection.catalogRevision,{},{},{},{}};
        }
        StoreDetail::history(*prior,projection);
        if(current->sequence==UINT64_MAX)StoreDetail::fail(StoreErrorCode::CounterExhausted,operation);
      }
      prior.reset(); // Release decoded old strings before constructing candidate.
      if(primary)StoreDetail::charge(base,primary->size(),limits_.stagedBytes,operation);
      CheckpointBytes candidate;
      const auto sequence=current?current->sequence+1U:1U;
      {
        auto copyPeak=base;StoreDetail::charge(copyPeak,shape.strings,limits_.stagedBytes,operation);
        auto next=projection;next.sequence=sequence;
        auto encoded=encodeCatalogCheckpoint(next,limits_,base);
        if(!encoded.succeeded())StoreDetail::codec(*encoded.error,StoreOperation::Save);
        candidate=std::move(*encoded.value);
      }
      StoreDetail::charge(base,candidate.size(),limits_.stagedBytes,operation);
      const auto newToken=StoreDetail::token(candidate,sequence);
      at(control,StoreCheckpoint::BeforeStaging);cancel(control);
      stage(artifacts.candidate,StoreArtifactKind::Candidate,candidate,base,operation);
      at(control,StoreCheckpoint::CandidateVerified);cancel(control);
      if(primary) {
        stage(artifacts.prior,StoreArtifactKind::PriorCopy,*primary,base,operation);
        at(control,StoreCheckpoint::PriorVerified);cancel(control);
      }
      at(control,StoreCheckpoint::BeforePublication);cancel(control);
      operation=StoreOperation::Recheck;
      StoreDetail::check(provider_.checkDirectory(),operation);
      {
        auto rechecked=readPrimary(limits_,base,operation);
        std::optional<StoreCommitToken> token;
        if(rechecked) {
          auto decoded=decodeCatalogCheckpoint(*rechecked,limits_,base);
          if(!decoded.succeeded())StoreDetail::codec(*decoded.error,operation);
          token=StoreDetail::token(*rechecked,decoded.value->sequence);
        }
        if(token!=current)StoreDetail::fail(StoreErrorCode::StaleCheckpoint,operation);
      }
      StoreDetail::check(provider_.checkDirectory(),operation);cancel(control);
      checkAdmission();
      // The result, token and artifact descriptors are ready before publication.
      // No callbacks, retry, throwing/allocating provider API or cancellation
      // check is permitted from here to acknowledgment.
      StoreSaveResult receipt{StoreSaveStatus::Saved,StoreExpectation(origin_,newToken),
          projection.catalogRevision,{},{},artifacts.candidate,artifacts.prior};
      static_assert(std::is_nothrow_move_constructible_v<StoreSaveResult>);
      const auto published=provider_.publish(*artifacts.candidate,!current);
      if(published.outcome==StorePublication::NotCommitted) {
        auto failure=error(published.error?published.error->code:StoreErrorCode::StorageFailure,StoreOperation::Publish);
        if(published.error)failure.nativeError=published.error->nativeError;
        return artifacts.failure(failure);
      }
      artifacts.preserve=true;
      if(published.outcome!=StorePublication::Published || published.error)
        return uncertain(artifacts,StoreOperation::Publish,published.error);
      if(auto e=provider_.syncPublication())return uncertain(artifacts,StoreOperation::SyncPublication,e);
      ack_=newToken;receipt.cleanupWarning=artifacts.clean();return receipt;
    } catch(const StoreDetail::Failure& e) {return artifacts.failure(e.error);
    } catch(const CheckpointDetail::Failure& e) {
      const CheckpointError nested{e.code,CheckpointOperation::Encode,e.field,e.record,e.offset};
      return artifacts.failure({StoreDetail::code(e.code),operation,StoreOutcome::NotCommitted,0,nested});
    } catch(const std::bad_alloc&) {return artifacts.failure(error(StoreErrorCode::StorageFailure,operation));}
  }
 private:
  friend class DurableLibraryService;
  // Private owner fence: fixed function/context, no public caller installation.
  void setAdmissionFence(void* context,std::optional<StoreProviderError> (*check)(void*) noexcept) noexcept {
    admissionContext_=context;admissionCheck_=check;
  }
  void checkAdmission() {if(admissionCheck_)StoreDetail::check(admissionCheck_(admissionContext_),StoreOperation::Recheck);}
  void* admissionContext_=nullptr;
  std::optional<StoreProviderError> (*admissionCheck_)(void*) noexcept=nullptr;
  struct Guard {
    std::atomic_flag& flag;bool held;
    explicit Guard(std::atomic_flag& f) noexcept:flag(f),held(!f.test_and_set(std::memory_order_acquire)){}
    ~Guard(){if(held)flag.clear(std::memory_order_release);}
  };
  struct Artifacts {
    CheckpointStoreProvider& provider;std::optional<StoreArtifact> candidate,prior;bool preserve=false;
    explicit Artifacts(CheckpointStoreProvider& p):provider(p){}
    ~Artifacts(){if(!preserve)clean();}
    std::optional<StoreProviderError> clean() noexcept {
      std::optional<StoreProviderError> warning;
      if(candidate) {warning=provider.cleanup(*candidate);candidate.reset();}
      if(prior) {const auto e=provider.cleanup(*prior);if(!warning)warning=e;prior.reset();}
      preserve=true;return warning;
    }
    StoreSaveResult failure(StoreError e) noexcept {
      auto c=candidate,p=prior;const auto warning=clean();return {{},{},{},e,warning,c,p};
    }
  };
  static StoreError error(StoreErrorCode c,StoreOperation op) noexcept {return {c,op,StoreOutcome::NotCommitted,0,{}};}
  static StoreSaveResult failed(StoreError e) noexcept {return {{},{},{},e,{},{},{}};}
  StoreSaveResult uncertain(Artifacts& a,StoreOperation op,std::optional<StoreProviderError> e) noexcept {
    faulted_=true;a.preserve=true;
    return {{},{},{},StoreError{StoreErrorCode::CommitUncertain,op,StoreOutcome::Uncertain,e?e->nativeError:0,{}},{},a.candidate,a.prior};
  }
  static void cancel(const StoreControl& c) {if(c.cancelled && c.cancelled())StoreDetail::fail(StoreErrorCode::Cancelled,StoreOperation::Save);}
  static void at(const StoreControl& c,StoreCheckpoint p) {if(c.checkpoint)c.checkpoint(p);}
  std::optional<CheckpointBytes> readPrimary(const CatalogCheckpointLimits& l,std::size_t retained,StoreOperation op) {
    if(retained>l.stagedBytes)StoreDetail::fail(StoreErrorCode::LimitExceeded,op);
    auto read=provider_.readPrimary(std::min(l.fileBytes,l.stagedBytes-retained));
    if(read.error && read.error->code==StoreErrorCode::NotFound && !read.bytes)return {};
    StoreDetail::check(read.error,op);
    if(!read.bytes)StoreDetail::fail(StoreErrorCode::StorageFailure,op);
    if(read.bytes->size()>l.fileBytes || read.bytes->size()>l.stagedBytes-retained)
      StoreDetail::fail(StoreErrorCode::LimitExceeded,op);
    return std::move(read.bytes);
  }
  void stage(std::optional<StoreArtifact>& artifact,StoreArtifactKind kind,std::span<const std::uint8_t> bytes,
      std::size_t retained,StoreOperation& operation) {
    const bool candidate=kind==StoreArtifactKind::Candidate;
    operation=candidate?StoreOperation::CreateCandidate:StoreOperation::CreatePrior;
    const auto created=provider_.createArtifact(kind);artifact=created.artifact;
    StoreDetail::check(created.error,operation);
    if(!artifact || artifact->identity==0U)StoreDetail::fail(StoreErrorCode::StorageFailure,operation);
    operation=candidate?StoreOperation::WriteCandidate:StoreOperation::WritePrior;
    StoreDetail::check(provider_.writeArtifact(*artifact,bytes),operation);
    operation=candidate?StoreOperation::SyncCandidate:StoreOperation::SyncPrior;
    StoreDetail::check(provider_.syncArtifact(*artifact),operation);
    operation=candidate?StoreOperation::VerifyCandidate:StoreOperation::VerifyPrior;
    if(retained>limits_.stagedBytes)StoreDetail::fail(StoreErrorCode::LimitExceeded,operation);
    auto verified=provider_.readArtifact(*artifact,std::min(limits_.fileBytes,limits_.stagedBytes-retained));
    StoreDetail::check(verified.error,operation);
    if(!verified.bytes || verified.bytes->size()>limits_.fileBytes || verified.bytes->size()>limits_.stagedBytes-retained)
      StoreDetail::fail(StoreErrorCode::LimitExceeded,operation);
    // Byte-exact equality to the already validated canonical checkpoint proves
    // schema, length, content and digest coverage without another decoded copy.
    if(!std::equal(verified.bytes->begin(),verified.bytes->end(),bytes.begin(),bytes.end()))
      StoreDetail::fail(StoreErrorCode::InvalidCheckpoint,operation);
  }
  CheckpointStoreProvider& provider_;
  std::shared_ptr<const unsigned char> origin_;
  std::optional<StoreCommitToken> ack_;
  CatalogCheckpointLimits limits_;
  std::atomic_flag busy_=ATOMIC_FLAG_INIT;
  bool open_=false,faulted_=false;
};
} // namespace OpenHDK
