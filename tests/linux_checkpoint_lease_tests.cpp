// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/LinuxCheckpointLease.hpp"
#include "tests/TestCheck.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <new>
#include <iostream>
#include <chrono>
#include <thread>
#ifdef __linux__
#include <signal.h>
#include <sys/wait.h>
#include <poll.h>
#endif
namespace { std::ptrdiff_t failAfter=-1; }
#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif
TEST_NOINLINE void* operator new(std::size_t n) {
  if(failAfter==0)throw std::bad_alloc();
  if(failAfter>0)--failAfter;
  if(auto p=std::malloc(n?n:1U))return p;
  throw std::bad_alloc();
}
TEST_NOINLINE void operator delete(void* p) noexcept {std::free(p);}
TEST_NOINLINE void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
#undef TEST_NOINLINE
using namespace OpenHDK;
namespace fs=std::filesystem;
#ifdef __linux__
namespace {
struct Temp {
  fs::path path=fs::temp_directory_path()/("openhdk-lease-"+std::to_string(::getpid())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  Temp(){fs::create_directory(path);}
  ~Temp(){std::error_code ec;fs::remove_all(path,ec);}
};
std::optional<StoreProviderError> acquire(LinuxCheckpointLease& l,const fs::path& p,std::string_view n="catalog.ohkcat") {
  return LinuxCheckpointLeaseTestAccess::acquire(l,p.string(),n);
}
bool code(const std::optional<StoreProviderError>& e,StoreErrorCode c){return e && e->code==c;}
std::size_t fdCount(){return static_cast<std::size_t>(std::distance(fs::directory_iterator("/proc/self/fd"),fs::directory_iterator{}));}
void write(const fs::path& p,std::string_view v){std::ofstream f(p);f<<v;}
std::string read(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
int waitChild(pid_t child) {
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);int status=0;
  while(std::chrono::steady_clock::now()<deadline) {
    const auto result=::waitpid(child,&status,WNOHANG);
    if(result==child)return WIFEXITED(status)?WEXITSTATUS(status):128;
    if(result<0)return 129;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  ::kill(child,SIGKILL);::waitpid(child,&status,0);return 130;
}
}
#endif
int main(int argc,char** argv) {
#ifndef __linux__
  (void)argc;(void)argv;LinuxCheckpointLease l;
  auto unsupported=l.acquire("/unused","catalog.ohkcat");
  OPENHDK_FAIL_IF(1,!unsupported || unsupported->code!=StoreErrorCode::UnsupportedStorage || l.held());
  std::cout<<"non-Linux lease unsupported; no I/O\n";
#else
  if(argc>=3) {
    LinuxCheckpointLease child;auto result=acquire(child,argv[2]);
    if(std::string_view(argv[1])=="--contend")return code(result,StoreErrorCode::Busy)?0:1;
    if(std::string_view(argv[1])=="--hold" && argc==4) {
      if(result)return 2;
      const int pipe=std::stoi(argv[3]);const char ready='R';if(::write(pipe,&ready,1)!=1)return 3;
      for(;;)::pause();
    }
    return 4;
  }
  Temp temp;const auto primary=temp.path/"catalog.ohkcat";const auto lock=temp.path/"catalog.ohkcat.ohk-lock";
  LinuxCheckpointLease production;
  const auto eligible=production.acquire(temp.path.string(),"catalog.ohkcat");
  if(eligible){OPENHDK_FAIL_IF(1,!code(eligible,StoreErrorCode::UnsupportedStorage) || fs::exists(lock));}
  else {OPENHDK_FAIL_IF(2,!production.held());production.release();}
  LinuxCheckpointLease first;OPENHDK_FAIL_IF(3,acquire(first,temp.path) || !first.held() || first.check());
  const auto bytes=read(lock);OPENHDK_FAIL_IF(4,!code(acquire(first,temp.path),StoreErrorCode::Busy) || first.check());
  LinuxCheckpointLease second;OPENHDK_FAIL_IF(5,!code(acquire(second,temp.path),StoreErrorCode::Busy) || !first.held() || read(lock)!=bytes);
  second.release();OPENHDK_FAIL_IF(6,first.check());
  const auto child=::fork();OPENHDK_FAIL_IF(7,child<0);
  if(child==0){::execl("/proc/self/exe","lease-tests","--contend",temp.path.c_str(),static_cast<char*>(nullptr));::_exit(127);}
  OPENHDK_FAIL_IF(8,waitChild(child)!=0 || first.check());
  first.release();OPENHDK_FAIL_IF(9,!fs::exists(lock) || acquire(second,temp.path));second.release();
  // Validate existing lock contents are retained and not truncated.
  write(lock,"lock sentinel");OPENHDK_FAIL_IF(10,acquire(first,temp.path) || read(lock)!="lock sentinel");first.release();
  write(primary,"source sentinel");OPENHDK_FAIL_IF(11,acquire(first,temp.path) || read(primary)!="source sentinel");first.release();
  fs::create_hard_link(primary,temp.path/"alias");OPENHDK_FAIL_IF(12,!code(acquire(first,temp.path),StoreErrorCode::SourceChanged));fs::remove(temp.path/"alias");
  fs::remove(primary);fs::create_symlink("missing",primary);
  OPENHDK_FAIL_IF(13,!code(acquire(first,temp.path),StoreErrorCode::SourceChanged));fs::remove(primary);
  fs::create_directory(primary);OPENHDK_FAIL_IF(14,!code(acquire(first,temp.path),StoreErrorCode::SourceChanged));fs::remove(primary);
  OPENHDK_FAIL_IF(15,::mkfifo(primary.c_str(),0600)!=0 || !code(acquire(first,temp.path),StoreErrorCode::SourceChanged));fs::remove(primary);
  fs::create_hard_link(lock,temp.path/"lock-alias");OPENHDK_FAIL_IF(16,!code(acquire(first,temp.path),StoreErrorCode::SourceChanged));fs::remove(temp.path/"lock-alias");
  fs::remove(lock);fs::create_symlink("missing",lock);OPENHDK_FAIL_IF(17,!code(acquire(first,temp.path),StoreErrorCode::SourceChanged));fs::remove(lock);
  OPENHDK_FAIL_IF(18,::mkfifo(lock.c_str(),0600)!=0 || !code(acquire(first,temp.path),StoreErrorCode::SourceChanged));fs::remove(lock);
  const auto real=temp.path/"real";fs::create_directories(real/"child");fs::create_directory_symlink("real",temp.path/"linked");
  OPENHDK_FAIL_IF(19,!code(acquire(first,temp.path/"linked"/"child"),StoreErrorCode::SourceChanged) || fs::exists(real/"child"/"catalog.ohkcat.ohk-lock"));
  for(auto path:{"relative","//root","/a/../b","/a//b","/a/./b","/a/"}) {
    OPENHDK_FAIL_IF(20,!code(LinuxCheckpointLeaseTestAccess::acquire(first,path,"catalog"),StoreErrorCode::InvalidConfiguration));
  }
  for(auto name:{"","../bad","a/b","a:b","a\\b","thing.ohk-lock"}) {
    OPENHDK_FAIL_IF(21,!code(acquire(first,temp.path,name),StoreErrorCode::InvalidConfiguration));
  }
  LinuxCheckpointLeaseLimits limits;limits.components=0;
  OPENHDK_FAIL_IF(22,!code(LinuxCheckpointLeaseTestAccess::acquire(first,temp.path.string(),"catalog",limits),StoreErrorCode::InvalidConfiguration));
  limits={};limits.pathBytes=temp.path.string().size()-1;
  OPENHDK_FAIL_IF(23,!code(LinuxCheckpointLeaseTestAccess::acquire(first,temp.path.string(),"catalog",limits),StoreErrorCode::InvalidConfiguration));
  OPENHDK_FAIL_IF(24,acquire(first,temp.path));
  fs::rename(lock,temp.path/"old-lock");write(lock,"different lock");
  OPENHDK_FAIL_IF(25,!code(first.check(),StoreErrorCode::SourceChanged));first.release();
  OPENHDK_FAIL_IF(26,read(lock)!="different lock" || !fs::exists(temp.path/"old-lock"));
  const auto dir=temp.path/"parent";fs::create_directory(dir);OPENHDK_FAIL_IF(27,acquire(first,dir));
  fs::rename(dir,temp.path/"old-parent");fs::create_directory(dir);
  OPENHDK_FAIL_IF(28,!code(first.check(),StoreErrorCode::SourceChanged));first.release();
  OPENHDK_FAIL_IF(29,!fs::exists(temp.path/"old-parent"/"catalog.ohkcat.ohk-lock"));
  // Exec creates a fresh process registry. Parent never shares the child's lease.
  const auto childDir=temp.path/"child-owner";fs::create_directory(childDir);int pipes[2];OPENHDK_FAIL_IF(30,::pipe(pipes)!=0);
  const auto owner=::fork();OPENHDK_FAIL_IF(31,owner<0);
  if(owner==0){::close(pipes[0]);const auto pipeName=std::to_string(pipes[1]);::execl("/proc/self/exe","lease-tests","--hold",childDir.c_str(),pipeName.c_str(),static_cast<char*>(nullptr));::_exit(127);}
  ::close(pipes[1]);struct pollfd poller{pipes[0],POLLIN,0};char ready=0;
  const bool handshake=::poll(&poller,1,5000)>0 && ::read(pipes[0],&ready,1)==1 && ready=='R';::close(pipes[0]);
  if(!handshake){::kill(owner,SIGKILL);waitChild(owner);}
  OPENHDK_FAIL_IF(32,!handshake);
  const bool busy=code(acquire(first,childDir),StoreErrorCode::Busy);
  ::kill(owner,SIGKILL);const auto stopped=waitChild(owner);
  OPENHDK_FAIL_IF(33,!busy || stopped!=128 || acquire(first,childDir));first.release();
  OPENHDK_FAIL_IF(34,!fs::exists(childDir/"catalog.ohkcat.ohk-lock"));
  // Exact positive configuration bounds and process registry capacity.
  limits={};limits.pathBytes=temp.path.string().size();limits.nameBytes=1;
  OPENHDK_FAIL_IF(35,LinuxCheckpointLeaseTestAccess::acquire(first,temp.path.string(),"x",limits));
  OPENHDK_FAIL_IF(45,first.ownedBytes()!=temp.path.string().size()+4U+std::string_view(".ohk-lock").size());first.release();
  OPENHDK_FAIL_IF(36,!code(LinuxCheckpointLeaseTestAccess::acquire(first,temp.path.string(),"xx",limits),StoreErrorCode::InvalidConfiguration));
  std::array<LinuxCheckpointLease,64> owners;
  for(std::size_t i=0;i<owners.size();++i) {
    OPENHDK_FAIL_IF(37,acquire(owners[i],temp.path,"slot"+std::to_string(i)));
  }
  OPENHDK_FAIL_IF(38,!code(acquire(first,temp.path,"overflow"),StoreErrorCode::Busy));
  for(auto& ownerLease:owners)ownerLease.release();
  OPENHDK_FAIL_IF(39,acquire(first,temp.path,"overflow") || first.ownedBytes()==0);first.release();
  OPENHDK_FAIL_IF(40,first.ownedBytes()!=0 || !code(first.check(),StoreErrorCode::InvalidConfiguration));
  const auto descriptors=fdCount();
  bool allocationSuccess=false;const auto nativeParent=temp.path.string();const std::string longName(100,'a');
  OPENHDK_FAIL_IF(41,acquire(second,temp.path,"baseline"));
  for(std::ptrdiff_t fault=0;fault<32;++fault) {
    failAfter=fault;auto result=LinuxCheckpointLeaseTestAccess::acquire(first,nativeParent,longName);failAfter=-1;
    if(!result){allocationSuccess=true;first.release();break;}
    OPENHDK_FAIL_IF(42,!code(result,StoreErrorCode::StorageFailure) || first.held() || second.check());
    // Failure must free its reservation without releasing another owner's lease.
    OPENHDK_FAIL_IF(43,acquire(first,temp.path,longName));first.release();
  }
  second.release();OPENHDK_FAIL_IF(44,!allocationSuccess);
  OPENHDK_FAIL_IF(46,fdCount()!=descriptors);
  std::cout<<"Linux lease ownership checks passed; native ext4 publication evidence pending\n";
#endif
}
