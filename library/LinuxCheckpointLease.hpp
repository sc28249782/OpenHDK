// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/CatalogCheckpointStore.hpp"
#include <charconv>
#include <cerrno>
#include <cstdio>
#include <mutex>
#include <utility>
#ifdef __linux__
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace OpenHDK {
struct LinuxCheckpointLeaseLimits {
  std::size_t pathBytes=4096,components=32,nameBytes=128;
};
#ifdef OPENHDK_ENABLE_TEST_SEAMS
struct LinuxCheckpointLeaseTestAccess;
#endif
// Ownership primitive only: not a CheckpointStoreProvider, no checkpoint reads,
// artifacts, publication, synchronization, recovery or native acceptance claim.
// Serialized control path; copies/moves and sharing one lease across fork are forbidden.
class LinuxCheckpointLease {
 public:
  LinuxCheckpointLease()=default;
  ~LinuxCheckpointLease(){release();}
  LinuxCheckpointLease(const LinuxCheckpointLease&)=delete;
  LinuxCheckpointLease& operator=(const LinuxCheckpointLease&)=delete;
  std::optional<StoreProviderError> acquire(std::string_view parent,std::string_view name,
      LinuxCheckpointLeaseLimits limits={}) {return acquireImpl(parent,name,limits,false);}
  bool held() const noexcept {return held_;}
  // Retained logical string bytes; a future provider must charge these together
  // with caller-owned payload. This does not count native handles/map overhead.
  std::size_t ownedBytes() const noexcept {
    auto bytes=path_.size()+name_.size()+lockName_.size();
#ifdef __linux__
    if(key_)bytes+=key_->name.size();
    if(reserved_)bytes+=key_->name.size(); // The registry owns another key copy.
#endif
    return bytes;
  }
  std::optional<StoreProviderError> check() const noexcept {
#ifdef __linux__
    if(!held_)return err(StoreErrorCode::InvalidConfiguration);
    auto observed=walk(path_);if(observed.error)return err(StoreErrorCode::SourceChanged,observed.error->nativeError);
    struct stat parent{},lock{},named{};
    if(::fstat(observed.fd.value,&parent)<0 || ::fstat(lock_.value,&lock)<0 ||
        ::fstatat(directory_.value,lockName_.c_str(),&named,AT_SYMLINK_NOFOLLOW)<0)
      return err(StoreErrorCode::SourceChanged,errno);
    if(identity(parent)!=key_->directory || !S_ISREG(named.st_mode) || named.st_nlink!=1 ||
        identity(named)!=identity(lock))return err(StoreErrorCode::SourceChanged);
    return validateTarget(directory_.value,name_);
#else
    return err(StoreErrorCode::UnsupportedStorage);
#endif
  }
  void release() noexcept {
#ifdef __linux__
    // Never unlink the stable lock. Close only this lease's descriptors.
    lock_.reset();directory_.reset();
    if(reserved_) {
      std::lock_guard<std::mutex> guard(registryMutex_);
      const auto found=registry_.find(*key_);
      if(found!=registry_.end() && found->second==this)registry_.erase(found);
      reserved_=false;
    }
    key_.reset();
#endif
    held_=false;path_.clear();name_.clear();lockName_.clear();
  }
 private:
#ifdef OPENHDK_ENABLE_TEST_SEAMS
  friend struct LinuxCheckpointLeaseTestAccess;
#endif
  static std::optional<StoreProviderError> err(StoreErrorCode code,std::int64_t native=0) noexcept {return StoreProviderError{code,native};}
  static bool valid(std::string_view path,std::string_view name,LinuxCheckpointLeaseLimits l) noexcept {
    if(l.pathBytes==0 || l.pathBytes>4096 || l.components==0 || l.components>32 ||
        l.nameBytes==0 || l.nameBytes>128 || path.empty() || path.front()!='/' ||
        path.size()>l.pathBytes || name.empty() || name.size()>l.nameBytes || name.find('/')!=name.npos ||
        name.ends_with(".ohk-lock") || !isCatalogLocator(name))return false;
    if(path=="/")return true;
    const auto relative=path.substr(1);
    return isCatalogLocator(relative) && 1U+static_cast<std::size_t>(std::count(relative.begin(),relative.end(),'/'))<=l.components;
  }
#ifdef __linux__
  struct Fd {
    int value=-1;
    Fd()=default;explicit Fd(int n):value(n){}
    Fd(Fd&& other) noexcept:value(std::exchange(other.value,-1)){}
    Fd& operator=(Fd&& other) noexcept {if(this!=&other){reset();value=std::exchange(other.value,-1);}return *this;}
    ~Fd(){reset();}
    void reset() noexcept {if(value>=0){::close(value);value=-1;}} // Never retry close.
  };
  struct Identity {std::uint64_t device,inode;auto operator<=>(const Identity&) const=default;};
  struct Key {Identity directory;std::string name;auto operator<=>(const Key&) const=default;};
  struct Walk {Fd fd;std::optional<StoreProviderError> error;};
  static Identity identity(const struct stat& s) noexcept {return {static_cast<std::uint64_t>(s.st_dev),static_cast<std::uint64_t>(s.st_ino)};}
  static Walk walk(std::string_view path) noexcept {
    Fd fd(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC));
    if(fd.value<0)return {std::move(fd),err(StoreErrorCode::StorageFailure,errno)};
    std::size_t start=1;
    while(start<path.size()) {
      auto end=path.find('/',start);if(end==path.npos)end=path.size();
      std::array<char,4097> component{};
      std::copy(path.begin()+static_cast<std::ptrdiff_t>(start),path.begin()+static_cast<std::ptrdiff_t>(end),component.begin());
      Fd next(::openat(fd.value,component.data(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
      if(next.value<0)return {std::move(next),err(errno==ENOENT?StoreErrorCode::NotFound:StoreErrorCode::SourceChanged,errno)};
      fd=std::move(next);start=end+1;
    }
    return {std::move(fd),{}};
  }
  // Mount ID ties the filesystem label to the retained descriptor. A label is
  // eligibility for this primitive, not ext4 durability or provider acceptance.
  static bool ext4(int fd) noexcept {
#if defined(STATX_MNT_ID)
    struct statx sx{};
    if(::statx(fd,"",AT_EMPTY_PATH,STATX_MNT_ID,&sx)<0 || !(sx.stx_mask&STATX_MNT_ID))return false;
    auto* file=std::fopen("/proc/self/mountinfo","re");if(!file)return false;
    char line[8192];std::size_t total=0;bool found=false;
    while(total<1024U*1024U) {
      const auto capacity=std::min(sizeof(line),1024U*1024U-total+1U);
      if(!std::fgets(line,static_cast<int>(capacity),file))break;
      const auto length=std::char_traits<char>::length(line);total+=length;
      if(!length || line[length-1]!='\n')break;
      std::uint64_t id=0;const auto parsed=std::from_chars(line,line+length,id);
      if(parsed.ec!=std::errc{} || parsed.ptr==line+length || *parsed.ptr!=' ')break;
      if(id!=sx.stx_mnt_id)continue;
      const std::string_view row(line,length);const auto separator=row.find(" - ");
      found=separator!=row.npos && row.substr(separator+3).starts_with("ext4 ");break;
    }
    std::fclose(file);return found;
#else
    (void)fd;return false;
#endif
  }
  static std::optional<StoreProviderError> validateTarget(int dir,const std::string& name) noexcept {
    struct stat before{};
    if(::fstatat(dir,name.c_str(),&before,AT_SYMLINK_NOFOLLOW)<0) {
      if(errno==ENOENT)return {};
      return err(StoreErrorCode::StorageFailure,errno);
    }
    if(!S_ISREG(before.st_mode) || before.st_nlink!=1)return err(StoreErrorCode::SourceChanged);
    Fd file(::openat(dir,name.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
    struct stat after{};
    if(file.value<0 || ::fstat(file.value,&after)<0)return err(StoreErrorCode::SourceChanged,errno);
    if(!S_ISREG(after.st_mode) || after.st_nlink!=1 || identity(before)!=identity(after))return err(StoreErrorCode::SourceChanged);
    return {};
  }
  inline static std::mutex registryMutex_;
  inline static std::map<Key,const LinuxCheckpointLease*> registry_; // Maximum 64 active owners.
  std::optional<Key> key_;
  Fd directory_,lock_;
  bool reserved_=false;
#endif
  std::optional<StoreProviderError> acquireImpl(std::string_view parent,std::string_view name,
      LinuxCheckpointLeaseLimits limits,bool testFilesystem) {
    if(held_)return err(StoreErrorCode::Busy);
    if(!valid(parent,name,limits))return err(StoreErrorCode::InvalidConfiguration);
#ifndef __linux__
    (void)testFilesystem;return err(StoreErrorCode::UnsupportedStorage);
#else
    try {
      path_=parent;name_=name;lockName_=name_+".ohk-lock";
      auto result=walk(path_);if(result.error){release();return result.error;}
      directory_=std::move(result.fd);
      if(!testFilesystem && !ext4(directory_.value)){release();return err(StoreErrorCode::UnsupportedStorage);}
      if(auto e=validateTarget(directory_.value,name_)){release();return e;}
      struct stat parentInfo{};
      if(::fstat(directory_.value,&parentInfo)<0){const auto e=err(StoreErrorCode::StorageFailure,errno);release();return e;}
      Key key{identity(parentInfo),name_};
      {
        std::unique_lock<std::mutex> guard(registryMutex_,std::try_to_lock);
        if(!guard.owns_lock()){release();return err(StoreErrorCode::Busy);}
        if(registry_.contains(key) || registry_.size()>=64) {
          guard.unlock();release();return err(StoreErrorCode::Busy);
        }
        // Prepare key allocation before registry publication; cleanup can erase
        // a reservation even when a later native open fails.
        key_=key;registry_.emplace(key,this);reserved_=true;
      }
      lock_=Fd(::openat(directory_.value,lockName_.c_str(),O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC,0600));
      struct stat priorLock{};bool existing=false;
      if(lock_.value<0 && errno==EEXIST) {
        if(::fstatat(directory_.value,lockName_.c_str(),&priorLock,AT_SYMLINK_NOFOLLOW)<0) {
          const auto e=err(StoreErrorCode::SourceChanged,errno);release();return e;
        }
        if(!S_ISREG(priorLock.st_mode) || priorLock.st_nlink!=1){release();return err(StoreErrorCode::SourceChanged);}
        existing=true;
        lock_=Fd(::openat(directory_.value,lockName_.c_str(),O_RDWR|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
      }
      struct stat info{};
      if(lock_.value<0 || ::fstat(lock_.value,&info)<0) {
        const auto e=err(StoreErrorCode::SourceChanged,errno);release();return e;
      }
      if(!S_ISREG(info.st_mode) || info.st_nlink!=1 || (existing && identity(info)!=identity(priorLock))){release();return err(StoreErrorCode::SourceChanged);}
      if(::flock(lock_.value,LOCK_EX|LOCK_NB)<0) {
        const auto e=err(errno==EWOULDBLOCK?StoreErrorCode::Busy:StoreErrorCode::StorageFailure,errno);release();return e;
      }
      held_=true;
      if(auto e=check()){release();return e;}
      return {};
    } catch(const std::bad_alloc&) {release();return err(StoreErrorCode::StorageFailure);}
#endif
  }
  std::string path_,name_,lockName_;
  bool held_=false;
};
#ifdef OPENHDK_ENABLE_TEST_SEAMS
struct LinuxCheckpointLeaseTestAccess {
  static std::optional<StoreProviderError> acquire(LinuxCheckpointLease& l,std::string_view path,
      std::string_view name,LinuxCheckpointLeaseLimits limits={}) {return l.acquireImpl(path,name,limits,true);}
};
#endif
} // namespace OpenHDK
