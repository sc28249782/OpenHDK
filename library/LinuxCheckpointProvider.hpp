// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/LinuxCheckpointLease.hpp"
#ifdef __linux__
#include <sys/syscall.h>
#endif
namespace OpenHDK {
#ifdef OPENHDK_ENABLE_TEST_SEAMS
struct LinuxCheckpointProviderTestAccess;
enum class LinuxProviderFault { None, Write, ReadArtifact, SyncArtifact, Publish, SyncPublication, Cleanup, Reconcile };
#endif
// Serialized control path. Native ext4 evidence remains a separate gate.
// Trusted directory/advisory writers only: identity checks are not pathname CAS.
// Constructor strings may throw. Retained strings must be charged via ownedBytes.
class LinuxCheckpointProvider final : public CheckpointStoreProvider {
 public:
  LinuxCheckpointProvider(std::string parent,std::string primary)
      : parent_(std::move(parent)),primaryName_(std::move(primary)) {}
  ~LinuxCheckpointProvider() override {release();}
  LinuxCheckpointProvider(const LinuxCheckpointProvider&)=delete;
  LinuxCheckpointProvider& operator=(const LinuxCheckpointProvider&)=delete;
  std::size_t ownedBytes() const noexcept {return 192U+parent_.size()+primaryName_.size()+lease_.ownedBytes();}
  std::size_t retainedBytes() const noexcept override {return ownedBytes();}
  std::optional<StoreProviderError> acquire() override {
    if(lease_.held())return error(StoreErrorCode::Busy);
    if(primaryName_.starts_with(".ohk-stage-"))return error(StoreErrorCode::InvalidConfiguration);
    auto e=lease_.acquireImpl(parent_,primaryName_,{},testFilesystem());if(e)return e;
#ifdef __linux__
    if(auto failure=probe()){release();return failure;}
#endif
    return {};
  }
  // Preserve abandoned artifacts, including uncertainty. Never scan suffixes.
  void release() noexcept override {
#ifdef __linux__
    primary_.reset();observedPrimary_=false;
    for(auto& s:slots_){s.fd.reset();s.id=0;s.consumed=false;}
#endif
    lease_.release();
  }
  std::optional<StoreProviderError> checkDirectory() noexcept override {return lease_.check();}
  StoreReadResult readPrimary(std::size_t limit) override {
#ifdef __linux__
    if(auto e=checkDirectory())return {{},e};
    auto opened=openRead(primaryName_.c_str());
    primary_.reset();observedPrimary_=false;
    if(opened.error)return {{},opened.error};
    primary_=std::move(opened.fd);observedPrimary_=true;
    return read(primary_.value,limit);
#else
    (void)limit;return {{},error(StoreErrorCode::UnsupportedStorage)};
#endif
  }
  StoreArtifactResult createArtifact(StoreArtifactKind kind) override {
    (void)kind;
#ifdef __linux__
    if(auto e=checkDirectory())return {{},e};
    Slot* slot=nullptr;for(auto& s:slots_)if(!s.id){slot=&s;break;}
    if(!slot)return {{},error(StoreErrorCode::LimitExceeded)};
    for(unsigned attempt=0;attempt<32;++attempt) {
      const auto id=nextIdentity();if(!id)return {{},error(StoreErrorCode::CounterExhausted)};
      std::array<char,96> name{};auto* at=name.data();
      constexpr std::string_view prefix=".ohk-stage-";at=std::copy(prefix.begin(),prefix.end(),at);
      at=std::to_chars(at,name.data()+name.size()-1,static_cast<std::uint64_t>(::getpid())).ptr;*at++='-';
      std::to_chars(at,name.data()+name.size()-1,*id);
      if(primaryName_==name.data())continue; // Never create a probe/stage at selected primary.
      Fd fd(::openat(dir(),name.data(),O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC,0600));
      if(fd.value<0){if(errno==EEXIST)continue;return {{},error(StoreErrorCode::StorageFailure,errno)};}
      slot->name=name;slot->id=*id;slot->fd=std::move(fd);slot->consumed=false;
      if(auto e=verify(*slot))return {StoreArtifact{*id},e};
      return {StoreArtifact{*id},{}};
    }
    return {{},error(StoreErrorCode::StorageFailure,EEXIST)};
#else
    return {{},error(StoreErrorCode::UnsupportedStorage)};
#endif
  }
  std::optional<StoreProviderError> writeArtifact(StoreArtifact a,std::span<const std::uint8_t> bytes) override {
#ifdef __linux__
    auto* s=slot(a);if(!s)return error(StoreErrorCode::InvalidConfiguration);
    if(bytes.size()>64U*1024U*1024U)return error(StoreErrorCode::LimitExceeded);
    if(auto e=verify(*s))return e;
    if(inject(1))return error(StoreErrorCode::StorageFailure,EIO);
    struct stat info{};if(::fstat(s->fd.value,&info)<0)return error(StoreErrorCode::StorageFailure,errno);
    if(info.st_size!=0)return error(StoreErrorCode::InvalidConfiguration);
    std::size_t at=0;
    while(at<bytes.size()) {
      const auto n=writeNative(s->fd.value,bytes.data()+at,std::min<std::size_t>(65536,bytes.size()-at),static_cast<off_t>(at));
      if(n<0){if(errno==EINTR)continue;return error(StoreErrorCode::StorageFailure,errno);}
      if(n==0)return error(StoreErrorCode::StorageFailure,EIO);
      at+=static_cast<std::size_t>(n);
    }
    return verify(*s);
#else
    (void)a;(void)bytes;return error(StoreErrorCode::UnsupportedStorage);
#endif
  }
  std::optional<StoreProviderError> syncArtifact(StoreArtifact a) noexcept override {
#ifdef __linux__
    auto* s=slot(a);if(!s)return error(StoreErrorCode::InvalidConfiguration);
    if(auto e=verify(*s))return e;
    if(inject(3))return error(StoreErrorCode::StorageFailure,EIO);
    if(auto e=sync(s->fd.value))return e;
    return sync(dir()); // Persist candidate/prior name before replacement.
#else
    (void)a;return error(StoreErrorCode::UnsupportedStorage);
#endif
  }
  StoreReadResult readArtifact(StoreArtifact a,std::size_t limit) override {
#ifdef __linux__
    auto* s=slot(a);if(!s)return {{},error(StoreErrorCode::InvalidConfiguration)};
    if(auto e=verify(*s))return {{},e};
    if(inject(2))return {{},error(StoreErrorCode::StorageFailure,EIO)};
    auto reopened=openRead(s->name.data());if(reopened.error)return {{},reopened.error};
    if(!same(s->fd.value,reopened.fd.value))return {{},error(StoreErrorCode::SourceChanged)};
    return read(reopened.fd.value,limit);
#else
    (void)a;(void)limit;return {{},error(StoreErrorCode::UnsupportedStorage)};
#endif
  }
  StorePublicationResult publish(StoreArtifact a,bool absent) noexcept override {
#ifdef __linux__
    auto* s=slot(a);if(!s)return {StorePublication::NotCommitted,error(StoreErrorCode::InvalidConfiguration)};
    if(auto e=verify(*s))return {StorePublication::NotCommitted,e};
    if(auto e=checkDirectory())return {StorePublication::NotCommitted,e};
    if(!absent && (!observedPrimary_ || !named(primary_.value,primaryName_.c_str())))
      return {StorePublication::NotCommitted,error(StoreErrorCode::SourceChanged)};
    if(inject(4))return {StorePublication::Uncertain,error(StoreErrorCode::StorageFailure,EIO)};
    const auto rc=absent?noReplace(s->name.data(),primaryName_.c_str())
        : ::renameat(dir(),s->name.data(),dir(),primaryName_.c_str());
    if(rc<0) {
      const auto native=errno;
      if(absent && native==EEXIST)return {StorePublication::NotCommitted,error(StoreErrorCode::StaleCheckpoint,native)};
      // Unknown native errors after invocation are not proof of rollback.
      return {StorePublication::Uncertain,error(StoreErrorCode::StorageFailure,native)};
    }
    s->consumed=true;
#ifdef OPENHDK_ENABLE_TEST_SEAMS
    if(afterPublication_)afterPublication_(); // Test-only child cut-point; no production callback.
#endif
    return {StorePublication::Published,{}};
#else
    (void)a;(void)absent;return {StorePublication::NotCommitted,error(StoreErrorCode::UnsupportedStorage)};
#endif
  }
  std::optional<StoreProviderError> syncPublication() noexcept override {
#ifdef __linux__
    if(auto e=checkDirectory())return e;
    if(inject(5))return error(StoreErrorCode::StorageFailure,EIO);
    return sync(dir());
#else
    return error(StoreErrorCode::UnsupportedStorage);
#endif
  }
  std::optional<StoreProviderError> reconcilePublication(bool exists) noexcept override {
#ifdef __linux__
    if(auto e=checkDirectory())return e;
    if(inject(7))return error(StoreErrorCode::StorageFailure,EIO);
    if(exists) {
      if(!observedPrimary_ || !named(primary_.value,primaryName_.c_str()))return error(StoreErrorCode::SourceChanged);
      if(auto e=sync(primary_.value))return e;
    } else {
      struct stat st{};
      if(::fstatat(dir(),primaryName_.c_str(),&st,AT_SYMLINK_NOFOLLOW)==0)return error(StoreErrorCode::SourceChanged);
      if(errno!=ENOENT)return error(StoreErrorCode::StorageFailure,errno);
    }
    return sync(dir());
#else
    (void)exists;return error(StoreErrorCode::UnsupportedStorage);
#endif
  }
  std::optional<StoreProviderError> cleanup(StoreArtifact a) noexcept override {
#ifdef __linux__
    auto* s=slot(a);if(!s)return error(StoreErrorCode::InvalidConfiguration);
    if(!s->consumed) {
      if(auto e=verify(*s))return e;
      if(inject(6))return error(StoreErrorCode::StorageFailure,EIO);
      if(::unlinkat(dir(),s->name.data(),0)<0)return error(StoreErrorCode::StorageFailure,errno);
    }
    // Consumed name is never interpreted as authority to unlink primary.
    s->fd.reset();s->id=0;s->consumed=false;return {};
#else
    (void)a;return error(StoreErrorCode::UnsupportedStorage);
#endif
  }
 private:
#ifdef OPENHDK_ENABLE_TEST_SEAMS
  friend struct LinuxCheckpointProviderTestAccess;
  bool bypass_=false;
  bool readInterrupted_=false,writeInterrupted_=false,shortIo_=false;
  unsigned readCalls_=0,writeCalls_=0;
  LinuxProviderFault fault_=LinuxProviderFault::None;
  void (*afterPublication_)() noexcept=nullptr;
#endif
  bool testFilesystem() const noexcept {
#ifdef OPENHDK_ENABLE_TEST_SEAMS
    return bypass_;
#else
    return false;
#endif
  }
  bool inject(int fault) const noexcept {
#ifdef OPENHDK_ENABLE_TEST_SEAMS
    return static_cast<int>(fault_)==fault;
#else
    (void)fault;return false;
#endif
  }
  static std::optional<StoreProviderError> error(StoreErrorCode c,std::int64_t n=0) noexcept {return StoreProviderError{c,n};}
#ifdef __linux__
  using Fd=LinuxCheckpointLease::Fd;
  struct Slot {std::uint64_t id=0;std::array<char,96> name{};Fd fd;bool consumed=false;};
  std::array<Slot,2> slots_{};
  Fd primary_;
  bool observedPrimary_=false;
  inline static std::atomic<std::uint64_t> identities_{0};
  static std::optional<std::uint64_t> nextIdentity() noexcept {
    auto n=identities_.load();while(n!=UINT64_MAX)if(identities_.compare_exchange_weak(n,n+1))return n+1;
    return {};
  }
  int dir() const noexcept {return lease_.directory_.value;}
  Slot* slot(StoreArtifact a) noexcept {for(auto& s:slots_)if(s.id && s.id==a.identity)return &s;return nullptr;}
  static bool same(int a,int b) noexcept {
    struct stat x{},y{};return ::fstat(a,&x)==0 && ::fstat(b,&y)==0 &&
        LinuxCheckpointLease::identity(x)==LinuxCheckpointLease::identity(y);
  }
  bool named(int fd,const char* name) const noexcept {
    struct stat x{},y{};return ::fstat(fd,&x)==0 && ::fstatat(dir(),name,&y,AT_SYMLINK_NOFOLLOW)==0 &&
        S_ISREG(x.st_mode) && S_ISREG(y.st_mode) && x.st_nlink==1 && y.st_nlink==1 &&
        LinuxCheckpointLease::identity(x)==LinuxCheckpointLease::identity(y);
  }
  std::optional<StoreProviderError> verify(const Slot& s) const noexcept {
    if(!lease_.held() || s.consumed || !named(s.fd.value,s.name.data()))return error(StoreErrorCode::SourceChanged);
    return {};
  }
  struct Opened {Fd fd;std::optional<StoreProviderError> error;};
  Opened openRead(const char* name) const noexcept {
    struct stat before{};
    if(::fstatat(dir(),name,&before,AT_SYMLINK_NOFOLLOW)<0)return {Fd{},error(errno==ENOENT?StoreErrorCode::NotFound:StoreErrorCode::StorageFailure,errno)};
    if(!S_ISREG(before.st_mode) || before.st_nlink!=1)return {Fd{},error(StoreErrorCode::SourceChanged)};
    Fd fd(::openat(dir(),name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));struct stat after{};
    if(fd.value<0 || ::fstat(fd.value,&after)<0)return {std::move(fd),error(StoreErrorCode::StorageFailure,errno)};
    if(!named(fd.value,name) || LinuxCheckpointLease::identity(before)!=LinuxCheckpointLease::identity(after))
      return {std::move(fd),error(StoreErrorCode::SourceChanged)};
    return {std::move(fd),{}};
  }
  StoreReadResult read(int fd,std::size_t limit) {
    if(limit>64U*1024U*1024U)return {{},error(StoreErrorCode::InvalidConfiguration)};
    struct stat before{};if(::fstat(fd,&before)<0)return {{},error(StoreErrorCode::StorageFailure,errno)};
    if(before.st_size<0 || static_cast<std::uint64_t>(before.st_size)>limit)return {{},error(StoreErrorCode::LimitExceeded)};
    CheckpointBytes bytes(static_cast<std::size_t>(before.st_size));std::size_t at=0;
    while(at<bytes.size()) {
      const auto n=readNative(fd,bytes.data()+at,std::min<std::size_t>(65536,bytes.size()-at),static_cast<off_t>(at));
      if(n<0){if(errno==EINTR)continue;return {{},error(StoreErrorCode::StorageFailure,errno)};}
      if(n==0)return {{},error(StoreErrorCode::SourceChanged)};
      at+=static_cast<std::size_t>(n);
    }
    std::uint8_t extra=0;ssize_t n;do{n=::pread(fd,&extra,1,static_cast<off_t>(at));}while(n<0 && errno==EINTR);
    if(n<0)return {{},error(StoreErrorCode::StorageFailure,errno)};
    if(n)return {{},error(at==limit?StoreErrorCode::LimitExceeded:StoreErrorCode::SourceChanged)};
    struct stat after{};if(::fstat(fd,&after)<0)return {{},error(StoreErrorCode::StorageFailure,errno)};
    if(before.st_size!=after.st_size)return {{},error(StoreErrorCode::SourceChanged)};
    return {std::move(bytes),{}};
  }
  ssize_t readNative(int fd,void* data,std::size_t size,off_t offset) noexcept {
#ifdef OPENHDK_ENABLE_TEST_SEAMS
    ++readCalls_;if(readInterrupted_){readInterrupted_=false;errno=EINTR;return -1;}
    if(shortIo_)size=std::min<std::size_t>(size,7);
#endif
    return ::pread(fd,data,size,offset);
  }
  ssize_t writeNative(int fd,const void* data,std::size_t size,off_t offset) noexcept {
#ifdef OPENHDK_ENABLE_TEST_SEAMS
    ++writeCalls_;if(writeInterrupted_){writeInterrupted_=false;errno=EINTR;return -1;}
    if(shortIo_)size=std::min<std::size_t>(size,7);
#endif
    return ::pwrite(fd,data,size,offset);
  }
  static std::optional<StoreProviderError> sync(int fd) noexcept {
    int rc;do{rc=::fsync(fd);}while(rc<0 && errno==EINTR);
    if(rc<0)return error(StoreErrorCode::StorageFailure,errno);
    return {};
  }
  int noReplace(const char* from,const char* to) const noexcept {
#ifdef SYS_renameat2
    return static_cast<int>(::syscall(SYS_renameat2,dir(),from,dir(),to,1U)); // RENAME_NOREPLACE
#else
    (void)from;(void)to;errno=ENOSYS;return -1;
#endif
  }
  std::optional<StoreProviderError> probe() {
    // Only exclusive harness-style entries; never probe using selected primary.
    const auto a=createArtifact(StoreArtifactKind::Candidate);if(a.error){if(a.artifact)(void)cleanup(*a.artifact);return a.error;}
    const auto b=createArtifact(StoreArtifactKind::PriorCopy);
    if(b.error){if(b.artifact)(void)cleanup(*b.artifact);(void)cleanup(*a.artifact);return b.error;}
    auto* first=slot(*a.artifact);auto* second=slot(*b.artifact);const auto destination=second->name;
    const auto collision=noReplace(first->name.data(),second->name.data());const auto native=errno;
    if(collision==0 || native!=EEXIST) {
      (void)cleanup(*a.artifact);(void)cleanup(*b.artifact);return error(StoreErrorCode::UnsupportedStorage,native);
    }
    if(auto e=cleanup(*b.artifact)){(void)cleanup(*a.artifact);return e;}
    if(noReplace(first->name.data(),destination.data())<0){const auto e=error(StoreErrorCode::UnsupportedStorage,errno);(void)cleanup(*a.artifact);return e;}
    first->name=destination;
    auto result=verify(*first);if(!result)result=sync(first->fd.value);if(!result)result=sync(dir());
    const auto cleaned=cleanup(*a.artifact);if(!result)result=cleaned;if(!result)result=sync(dir());
    return result;
  }
#endif
  std::string parent_,primaryName_;
  LinuxCheckpointLease lease_;
};
#ifdef OPENHDK_ENABLE_TEST_SEAMS
struct LinuxCheckpointProviderTestAccess {
  static void shortIo(LinuxCheckpointProvider& p) noexcept {p.shortIo_=true;p.readInterrupted_=true;p.writeInterrupted_=true;}
  static std::pair<unsigned,unsigned> calls(const LinuxCheckpointProvider& p) noexcept {return {p.readCalls_,p.writeCalls_};}
#ifdef __linux__
  static std::string nextArtifactName() {return ".ohk-stage-"+std::to_string(::getpid())+"-"+std::to_string(LinuxCheckpointProvider::identities_.load()+1);}
  static const char* artifactName(LinuxCheckpointProvider& p,StoreArtifact a) noexcept {auto* s=p.slot(a);return s?s->name.data():nullptr;}
#endif
  static void filesystem(LinuxCheckpointProvider& p) noexcept {p.bypass_=true;}
  static void fault(LinuxCheckpointProvider& p,LinuxProviderFault f) noexcept {p.fault_=f;}
  static void afterPublication(LinuxCheckpointProvider& p,void (*hook)() noexcept) noexcept {p.afterPublication_=hook;}
};
#endif
} // namespace OpenHDK
