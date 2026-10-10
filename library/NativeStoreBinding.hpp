// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/LinuxCheckpointLease.hpp"
namespace OpenHDK {
#ifdef OPENHDK_ENABLE_TEST_SEAMS
struct NativeStoreBindingTestAccess;
#endif
struct NativeStoreAdmissionLimits {
  std::size_t activeRoots=32,pathBytes=4096,components=32,parentSteps=32,
      descriptors=45,stagedBytes=64U*1024U*1024U;
};
enum class NativeBindingOperation { Export, Admit, Recheck };
enum class NativeBindingErrorCode { InvalidConfiguration, Unbound, ForeignBinding,
  StaleBinding, RootOverlap, SourceChanged, UnsupportedStorage, LimitExceeded, StorageFailure };
struct NativeBindingError {
  NativeBindingErrorCode code;NativeBindingOperation operation;
  std::optional<std::size_t> rootIndex;
  std::optional<StoreProviderError> providerError;
};
#ifdef OPENHDK_ENABLE_TEST_SEAMS
enum class NativeAdmissionCall { Observe, Authority, Ancestor, Terminate, Export, Failure };
struct NativeAdmissionReceipt {
  NativeAdmissionCall call;NativeBindingOperation operation;
  std::uint64_t device=0,inode=0,mount=0;
  std::size_t index=SIZE_MAX;int nativeError=0;
  bool ownerMatch=false,held=false,epochMatch=false,injected=false;
  int errorCode=-1;
};
#endif
// Descriptive ownership only. No public construction, raw handle, serialization,
// writer lock authority or unlink. A retained value cannot revive a released epoch.
class NativeStoreBinding {
 public:
  NativeStoreBinding(const NativeStoreBinding&)=delete;
  NativeStoreBinding& operator=(const NativeStoreBinding&)=delete;
  std::string_view parentMapping() const noexcept {return parent_;}
  std::string_view primaryName() const noexcept {return primary_;}
  std::size_t retainedBytes() const noexcept {return parent_.size()+primary_.size();}
 private:
  friend class NativeStoreAdmission;
#ifdef OPENHDK_ENABLE_TEST_SEAMS
  friend struct NativeStoreBindingTestAccess;
#endif
  NativeStoreBinding()=default;
  const LinuxCheckpointLease* owner_=nullptr; // Compare only, never dereference.
  std::shared_ptr<const unsigned char> epoch_;
  std::string parent_,primary_;
#ifdef __linux__
  LinuxCheckpointLease::Fd directory_;
  LinuxCheckpointLease::Identity identity_{};
  std::uint64_t mount_=0;
#endif
};
struct NativeStoreBindingResult {
  std::shared_ptr<const NativeStoreBinding> binding;
  std::optional<NativeBindingError> error;
};
class NativeStoreAdmission;
struct NativeStoreAdmissionResult {
  std::shared_ptr<const NativeStoreAdmission> admission;
  std::optional<NativeBindingError> error;
};
// Private preparation/recheck primitive for the future owning service factory.
// All mappings must come from that service's owner; no public admission factory.
// Serialized control path. Retained handles do not prove filesystem-wide atomicity.
class NativeStoreAdmission {
 public:
  NativeStoreAdmission(const NativeStoreAdmission&)=delete;
  NativeStoreAdmission& operator=(const NativeStoreAdmission&)=delete;
  std::size_t retainedBytes() const noexcept {
    auto bytes=binding_->retainedBytes();
#ifdef __linux__
    for(std::size_t i=0;i<count_;++i)bytes+=roots_[i].path.size();
#endif
    return bytes;
  }
 private:
  friend class DurableLibraryService;
  friend class LinuxCheckpointProvider;
#ifdef OPENHDK_ENABLE_TEST_SEAMS
  friend struct NativeStoreBindingTestAccess;
#endif
  NativeStoreAdmission()=default;
#ifdef OPENHDK_ENABLE_TEST_SEAMS
  inline static bool missingMount_=false,differentRootMount_=false,skipLexical_=false;
  inline static void (*afterRoots_)() noexcept=nullptr;
  inline static bool tracing_=false,overflow_=false;
  inline static std::size_t traceCount_=0;
  inline static std::array<NativeAdmissionReceipt,256> trace_{};
  static void record(NativeAdmissionReceipt r) noexcept {
    if(!tracing_)return;
    if(traceCount_==trace_.size()){overflow_=true;return;}
    trace_[traceCount_++]=r;
  }
#endif
  static NativeBindingError error(NativeBindingErrorCode c,NativeBindingOperation o,
      std::optional<std::size_t> i={},std::optional<StoreProviderError> e={}) noexcept {
#ifdef OPENHDK_ENABLE_TEST_SEAMS
    NativeAdmissionReceipt r{NativeAdmissionCall::Failure,o};r.index=i.value_or(SIZE_MAX);
    r.nativeError=e?e->nativeError:0;r.errorCode=static_cast<int>(c);
    r.injected=(c==NativeBindingErrorCode::UnsupportedStorage && (missingMount_ || differentRootMount_));record(r);
#endif
    return {c,o,i,e};
  }
  static bool valid(NativeStoreAdmissionLimits l) noexcept {
    return l.activeRoots>0 && l.activeRoots<=32 && l.pathBytes>0 && l.pathBytes<=4096 &&
      l.components>0 && l.components<=32 && l.parentSteps>0 && l.parentSteps<=32 &&
      l.descriptors>0 && l.descriptors<=45 && l.stagedBytes>0 && l.stagedBytes<=64U*1024U*1024U;
  }
  static bool charge(std::size_t& used,std::size_t n,std::size_t limit) noexcept {
    if(used>limit || n>limit-used)return false;
    used+=n;return true;
  }
  static bool contains(std::string_view root,std::string_view path) noexcept {
    return root=="/" || path==root || (path.size()>root.size() && path.starts_with(root) && path[root.size()]=='/');
  }
#ifdef __linux__
  using Fd=LinuxCheckpointLease::Fd;
  using Identity=LinuxCheckpointLease::Identity;
  struct Root {Fd directory;Identity identity{};std::uint64_t mount=0;std::string path;};
  std::array<Root,32> roots_{};
  std::size_t count_=0;
  struct Observation {Identity identity{};std::uint64_t mount=0;std::optional<NativeBindingError> error;};
  static Observation observe(int fd,NativeBindingOperation op,std::optional<std::size_t> root={}) noexcept {
    struct stat s{};
    if(::fstat(fd,&s)<0)return {{},0,error(NativeBindingErrorCode::SourceChanged,op,root,StoreProviderError{StoreErrorCode::SourceChanged,errno})};
#if defined(STATX_MNT_ID)
    struct statx sx{};
    if(::statx(fd,"",AT_EMPTY_PATH,STATX_MNT_ID,&sx)<0)
      return {{},0,error(NativeBindingErrorCode::UnsupportedStorage,op,root,StoreProviderError{StoreErrorCode::UnsupportedStorage,errno})};

#ifdef OPENHDK_ENABLE_TEST_SEAMS
    if(missingMount_)return {{},0,error(NativeBindingErrorCode::UnsupportedStorage,op,root)};
    if(root && differentRootMount_)sx.stx_mnt_id^=1U;
#endif
    if(!(sx.stx_mask&STATX_MNT_ID))return {{},0,error(NativeBindingErrorCode::UnsupportedStorage,op,root)};
#ifdef OPENHDK_ENABLE_TEST_SEAMS
    record({NativeAdmissionCall::Observe,op,static_cast<std::uint64_t>(s.st_dev),static_cast<std::uint64_t>(s.st_ino),sx.stx_mnt_id,
        root.value_or(SIZE_MAX),0,false,false,false,root.has_value() && differentRootMount_});
#endif
    return {LinuxCheckpointLease::identity(s),sx.stx_mnt_id,{}};
#else
    return {{},0,error(NativeBindingErrorCode::UnsupportedStorage,op,root)};
#endif
  }
  static bool validPath(std::string_view path,NativeStoreAdmissionLimits l) noexcept {
    return LinuxCheckpointLease::valid(path,"binding",{l.pathBytes,l.components,128});
  }
  std::optional<NativeBindingError> ancestors(NativeBindingOperation op) const noexcept {
    // At most two transient handles. Start at the retained capability, not text.
    int current=binding_->directory_.value;Fd owned;
    for(std::size_t step=0;;++step) {
      const auto child=observe(current,op);if(child.error)return child.error;
#ifdef OPENHDK_ENABLE_TEST_SEAMS
      record({NativeAdmissionCall::Ancestor,op,child.identity.device,child.identity.inode,child.mount,step,0,false,false,false,skipLexical_});
#endif
      for(std::size_t i=0;i<count_;++i)
        if(child.identity==roots_[i].identity && child.mount==roots_[i].mount)
          return error(NativeBindingErrorCode::RootOverlap,op,i);
      if(step>=limits_.parentSteps)return error(NativeBindingErrorCode::LimitExceeded,op);
      Fd parent(::openat(current,"..",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
      if(parent.value<0)return error(NativeBindingErrorCode::SourceChanged,op,{},StoreProviderError{StoreErrorCode::SourceChanged,errno});
      const auto next=observe(parent.value,op);if(next.error)return next.error;
      if(next.mount!=child.mount || next.identity==child.identity) {
#ifdef OPENHDK_ENABLE_TEST_SEAMS
        // index 0 = mount boundary; 1 = self-parent.
        record({NativeAdmissionCall::Terminate,op,next.identity.device,next.identity.inode,next.mount,
            next.mount!=child.mount?0U:1U});
#endif
        return {};
      }
      owned=std::move(parent);current=owned.value;
    }
  }
#endif
  static std::optional<NativeBindingError> authority(const LinuxCheckpointLease& lease,
      const std::shared_ptr<const NativeStoreBinding>& b,NativeBindingOperation op) noexcept {
#ifdef OPENHDK_ENABLE_TEST_SEAMS
    record({NativeAdmissionCall::Authority,op,0,0,0,SIZE_MAX,0,b && b->owner_==&lease,
        lease.held_,b && b->epoch_==lease.bindingEpoch_});
#endif
    if(!b)return error(NativeBindingErrorCode::Unbound,op);
    if(b->owner_!=&lease)return error(NativeBindingErrorCode::ForeignBinding,op);
    if(!lease.held_ || b->epoch_!=lease.bindingEpoch_)return error(NativeBindingErrorCode::StaleBinding,op);
    if(auto e=lease.check())return error(NativeBindingErrorCode::SourceChanged,op,{},e);
#ifdef __linux__
    const auto held=observe(b->directory_.value,op);if(held.error)return held.error;
    const auto current=observe(lease.directory_.value,op);if(current.error)return current.error;
    if(held.identity!=b->identity_ || held.mount!=b->mount_ || current.identity!=held.identity || current.mount!=held.mount ||
        lease.path_!=b->parent_ || lease.name_!=b->primary_)return error(NativeBindingErrorCode::SourceChanged,op);
    return {};
#else
    return error(NativeBindingErrorCode::UnsupportedStorage,op);
#endif
  }
  static NativeStoreBindingResult exportBinding(LinuxCheckpointLease& lease,
      NativeStoreAdmissionLimits l={},std::size_t alreadyOwned=0) {
    constexpr auto op=NativeBindingOperation::Export;
    if(!valid(l) || alreadyOwned>l.stagedBytes)return {{},error(NativeBindingErrorCode::InvalidConfiguration,op)};
    if(!lease.held_)return {{},error(NativeBindingErrorCode::Unbound,op)};
#ifdef __linux__
    if(!validPath(lease.path_,l))return {{},error(NativeBindingErrorCode::LimitExceeded,op)};
    if(l.descriptors<13)return {{},error(NativeBindingErrorCode::LimitExceeded,op)};
    auto used=alreadyOwned;
    if(!charge(used,lease.path_.size(),l.stagedBytes) || !charge(used,lease.name_.size(),l.stagedBytes))
      return {{},error(NativeBindingErrorCode::LimitExceeded,op)};
    if(auto e=lease.check())return {{},error(NativeBindingErrorCode::SourceChanged,op,{},e)};
    if(lease.binding_) {
      if(auto e=authority(lease,lease.binding_,op))return {{},e};
#ifdef OPENHDK_ENABLE_TEST_SEAMS
      record({NativeAdmissionCall::Export,op,lease.binding_->identity_.device,lease.binding_->identity_.inode,lease.binding_->mount_,1});
#endif
      return {lease.binding_,{}};
    }
    const auto observed=observe(lease.directory_.value,op);if(observed.error)return {{},observed.error};
    try {
      Fd duplicate(::fcntl(lease.directory_.value,F_DUPFD_CLOEXEC,0));
      if(duplicate.value<0)return {{},error(NativeBindingErrorCode::StorageFailure,op,{},StoreProviderError{StoreErrorCode::StorageFailure,errno})};
      auto b=std::shared_ptr<NativeStoreBinding>(new NativeStoreBinding);
      b->directory_=std::move(duplicate);b->parent_=lease.path_;b->primary_=lease.name_;
      b->owner_=&lease;b->epoch_=std::make_shared<const unsigned char>(0);
      b->identity_=observed.identity;b->mount_=observed.mount;
      if(auto e=lease.check())return {{},error(NativeBindingErrorCode::SourceChanged,op,{},e)};
      const auto final=observe(lease.directory_.value,op);if(final.error)return {{},final.error};
      if(final.identity!=b->identity_ || final.mount!=b->mount_)return {{},error(NativeBindingErrorCode::SourceChanged,op)};
      lease.bindingEpoch_=b->epoch_;lease.binding_=b;lease.bindingBytes_=b->retainedBytes();
#ifdef OPENHDK_ENABLE_TEST_SEAMS
      record({NativeAdmissionCall::Export,op,b->identity_.device,b->identity_.inode,b->mount_,0});
#endif
      return {std::move(b),{}};
    } catch(const std::bad_alloc&) {return {{},error(NativeBindingErrorCode::StorageFailure,op)};}
#else
    return {{},error(NativeBindingErrorCode::UnsupportedStorage,op)};
#endif
  }
  static NativeStoreAdmissionResult admit(const LinuxCheckpointLease& lease,
      std::shared_ptr<const NativeStoreBinding> b,std::span<const std::string_view> paths,
      NativeStoreAdmissionLimits l={},std::size_t alreadyOwned=0) {
    constexpr auto op=NativeBindingOperation::Admit;
    if(!valid(l) || alreadyOwned>l.stagedBytes)return {{},error(NativeBindingErrorCode::InvalidConfiguration,op)};
    if(paths.size()>l.activeRoots || 13U+paths.size()>l.descriptors)return {{},error(NativeBindingErrorCode::LimitExceeded,op)};
    if(auto e=authority(lease,b,op))return {{},e};
#ifdef __linux__
    auto used=alreadyOwned;
    if(!charge(used,b->retainedBytes(),l.stagedBytes))return {{},error(NativeBindingErrorCode::LimitExceeded,op)};
    for(std::size_t i=0;i<paths.size();++i) {
      if(!validPath(paths[i],l))return {{},error(NativeBindingErrorCode::InvalidConfiguration,op,i)};
      if(!charge(used,paths[i].size(),l.stagedBytes))return {{},error(NativeBindingErrorCode::LimitExceeded,op,i)};
    }
    try {
      auto a=std::shared_ptr<NativeStoreAdmission>(new NativeStoreAdmission);
      a->binding_=std::move(b);a->limits_=l;
      for(std::size_t i=0;i<paths.size();++i) {
        auto opened=LinuxCheckpointLease::walk(paths[i]);
        if(opened.error)return {{},error(NativeBindingErrorCode::SourceChanged,op,i,opened.error)};
        auto& root=a->roots_[i];root.directory=std::move(opened.fd);
        const auto observed=observe(root.directory.value,op,i);if(observed.error)return {{},observed.error};
        if(observed.mount!=a->binding_->mount_)return {{},error(NativeBindingErrorCode::UnsupportedStorage,op,i)};
        root.identity=observed.identity;root.mount=observed.mount;root.path=paths[i];++a->count_;
        bool lexical=contains(root.path,a->binding_->parent_);
#ifdef OPENHDK_ENABLE_TEST_SEAMS
        if(skipLexical_)lexical=false;
#endif
        if(lexical)return {{},error(NativeBindingErrorCode::RootOverlap,op,i)};
      }

#ifdef OPENHDK_ENABLE_TEST_SEAMS
      if(afterRoots_)afterRoots_(); // Test hook must not re-enter a lease/service.
#endif
      if(auto e=a->ancestors(op))return {{},e};
      if(auto e=a->recheckImpl(lease,op))return {{},e};
      return {std::move(a),{}};
    } catch(const std::bad_alloc&) {return {{},error(NativeBindingErrorCode::StorageFailure,op)};}
#else
    (void)paths;return {{},error(NativeBindingErrorCode::UnsupportedStorage,op)};
#endif
  }
  std::optional<NativeBindingError> recheck(const LinuxCheckpointLease& lease) const noexcept {
    return recheckImpl(lease,NativeBindingOperation::Recheck);
  }
  std::optional<NativeBindingError> recheckImpl(const LinuxCheckpointLease& lease,NativeBindingOperation op) const noexcept {
    if(auto e=authority(lease,binding_,op))return e;
#ifdef __linux__
    for(std::size_t i=0;i<count_;++i) {
      auto observed=LinuxCheckpointLease::walk(roots_[i].path);
      if(observed.error)return error(NativeBindingErrorCode::SourceChanged,op,i,observed.error);
      const auto named=observe(observed.fd.value,op,i);if(named.error)return named.error;
      if(named.identity!=roots_[i].identity || named.mount!=roots_[i].mount)return error(NativeBindingErrorCode::SourceChanged,op,i);
    }
    return ancestors(op);
#else
    return error(NativeBindingErrorCode::UnsupportedStorage,op);
#endif
  }
  std::shared_ptr<const NativeStoreBinding> binding_;
  NativeStoreAdmissionLimits limits_;
};
#ifdef OPENHDK_ENABLE_TEST_SEAMS
struct NativeStoreBindingTestAccess {
  static void receipts(bool enabled=true) noexcept {NativeStoreAdmission::tracing_=enabled;NativeStoreAdmission::traceCount_=0;NativeStoreAdmission::overflow_=false;}
  static std::span<const NativeAdmissionReceipt> records() noexcept {return {NativeStoreAdmission::trace_.data(),NativeStoreAdmission::traceCount_};}
  static bool overflow() noexcept {return NativeStoreAdmission::overflow_;}
  static void missingMount(bool v) noexcept {NativeStoreAdmission::missingMount_=v;}
  static void differentRootMount(bool v) noexcept {NativeStoreAdmission::differentRootMount_=v;}
  static void skipLexical(bool v) noexcept {NativeStoreAdmission::skipLexical_=v;}
  static void afterRoots(void (*hook)() noexcept) noexcept {NativeStoreAdmission::afterRoots_=hook;}
  static NativeStoreBindingResult binding(LinuxCheckpointLease& l,NativeStoreAdmissionLimits limits={},std::size_t owned=0) {
    return NativeStoreAdmission::exportBinding(l,limits,owned);
  }
  static NativeStoreAdmissionResult admit(const LinuxCheckpointLease& l,std::shared_ptr<const NativeStoreBinding> b,
      std::span<const std::string_view> paths,NativeStoreAdmissionLimits limits={},std::size_t owned=0) {
    return NativeStoreAdmission::admit(l,std::move(b),paths,limits,owned);
  }
  static std::optional<NativeBindingError> recheck(const NativeStoreAdmission& a,const LinuxCheckpointLease& l) noexcept {return a.recheck(l);}
#ifdef __linux__
  static int descriptor(const NativeStoreBinding& b) noexcept {return b.directory_.value;}
#endif
};
#endif
} // namespace OpenHDK
