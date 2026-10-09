// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/LinuxCheckpointProvider.hpp"
#include "tests/TestCheck.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#ifdef __linux__
#include <chrono>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#endif
namespace { bool forbidNew=false; }
#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif
TEST_NOINLINE void* operator new(std::size_t n) {
  if(forbidNew)throw std::bad_alloc();
  if(auto p=std::malloc(n?n:1))return p;
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
using Access=LinuxCheckpointProviderTestAccess;
struct CutPacket {
  std::uint32_t magic=0x4f484b52;std::size_t count=0;bool overflow=false;
  std::array<LinuxProviderReceipt,256> records{};
};
static_assert(std::is_trivially_copyable_v<CutPacket>);
CutPacket childPacket;
LinuxCheckpointProvider* childProvider=nullptr;
int childPipe=-1;
bool nativeMode=false;
void enable(LinuxCheckpointProvider& p){if(!nativeMode)Access::filesystem(p);Access::receipts(p);}
void hex(std::span<const std::uint8_t> b) {
  constexpr char digits[]="0123456789abcdef";
  for(auto x:b)std::cout<<digits[x>>4]<<digits[x&15];
}
const char* callName(LinuxProviderCall c) {
  switch(c) {
    case LinuxProviderCall::OpenArtifact:return "openat-artifact";
    case LinuxProviderCall::OpenRead:return "openat-read";
    case LinuxProviderCall::Write:return "pwrite";
    case LinuxProviderCall::Read:return "pread";
    case LinuxProviderCall::SyncFile:return "fsync-file";
    case LinuxProviderCall::SyncDirectory:return "fsync-parent";
    case LinuxProviderCall::RenameNoReplace:return "renameat2-noreplace";
    case LinuxProviderCall::RenameReplace:return "renameat";
    case LinuxProviderCall::Unlink:return "unlinkat-owned-stage";
    case LinuxProviderCall::CleanupSnapshot:return "pre-cleanup-fstat";
    case LinuxProviderCall::InjectedBoundary:return "injected-provider-boundary";
  }
  return "unknown";
}
void wireReceipt(std::string_view tag,std::span<const std::uint8_t> bytes) {
  std::cout<<"WIRE tag="<<tag<<" bytes="<<bytes.size()<<" full_sha256=";
  hex(sourceRevision(bytes).sha256);std::cout<<" hex=";hex(bytes);std::cout<<'\n';
  const auto d=decodeCatalogCheckpoint(bytes);
  if(!d.succeeded()){std::cout<<"DECODE tag="<<tag<<" error="<<static_cast<int>(d.error->code)<<'\n';return;}
  const auto& p=*d.value;
  std::cout<<"PROJECTION tag="<<tag<<" sequence="<<p.sequence<<" revision="<<p.catalogRevision
      <<" nextRoot="<<p.nextRoot<<" nextSong="<<p.nextSong<<" roots="<<p.roots.size()<<" songs="<<p.songs.size()<<" token_digest=";
  hex(bytes.last(32));std::cout<<'\n';
  // Full canonical wire above represents every record/UTF-8 field exactly.
}
bool records(std::string_view tag,std::span<const LinuxProviderReceipt> events,bool overflow) {
  std::cout<<"TRACE tag="<<tag<<" count="<<events.size()<<" overflow="<<overflow<<'\n';
  for(const auto& e:events) {
    if(e.result>=0 && e.nativeError!=0)return false;
    std::cout<<"NATIVE tag="<<tag<<" call="<<callName(e.call)<<" rc="<<e.result<<" errno="<<e.nativeError
        <<" injected="<<e.injected<<" fd="<<e.fd<<" capability_or_fault="<<e.capability<<'\n';
    if(e.call==LinuxProviderCall::CleanupSnapshot) {
      std::cout<<"ARTIFACT tag="<<tag<<" name="<<e.name.data()<<" capability="<<e.capability
          <<" dev="<<e.device<<" ino="<<e.inode<<" links="<<e.links<<" bytes="<<e.bytes<<" consumed="<<e.consumed
          <<" captured="<<e.capturedBytes<<" capture_errno="<<e.captureError<<'\n';
      if(e.capturedBytes>=0)wireReceipt(e.name.data(),{e.wire.data(),static_cast<std::size_t>(e.capturedBytes)});
      if(e.result!=0 || e.capturedBytes!=e.bytes || e.captureError)return false;
    }
  }
  return !overflow;
}
bool trace(std::string_view tag,LinuxCheckpointProvider& p) {
  const bool okay=records(tag,Access::records(p),Access::overflow(p));Access::receipts(p);return okay;
}
// Inspect only tiny synthetic fixture entries before harness deletion. Never
// follow symlinks, and report malformed/absent data rather than select a backup.
bool inspect(const fs::path& path,std::string_view tag) {
  struct stat before{};
  if(::lstat(path.c_str(),&before)<0){std::cout<<"FILE tag="<<tag<<" lstat_rc=-1 errno="<<errno<<'\n';return errno==ENOENT;}
  std::cout<<"FILE tag="<<tag<<" dev="<<before.st_dev<<" ino="<<before.st_ino<<" links="<<before.st_nlink<<" bytes="<<before.st_size<<'\n';
  if(!S_ISREG(before.st_mode)){std::cout<<"FILE tag="<<tag<<" regular=0 bytes-not-read\n";return true;}
  if(before.st_size<0 || before.st_size>256)return false;
  const int fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)return false;
  struct stat after{};std::array<std::uint8_t,256> b{};
  const bool same=::fstat(fd,&after)==0 && before.st_dev==after.st_dev && before.st_ino==after.st_ino;
  const auto n=::pread(fd,b.data(),static_cast<std::size_t>(before.st_size),0);const int code=n<0?errno:0;::close(fd);
  std::cout<<"FILE tag="<<tag<<" pread_rc="<<n<<" errno="<<code<<" injected=0\n";
  if(!same || n!=before.st_size)return false;
  wireReceipt(tag,{b.data(),static_cast<std::size_t>(n)});return true;
}
bool namespaceReceipt(const fs::path& root,std::string_view tag) {
  if(!inspect(root/"catalog",tag))return false;
  std::vector<fs::path> names;
  for(const auto& e:fs::directory_iterator(root)) {
    if(names.size()==64)return false;
    if(e.path().filename().string().starts_with(".ohk-stage-"))names.push_back(e.path());
  }
  std::sort(names.begin(),names.end());
  std::cout<<"NAMESPACE tag="<<tag<<" stages="<<names.size()<<" capability-source=unavailable-after-owner-exit\n";
  for(const auto& n:names)if(!inspect(n,n.filename().string()))return false;
  return true;
}
CatalogCheckpointProjection fixture(std::uint64_t revision) {
  CatalogCheckpointProjection p;p.catalogRevision=revision;p.nextRoot=2;p.nextSong=2;
  p.roots.push_back({1,1,{},std::string("/synthetic")});
  p.songs.push_back({1,1,SourceMemberRole::PrimaryMidi,"song.kar",std::string("Title"),std::string("Artist")});return p;
}
void saveReceipt(std::string_view tag,const StoreSaveResult& r) {
  std::cout<<"SAVE tag="<<tag<<" success="<<r.succeeded()<<" status="<<(r.status?static_cast<int>(*r.status):-1)
      <<" captured_revision="<<r.capturedRevision.value_or(UINT64_MAX)<<" warning="<<r.cleanupWarning.has_value();
  if(r.error)std::cout<<" error="<<static_cast<int>(r.error->code)<<" operation="<<static_cast<int>(r.error->operation)
      <<" outcome="<<static_cast<int>(r.error->outcome)<<" native_errno="<<r.error->nativeError;
  std::cout<<'\n';
  if(r.expectation && r.expectation->token()) {
    std::cout<<"ACK tag="<<tag<<" sequence="<<r.expectation->token()->sequence<<" digest=";
    hex(r.expectation->token()->digest);std::cout<<'\n';
  }
}
void cut() noexcept {
  const auto span=Access::records(*childProvider);childPacket.count=span.size();childPacket.overflow=Access::overflow(*childProvider);
  std::copy(span.begin(),span.end(),childPacket.records.begin());
  // Fixed pipe transport in the test-only cut hook; parent drains with deadline.
  // No application allocation/output formatting occurs in publication zone.
  const auto* b=reinterpret_cast<const char*>(&childPacket);std::size_t at=0;
  while(at<sizeof(childPacket)) {
    const auto n=::write(childPipe,b+at,sizeof(childPacket)-at);
    if(n<0 && errno==EINTR)continue;
    if(n<=0)::_exit(98);
    at+=static_cast<std::size_t>(n);
  }
  for(;;)::pause();
}
void allocationCut() noexcept {forbidNew=true;}
bool receive(int fd,CutPacket& packet) {
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  auto* b=reinterpret_cast<char*>(&packet);std::size_t at=0;
  while(at<sizeof(packet)) {
    const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
    if(left<=0)return false;
    pollfd p{fd,POLLIN,0};const int rc=::poll(&p,1,static_cast<int>(left));
    if(rc<0 && errno==EINTR)continue;
    if(rc<=0)return false;
    const auto n=::read(fd,b+at,sizeof(packet)-at);if(n<0 && errno==EINTR)continue;
    if(n<=0)return false;
    at+=static_cast<std::size_t>(n);
  }
  return packet.magic==0x4f484b52 && packet.count<=packet.records.size();
}
bool reap(pid_t pid,int& status) {
  for(unsigned n=0;n<500;++n){const auto rc=::waitpid(pid,&status,WNOHANG);if(rc==pid)return true;if(rc<0)return false;::usleep(10000);}
  return false;
}
}
#endif
int main(int argc,char** argv) {
#ifndef __linux__
  (void)argc;(void)argv;std::cout<<"EVIDENCE platform=non-linux status=Skipped native-ext4-required\n";return 0;
#else
  nativeMode=std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR")!=nullptr;
  if(argc==5 && std::string_view(argv[1])=="--cut") {
    childPipe=std::stoi(argv[3]);const auto phase=std::stoi(argv[4]);
    LinuxCheckpointProvider p(argv[2],"catalog");childProvider=&p;enable(p);CatalogCheckpointStore store(p);
    auto opened=store.open();if(!opened.succeeded())return 90;
    StoreControl control;
    if(phase==2)Access::afterPublication(p,cut);
    if(phase<2)control.checkpoint=[phase](StoreCheckpoint at) {
      if((phase==0 && at==StoreCheckpoint::CandidateVerified)||(phase==1 && at==StoreCheckpoint::PriorVerified))cut();
    };
    auto saved=store.save(fixture(opened.projection->catalogRevision+1),*opened.expectation,control);
    if(phase==3 && saved.succeeded())cut();
    return 91;
  }
  std::string name=nativeMode?std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR"):"/tmp";name+="/openhdk-receipt-fixture-XXXXXX";
  const auto* made=::mkdtemp(name.data());OPENHDK_FAIL_IF(1,!made);
  const fs::path root(made);struct Cleanup {fs::path root;~Cleanup(){fs::remove_all(root);}} cleanup{root};
  std::cout<<"EVIDENCE schema=1 filesystem-bypass="<<(!nativeMode)<<" parent="<<root<<" seed=none deterministic-fixtures\n";
  LinuxCheckpointProvider p(root.string(),"catalog");enable(p);CatalogCheckpointStore store(p);
  auto opened=store.open();OPENHDK_FAIL_IF(2,!opened.succeeded() || !trace("open-probe-reconcile",p));
  auto saved=store.save(fixture(1),*opened.expectation);saveReceipt("create",saved);
  OPENHDK_FAIL_IF(3,!saved.succeeded() || !trace("create",p) || !namespaceReceipt(root,"create"));
  Access::afterPublication(p,allocationCut);
  auto updated=store.save(fixture(2),*saved.expectation);forbidNew=false;Access::afterPublication(p,nullptr);
  saveReceipt("update-no-allocation",updated);
  OPENHDK_FAIL_IF(4,!updated.succeeded() || !trace("update-no-allocation",p) || !namespaceReceipt(root,"update"));
  for(const auto fault:{LinuxProviderFault::Write,LinuxProviderFault::ReadArtifact,LinuxProviderFault::SyncArtifact,
      LinuxProviderFault::Publish,LinuxProviderFault::SyncPublication,LinuxProviderFault::Cleanup}) {
    const auto tag="fault-"+std::to_string(static_cast<int>(fault));Access::fault(p,fault);
    auto attempt=store.save(fixture(3+static_cast<std::uint64_t>(fault)),*updated.expectation);saveReceipt(tag,attempt);
    OPENHDK_FAIL_IF(5,!trace(tag,p) || !namespaceReceipt(root,tag));
    OPENHDK_FAIL_IF(6,fault==LinuxProviderFault::Cleanup?!attempt.succeeded()||!attempt.cleanupWarning:attempt.succeeded());
    Access::fault(p,LinuxProviderFault::None);(void)store.close();opened=store.open();
    OPENHDK_FAIL_IF(7,!opened.succeeded() || !trace("explicit-reconcile",p));
    // Keep exact acknowledged token rather than derive one from observed wire.
    updated.expectation=opened.expectation;
  }
  Access::fault(p,LinuxProviderFault::Reconcile);(void)store.close();const auto rejected=store.open();
  std::cout<<"OPEN tag=reconcile-fault success="<<rejected.succeeded()<<" errno="<<(rejected.error?rejected.error->nativeError:0)<<'\n';
  OPENHDK_FAIL_IF(8,rejected.succeeded() || !trace("reconcile-fault",p));Access::fault(p,LinuxProviderFault::None);
  OPENHDK_FAIL_IF(9,p.acquire().has_value() || !trace("collision-probe",p));
  auto artifact=p.createArtifact(StoreArtifactKind::Candidate);auto bytes=encodeCatalogCheckpoint(fixture(10));
  OPENHDK_FAIL_IF(10,!artifact.artifact || !bytes.succeeded() || p.writeArtifact(*artifact.artifact,*bytes.value).has_value());
  const auto collision=p.publish(*artifact.artifact,true);
  std::cout<<"PUBLISH tag=existing-target-create outcome="<<static_cast<int>(collision.outcome)<<" native_errno="<<(collision.error?collision.error->nativeError:0)<<'\n';
  OPENHDK_FAIL_IF(11,collision.outcome!=StorePublication::NotCommitted || !collision.error || collision.error->nativeError!=EEXIST);
  OPENHDK_FAIL_IF(12,p.cleanup(*artifact.artifact).has_value() || !trace("existing-target-create",p));p.release();
  for(unsigned phase=0;phase<4;++phase)for(unsigned repeat=0;repeat<2;++repeat) {
    opened=store.open();OPENHDK_FAIL_IF(13,!opened.succeeded() || !trace("before-child-open",p));
    const auto priorRevision=opened.projection->catalogRevision;(void)store.close();
    OPENHDK_FAIL_IF(13,!namespaceReceipt(root,"before-child"));
    int pipe[2];OPENHDK_FAIL_IF(14,::pipe(pipe)<0);
    const auto fd=std::to_string(pipe[1]),point=std::to_string(phase);const auto pid=::fork();OPENHDK_FAIL_IF(15,pid<0);
    if(pid==0){::close(pipe[0]);::execl("/proc/self/exe","receipt-test","--cut",root.c_str(),fd.c_str(),point.c_str(),nullptr);::_exit(99);}
    ::close(pipe[1]);CutPacket packet;const bool got=receive(pipe[0],packet);::close(pipe[0]);
    const int killed=::kill(pid,SIGKILL);int status=0;const bool exited=reap(pid,status);
    std::cout<<"CHILD phase="<<phase<<" repeat="<<repeat<<" pid="<<pid<<" receipt_complete="<<got<<" kill_rc="<<killed
        <<" wait_complete="<<exited<<" wait_status="<<status<<'\n';
    OPENHDK_FAIL_IF(16,!got || killed!=0 || !exited || !WIFSIGNALED(status) || WTERMSIG(status)!=SIGKILL);
    OPENHDK_FAIL_IF(17,!records("child-before-termination",{packet.records.data(),packet.count},packet.overflow));
    OPENHDK_FAIL_IF(18,!namespaceReceipt(root,"after-child-before-reopen"));
    opened=store.open();OPENHDK_FAIL_IF(19,!opened.succeeded() || !trace("after-child-reconcile",p));
    const auto expectedRevision=priorRevision+(phase>=2?1:0);
    std::cout<<"CUT_RESULT phase="<<phase<<" repeat="<<repeat<<" prior_revision="<<priorRevision
        <<" observed_revision="<<opened.projection->catalogRevision<<" expected_revision="<<expectedRevision<<'\n';
    OPENHDK_FAIL_IF(20,opened.projection->catalogRevision!=expectedRevision);(void)store.close();
  }
  std::cout<<"EVIDENCE status=completed native-acceptance=Pending test_exit=0\n";
#endif
}
