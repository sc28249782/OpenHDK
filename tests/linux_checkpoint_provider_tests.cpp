// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/LinuxCheckpointProvider.hpp"
#include "tests/TestCheck.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#ifdef __linux__
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#endif
#include <cstdlib>
namespace { std::ptrdiff_t failAfter=-1; }
#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif
TEST_NOINLINE void* operator new(std::size_t n) {
  if(failAfter==0) throw std::bad_alloc();
  if(failAfter>0) --failAfter;
  if(auto p=std::malloc(n?n:1U)) return p;
  throw std::bad_alloc();
}
TEST_NOINLINE void operator delete(void* p) noexcept {std::free(p);}
TEST_NOINLINE void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
#undef TEST_NOINLINE

using namespace OpenHDK;
namespace fs=std::filesystem;
#ifdef __linux__
static bool nativeRun=false;
static int marker=-1;
static void forbidAllocation() noexcept {failAfter=0;}
static void cut() noexcept {const char byte='P';const auto written=::write(marker,&byte,1);if(written!=1)::_exit(98);for(;;)::pause();}
static bool waitChild(pid_t pid,int& status) {
  for(unsigned n=0;n<500;++n){if(::waitpid(pid,&status,WNOHANG)==pid)return true;::usleep(10000);}
  (void)::kill(pid,SIGKILL);(void)::waitpid(pid,&status,0);return false;
}
static CheckpointBytes disk(const fs::path& p) {std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static CatalogCheckpointProjection projection(std::uint64_t revision=0) {
  CatalogCheckpointProjection p;p.sequence=1;p.catalogRevision=revision;p.nextRoot=1;p.nextSong=1;return p;
}
static void enable(LinuxCheckpointProvider& p){if(!nativeRun)LinuxCheckpointProviderTestAccess::filesystem(p);}
#endif
int main(int argc,char** argv) {
#ifndef __linux__
  (void)argc;(void)argv;LinuxCheckpointProvider p("/unused","catalog");
  OPENHDK_FAIL_IF(1,p.acquire()->code!=StoreErrorCode::UnsupportedStorage);
  OPENHDK_FAIL_IF(2,p.readPrimary(96).error->code!=StoreErrorCode::UnsupportedStorage);
  std::cout<<"Non-Linux provider unsupported; no I/O.\n";
#else
  nativeRun=std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR")!=nullptr;
  if(argc==5 && std::string_view(argv[1])=="--cut") {
    marker=std::stoi(argv[3]);LinuxCheckpointProvider p(argv[2],"catalog");enable(p);
    CatalogCheckpointStore store(p);auto opened=store.open();if(!opened.succeeded())return 90;
    const int phase=std::stoi(argv[4]);StoreControl control;
    if(phase==2)LinuxCheckpointProviderTestAccess::afterPublication(p,cut);
    if(phase<2)control.checkpoint=[phase](StoreCheckpoint at) {
      if((phase==0 && at==StoreCheckpoint::CandidateVerified) || (phase==1 && at==StoreCheckpoint::PriorVerified))cut();
    };
    auto saved=store.save(projection(opened.projection?opened.projection->catalogRevision+1:0),*opened.expectation,control);
    if(phase==3 && saved.succeeded())cut();
    return saved.succeeded()?91:92;
  }
  std::string tmp=nativeRun?std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR"):"/tmp";
  tmp+="/openhdk-provider-XXXXXX";auto* made=::mkdtemp(tmp.data());OPENHDK_FAIL_IF(1,!made);
  const fs::path root(made);struct Cleanup {fs::path p;~Cleanup(){fs::remove_all(p);}} cleanup{root};
  LinuxCheckpointProvider production(root.string(),"production");
  const auto eligibility=production.acquire();
  if(nativeRun)OPENHDK_FAIL_IF(2,eligibility.has_value());
  else if(eligibility)OPENHDK_FAIL_IF(2,eligibility->code!=StoreErrorCode::UnsupportedStorage);
  struct utsname system{};OPENHDK_FAIL_IF(53,::uname(&system)<0);
  std::cout<<"Environment kernel="<<system.release<<" compiler="<<__VERSION__<<" parent="<<root
      <<" ext4-eligibility="<<(!eligibility)<<" test-filesystem-bypass="<<(!nativeRun)<<std::endl;
  production.release();
  const auto reservedPrimary=LinuxCheckpointProviderTestAccess::nextArtifactName();
  LinuxCheckpointProvider namespaceCollision(root.string(),reservedPrimary);enable(namespaceCollision);
  OPENHDK_FAIL_IF(56,namespaceCollision.acquire()->code!=StoreErrorCode::InvalidConfiguration || fs::exists(root/reservedPrimary));namespaceCollision.release();
  LinuxCheckpointProvider p(root.string(),"catalog");enable(p);CatalogCheckpointStore store(p);
  auto opened=store.open();OPENHDK_FAIL_IF(3,!opened.succeeded() || opened.projection);
  OPENHDK_FAIL_IF(4,p.acquire()->code!=StoreErrorCode::Busy);
  LinuxCheckpointProvider contender(root.string(),"catalog");enable(contender);
  OPENHDK_FAIL_IF(5,contender.acquire()->code!=StoreErrorCode::Busy);
  auto first=store.save(projection(),*opened.expectation);OPENHDK_FAIL_IF(6,!first.succeeded());
  const auto original=disk(root/"catalog");OPENHDK_FAIL_IF(7,original.size()!=96 || !decodeCatalogCheckpoint(original).succeeded());
  auto unchanged=store.save(projection(),*first.expectation);OPENHDK_FAIL_IF(8,!unchanged.succeeded() || unchanged.status!=StoreSaveStatus::Unchanged);
  auto second=store.save(projection(1),*first.expectation);OPENHDK_FAIL_IF(9,!second.succeeded() || second.expectation->token()->sequence!=2);
  OPENHDK_FAIL_IF(10,store.save(projection(2),*first.expectation).error->code!=StoreErrorCode::StaleCheckpoint);
  OPENHDK_FAIL_IF(11,store.close().has_value());
  opened=store.open();OPENHDK_FAIL_IF(12,!opened.succeeded() || opened.projection->catalogRevision!=1);
  const auto stable=disk(root/"catalog");
  LinuxCheckpointProviderTestAccess::shortIo(p);

  for(const auto fault:{LinuxProviderFault::Write,LinuxProviderFault::ReadArtifact,LinuxProviderFault::SyncArtifact}) {
    LinuxCheckpointProviderTestAccess::fault(p,fault);
    auto failed=store.save(projection(2),*opened.expectation);
    OPENHDK_FAIL_IF(13,failed.succeeded() || !failed.error || failed.error->outcome!=StoreOutcome::NotCommitted || store.faulted() || disk(root/"catalog")!=stable);
  }
  LinuxCheckpointProviderTestAccess::fault(p,LinuxProviderFault::Publish);
  auto uncertain=store.save(projection(2),*opened.expectation);
  OPENHDK_FAIL_IF(14,uncertain.succeeded() || uncertain.error->code!=StoreErrorCode::CommitUncertain || !store.faulted() || !uncertain.prior);
  OPENHDK_FAIL_IF(15,store.save(projection(3),*opened.expectation).error->code!=StoreErrorCode::CommitUncertain);
  LinuxCheckpointProviderTestAccess::fault(p,LinuxProviderFault::None);(void)store.close();opened=store.open();
  OPENHDK_FAIL_IF(16,!opened.succeeded() || store.faulted() || opened.projection->catalogRevision!=1);
  LinuxCheckpointProviderTestAccess::fault(p,LinuxProviderFault::SyncPublication);
  uncertain=store.save(projection(2),*opened.expectation);
  OPENHDK_FAIL_IF(17,uncertain.succeeded() || uncertain.error->operation!=StoreOperation::SyncPublication || decodeCatalogCheckpoint(disk(root/"catalog")).value->catalogRevision!=2);
  LinuxCheckpointProviderTestAccess::fault(p,LinuxProviderFault::None);(void)store.close();opened=store.open();
  OPENHDK_FAIL_IF(18,!opened.succeeded() || opened.projection->catalogRevision!=2);
  LinuxCheckpointProviderTestAccess::fault(p,LinuxProviderFault::Cleanup);
  auto warned=store.save(projection(3),*opened.expectation);
  OPENHDK_FAIL_IF(19,!warned.succeeded() || !warned.cleanupWarning || store.faulted());
  LinuxCheckpointProviderTestAccess::fault(p,LinuxProviderFault::None);(void)store.close();opened=store.open();
  OPENHDK_FAIL_IF(20,!opened.succeeded() || opened.projection->catalogRevision!=3);
  (void)store.close();
  const auto calls=LinuxCheckpointProviderTestAccess::calls(p);
  OPENHDK_FAIL_IF(44,calls.first<14 || calls.second<14);
  // Foreign, consumed and previous-lease capabilities cannot delete the primary.
  OPENHDK_FAIL_IF(21,p.acquire().has_value());auto a=p.createArtifact(StoreArtifactKind::Candidate);
  OPENHDK_FAIL_IF(22,!a.artifact || p.writeArtifact(*a.artifact,original).has_value());
  OPENHDK_FAIL_IF(23,p.readArtifact(*a.artifact,95).error->code!=StoreErrorCode::LimitExceeded);
  OPENHDK_FAIL_IF(24,p.readArtifact(*a.artifact,96).bytes!=original);
  LinuxCheckpointProvider other(root.string(),"other");enable(other);OPENHDK_FAIL_IF(25,other.acquire().has_value());
  OPENHDK_FAIL_IF(26,other.cleanup(*a.artifact)->code!=StoreErrorCode::InvalidConfiguration);other.release();
  OPENHDK_FAIL_IF(27,p.cleanup(*a.artifact).has_value());
  OPENHDK_FAIL_IF(28,p.cleanup(*a.artifact)->code!=StoreErrorCode::InvalidConfiguration);
  auto b=p.createArtifact(StoreArtifactKind::Candidate);OPENHDK_FAIL_IF(29,p.writeArtifact(*b.artifact,original).has_value());
  OPENHDK_FAIL_IF(30,p.publish(*b.artifact,true).outcome!=StorePublication::NotCommitted); // existing target untouched
  OPENHDK_FAIL_IF(31,p.cleanup(*b.artifact).has_value());p.release();
  // Identity mismatch preserves unrelated replacement artifact.
  OPENHDK_FAIL_IF(45,p.acquire().has_value());auto replaced=p.createArtifact(StoreArtifactKind::Candidate);
  const fs::path artifact=root/LinuxCheckpointProviderTestAccess::artifactName(p,*replaced.artifact);
  fs::rename(artifact,root/"retained-stage");{std::ofstream f(artifact);f<<"unrelated";}
  OPENHDK_FAIL_IF(46,p.cleanup(*replaced.artifact)->code!=StoreErrorCode::SourceChanged || disk(artifact)!=CheckpointBytes({'u','n','r','e','l','a','t','e','d'}));
  p.release();
  OPENHDK_FAIL_IF(47,p.acquire().has_value());
  OPENHDK_FAIL_IF(48,p.cleanup(*replaced.artifact)->code!=StoreErrorCode::InvalidConfiguration);p.release();
  // Reconciliation failure cannot acknowledge an open; missing primary after uncertainty stays missing.
  LinuxCheckpointProviderTestAccess::fault(p,LinuxProviderFault::Reconcile);
  OPENHDK_FAIL_IF(32,store.open().succeeded());LinuxCheckpointProviderTestAccess::fault(p,LinuxProviderFault::None);
  opened=store.open();OPENHDK_FAIL_IF(33,!opened.succeeded());
  LinuxCheckpointProviderTestAccess::fault(p,LinuxProviderFault::Publish);
  uncertain=store.save(projection(4),*opened.expectation);OPENHDK_FAIL_IF(34,!store.faulted());
  LinuxCheckpointProviderTestAccess::fault(p,LinuxProviderFault::None);(void)store.close();
  fs::rename(root/"catalog",root/"saved-primary");
  auto missing=store.open();OPENHDK_FAIL_IF(35,missing.succeeded() || missing.error->code!=StoreErrorCode::NotFound);
  fs::rename(root/"saved-primary",root/"catalog");opened=store.open();OPENHDK_FAIL_IF(36,!opened.succeeded());(void)store.close();
  // Actual subprocess interruption after rename/before directory sync. Fresh exec has no inherited registry.
  for(unsigned phase=0;phase<4;++phase)for(unsigned repeat=0;repeat<2;++repeat) {
    const auto previous=decodeCatalogCheckpoint(disk(root/"catalog")).value->catalogRevision;
    int pipes[2];OPENHDK_FAIL_IF(37,::pipe(pipes)<0);
    const auto fd=std::to_string(pipes[1]);const auto phaseText=std::to_string(phase);const auto pid=::fork();OPENHDK_FAIL_IF(38,pid<0);
    if(pid==0){::close(pipes[0]);::execl("/proc/self/exe","provider-test","--cut",root.c_str(),fd.c_str(),phaseText.c_str(),nullptr);::_exit(99);}
    ::close(pipes[1]);pollfd event{pipes[0],POLLIN,0};const auto ready=::poll(&event,1,5000);char byte=0;
    const auto count=ready>0?::read(pipes[0],&byte,1):0;::close(pipes[0]);(void)::kill(pid,SIGKILL);int status=0;const bool exited=waitChild(pid,status);
    OPENHDK_FAIL_IF(39,count!=1 || byte!='P' || !exited || !WIFSIGNALED(status));
    opened=store.open();OPENHDK_FAIL_IF(40,!opened.succeeded() || !opened.projection ||
        opened.projection->catalogRevision!=previous+(phase>=2?1U:0U));(void)store.close();
    std::cout<<"Interruption phase="<<phase<<" repeat="<<repeat<<" revision="<<previous+(phase>=2?1U:0U)<<std::endl;
  }
  // Provider retention shares the operation budget: it is not a second allowance.
  CatalogCheckpointLimits tiny;tiny.stagedBytes=1;
  OPENHDK_FAIL_IF(41,store.open(tiny).error->code!=StoreErrorCode::LimitExceeded);
  opened=store.open();OPENHDK_FAIL_IF(42,!opened.succeeded());
  LinuxCheckpointProviderTestAccess::afterPublication(p,forbidAllocation);
  auto allocationFree=store.save(projection(opened.projection->catalogRevision+1),*opened.expectation);
  failAfter=-1;LinuxCheckpointProviderTestAccess::afterPublication(p,nullptr);
  OPENHDK_FAIL_IF(49,!allocationFree.succeeded());(void)store.close();
  // Staging allocation failures return no acknowledgment and preserve bytes.
  const auto beforeSweep=disk(root/"catalog");unsigned failures=0;
  opened=store.open();OPENHDK_FAIL_IF(50,!opened.succeeded());
  for(std::ptrdiff_t fault=0;fault<12;++fault) {
    failAfter=fault;auto attempted=store.save(projection(opened.projection->catalogRevision+1),*opened.expectation);failAfter=-1;
    if(attempted.succeeded()){opened.expectation=attempted.expectation;break;}
    ++failures;OPENHDK_FAIL_IF(51,!attempted.error || attempted.error->code!=StoreErrorCode::StorageFailure || disk(root/"catalog")!=beforeSweep || store.faulted());
  }
  OPENHDK_FAIL_IF(52,failures==0);(void)store.close();
  fs::create_symlink(root/"catalog",root/"alias");LinuxCheckpointProvider alias(root.string(),"alias");enable(alias);
  OPENHDK_FAIL_IF(43,alias.acquire()->code!=StoreErrorCode::SourceChanged);
  const auto savedPrimary=disk(root/"catalog");{std::ofstream bad(root/"catalog",std::ios::binary);bad<<"invalid";}
  const auto corrupt=store.open();OPENHDK_FAIL_IF(54,corrupt.succeeded() || corrupt.error->code!=StoreErrorCode::InvalidCheckpoint);
  {std::ofstream restored(root/"catalog",std::ios::binary);restored.write(reinterpret_cast<const char*>(savedPrimary.data()),static_cast<std::streamsize>(savedPrimary.size()));}
  opened=store.open();OPENHDK_FAIL_IF(55,!opened.succeeded());(void)store.close();
  std::cout<<"Linux provider syscall/coordinator tests passed; native ext4 acceptance requires recorded review.\n";
#endif
}
