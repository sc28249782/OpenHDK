// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/LinuxCheckpointProvider.hpp"
#include "tests/TestCheck.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#ifdef __linux__
#include <chrono>
#include <fstream>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#endif
namespace { bool forbidNew=false;std::ptrdiff_t allocationCountdown=-1;std::uint64_t allocationThrows=0; }
#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif
TEST_NOINLINE void* operator new(std::size_t n) {
  if(forbidNew || allocationCountdown==0){++allocationThrows;throw std::bad_alloc();}
  if(allocationCountdown>0)--allocationCountdown;
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
struct OwnershipPacket {int code=-1;std::int64_t nativeError=0;};
CheckpointBytes fixtureBytes(const fs::path& path) {
  const int fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
  if(fd<0)throw std::runtime_error("fixture read open");
  struct stat st{};std::array<std::uint8_t,256> bytes{};
  const bool regular=::fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_size>=0 && st.st_size<=256;
  const auto n=regular?::pread(fd,bytes.data(),static_cast<std::size_t>(st.st_size),0):-1;::close(fd);
  if(!regular || n!=st.st_size)throw std::runtime_error("fixture read bounds");
  return {bytes.begin(),bytes.begin()+n};
}
void putFixture(const fs::path& path,std::span<const std::uint8_t> bytes) {
  std::ofstream f(path,std::ios::binary|std::ios::trunc);
  f.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
  if(!f)throw std::runtime_error("fixture write");
}
bool preserved(const fs::path& path,const CheckpointBytes& before,std::string_view tag) {
  const auto after=fixtureBytes(path);std::cout<<"PRESERVED tag="<<tag<<" bytes_equal="<<(before==after)<<" before_sha256=";
  hex(sourceRevision(before).sha256);std::cout<<" after_sha256=";hex(sourceRevision(after).sha256);std::cout<<'\n';
  return before==after && inspect(path,tag);
}
void providerReceipt(std::string_view tag,const std::optional<StoreProviderError>& error) {
  std::cout<<"PROVIDER_CASE tag="<<tag<<" success="<<(!error)<<" code="<<(error?static_cast<int>(error->code):-1)
      <<" native_errno="<<(error?error->nativeError:0)<<" injected=0\n";
}
void openReceipt(std::string_view tag,const StoreOpenResult& r) {
  std::cout<<"OPEN_CASE tag="<<tag<<" success="<<r.succeeded()<<" projection="<<r.projection.has_value()
      <<" expectation="<<r.expectation.has_value();
  if(r.error)std::cout<<" code="<<static_cast<int>(r.error->code)<<" operation="<<static_cast<int>(r.error->operation)<<" native_errno="<<r.error->nativeError;
  std::cout<<'\n';
}
bool errorIs(const std::optional<StoreProviderError>& e,StoreErrorCode c){return e && e->code==c;}
// Dedicated directories and bounded synthetic bytes. Every refusal records its
// operation/result and checks preservation before the enclosing harness cleans up.
int matrixTests(const fs::path& parent) {
  const auto baseline=*encodeCatalogCheckpoint(fixture(1)).value;
  const auto replacement=*encodeCatalogCheckpoint(fixture(2)).value;
  const auto make=[&](std::string_view name) {auto p=parent/std::string(name);fs::create_directory(p);return p;};
  std::cout<<"MATRIX groups=4 scope=selected-cases native-acceptance=Pending\n";
  {
    const auto root=make("ownership");putFixture(root/"catalog",baseline);
    LinuxCheckpointProvider p(root.string(),"catalog");enable(p);CatalogCheckpointStore owner(p),same(p);
    auto opened=owner.open();OPENHDK_FAIL_IF(21,!opened.succeeded() || !trace("ownership-owner-open",p));
    auto rejected=same.open();openReceipt("same-provider-second-store",rejected);
    OPENHDK_FAIL_IF(22,rejected.succeeded() || rejected.error->code!=StoreErrorCode::Busy || !preserved(root/"catalog",baseline,"same-provider"));
    LinuxCheckpointProvider contender(root.string(),"catalog");enable(contender);auto busy=contender.acquire();providerReceipt("same-process-contender",busy);
    OPENHDK_FAIL_IF(23,!errorIs(busy,StoreErrorCode::Busy) || !trace("same-process-contender",contender));contender.release();
    int pipes[2];OPENHDK_FAIL_IF(24,::pipe(pipes)<0);const auto fd=std::to_string(pipes[1]);const auto pid=::fork();OPENHDK_FAIL_IF(25,pid<0);
    if(pid==0){::close(pipes[0]);::execl("/proc/self/exe","evidence","--matrix-contend",root.c_str(),fd.c_str(),nullptr);::_exit(99);}
    ::close(pipes[1]);pollfd event{pipes[0],POLLIN,0};OwnershipPacket packet;
    const auto ready=::poll(&event,1,5000);const auto n=ready>0?::read(pipes[0],&packet,sizeof(packet)):0;::close(pipes[0]);
    int status=0;const bool exited=reap(pid,status);
    if(!exited){(void)::kill(pid,SIGKILL);(void)reap(pid,status);}
    std::cout<<"OWNERSHIP_CHILD pid="<<pid<<" packet_bytes="<<n<<" code="<<packet.code<<" native_errno="<<packet.nativeError
        <<" injected=0 wait_complete="<<exited<<" wait_status="<<status<<'\n';
    OPENHDK_FAIL_IF(26,n!=sizeof(packet) || !exited || !WIFEXITED(status) || WEXITSTATUS(status)!=0 ||
        packet.code!=static_cast<int>(StoreErrorCode::Busy) || packet.nativeError!=EWOULDBLOCK);
    auto live=p.readPrimary(256);OPENHDK_FAIL_IF(27,live.error || live.bytes!=baseline || !preserved(root/"catalog",baseline,"owner-after-contenders") || !trace("owner-still-held",p));
    (void)owner.close();auto acquired=contender.acquire();providerReceipt("after-owner-release",acquired);
    OPENHDK_FAIL_IF(28,acquired || !trace("after-owner-release",contender));contender.release();
  }
  {
    const auto root=make("owner-death");putFixture(root/"catalog",baseline);
    int pipes[2];OPENHDK_FAIL_IF(29,::pipe(pipes)<0);const auto fd=std::to_string(pipes[1]);const auto pid=::fork();OPENHDK_FAIL_IF(30,pid<0);
    if(pid==0){::close(pipes[0]);::execl("/proc/self/exe","evidence","--matrix-hold",root.c_str(),fd.c_str(),nullptr);::_exit(99);}
    ::close(pipes[1]);pollfd event{pipes[0],POLLIN,0};OwnershipPacket packet;
    const auto ready=::poll(&event,1,5000);const auto n=ready>0?::read(pipes[0],&packet,sizeof(packet)):0;::close(pipes[0]);
    LinuxCheckpointProvider contender(root.string(),"catalog");enable(contender);const auto busy=contender.acquire();providerReceipt("live-child-owner",busy);
    struct stat before{};const int observed=::lstat((root/"catalog.ohk-lock").c_str(),&before);
    const int killed=::kill(pid,SIGKILL);int status=0;const bool exited=reap(pid,status);
    std::cout<<"OWNER_DEATH pid="<<pid<<" packet_bytes="<<n<<" admitted_code="<<packet.code<<" admitted_errno="<<packet.nativeError
        <<" kill_rc="<<killed<<" wait_complete="<<exited<<" wait_status="<<status<<'\n';
    OPENHDK_FAIL_IF(31,n!=sizeof(packet) || packet.code!=-1 || !errorIs(busy,StoreErrorCode::Busy) || observed!=0 || killed!=0 || !exited || !WIFSIGNALED(status) || WTERMSIG(status)!=SIGKILL);
    const auto acquired=contender.acquire();providerReceipt("after-owner-death",acquired);struct stat after{};
    OPENHDK_FAIL_IF(32,acquired || ::lstat((root/"catalog.ohk-lock").c_str(),&after)<0 || before.st_dev!=after.st_dev || before.st_ino!=after.st_ino ||
        !preserved(root/"catalog",baseline,"owner-death-primary") || !trace("after-owner-death",contender));
    std::cout<<"LOCK_PRESERVED dev="<<after.st_dev<<" ino="<<after.st_ino<<" links="<<after.st_nlink<<" stale_lock_deleted=0\n";contender.release();
  }
  for(const auto tag:{"primary-symlink","primary-hardlink","primary-fifo","lock-symlink","lock-hardlink","lock-fifo"}) {
    const auto root=make(tag);putFixture(root/"sentinel",baseline);putFixture(root/"catalog",baseline);
    const bool lock=std::string_view(tag).starts_with("lock-");const auto selected=root/(lock?"catalog.ohk-lock":"catalog");
    if(!lock)fs::remove(selected);
    if(std::string_view(tag).ends_with("symlink"))fs::create_symlink("sentinel",selected);
    else if(std::string_view(tag).ends_with("hardlink"))fs::create_hard_link(root/"sentinel",selected);
    else OPENHDK_FAIL_IF(33,::mkfifo(selected.c_str(),0600)<0);
    struct stat before{},after{};OPENHDK_FAIL_IF(34,::lstat(selected.c_str(),&before)<0);
    LinuxCheckpointProvider p(root.string(),"catalog");enable(p);const auto rejected=p.acquire();providerReceipt(tag,rejected);
    const bool sameIdentity=::lstat(selected.c_str(),&after)==0 && before.st_dev==after.st_dev && before.st_ino==after.st_ino && before.st_mode==after.st_mode && before.st_nlink==after.st_nlink;
    std::cout<<"PATH_PRESERVED tag="<<tag<<" identity_equal="<<sameIdentity<<" before_ino="<<before.st_ino<<" after_ino="<<after.st_ino<<" mode="<<after.st_mode<<" links="<<after.st_nlink<<'\n';
    OPENHDK_FAIL_IF(34,!sameIdentity || !errorIs(rejected,StoreErrorCode::SourceChanged) || !trace(tag,p) || !inspect(selected,tag) || !preserved(root/"sentinel",baseline,tag));
    if(lock)OPENHDK_FAIL_IF(35,!preserved(root/"catalog",baseline,tag));
  }
  {
    const auto root=make("ancestor-symlink");fs::create_directories(root/"real"/"child");putFixture(root/"real"/"child"/"catalog",baseline);
    fs::create_directory_symlink("real",root/"linked");LinuxCheckpointProvider p((root/"linked"/"child").string(),"catalog");enable(p);
    const auto rejected=p.acquire();providerReceipt("ancestor-symlink",rejected);
    OPENHDK_FAIL_IF(36,!errorIs(rejected,StoreErrorCode::SourceChanged) || fs::exists(root/"real"/"child"/"catalog.ohk-lock") ||
        !preserved(root/"real"/"child"/"catalog",baseline,"ancestor-symlink") || !trace("ancestor-symlink",p));
  }
  {
    const auto root=make("directory-replacement");fs::create_directory(root/"active");putFixture(root/"active"/"catalog",baseline);
    LinuxCheckpointProvider p((root/"active").string(),"catalog");enable(p);OPENHDK_FAIL_IF(37,p.acquire() || !trace("directory-original",p));
    fs::rename(root/"active",root/"old");fs::create_directory(root/"active");putFixture(root/"active"/"catalog",replacement);
    auto rejected=p.checkDirectory();providerReceipt("directory-replacement",rejected);
    OPENHDK_FAIL_IF(38,!errorIs(rejected,StoreErrorCode::SourceChanged) || !preserved(root/"old"/"catalog",baseline,"retained-directory") ||
        !preserved(root/"active"/"catalog",replacement,"replacement-directory") || !trace("directory-replacement",p));p.release();
    OPENHDK_FAIL_IF(39,!fs::exists(root/"old"/"catalog.ohk-lock"));
  }
  {
    const auto root=make("short-io");putFixture(root/"catalog",baseline);LinuxCheckpointProvider p(root.string(),"catalog");enable(p);
    OPENHDK_FAIL_IF(40,p.acquire() || !trace("short-io-open",p));Access::shortIo(p);
    const auto a=p.createArtifact(StoreArtifactKind::Candidate);OPENHDK_FAIL_IF(41,!a.artifact || p.writeArtifact(*a.artifact,baseline));
    auto read=p.readArtifact(*a.artifact,256);OPENHDK_FAIL_IF(42,read.error || read.bytes!=baseline);
    unsigned interruptedRead=0,interruptedWrite=0;std::uint64_t bytesRead=0,bytesWritten=0;
    for(const auto& e:Access::records(p)) {
      if(e.call==LinuxProviderCall::Read || e.call==LinuxProviderCall::Write) {
        if(e.injected){OPENHDK_FAIL_IF(43,e.result!=-1 || e.nativeError!=EINTR);if(e.call==LinuxProviderCall::Read)++interruptedRead;else ++interruptedWrite;}
        else {OPENHDK_FAIL_IF(44,e.result<0 || e.result>7);if(e.call==LinuxProviderCall::Read)bytesRead+=static_cast<std::uint64_t>(e.result);else bytesWritten+=static_cast<std::uint64_t>(e.result);}
      }
    }
    std::cout<<"SHORT_IO read_bytes="<<bytesRead<<" write_bytes="<<bytesWritten<<" chunk_max=7 injected_read_eintr="<<interruptedRead<<" injected_write_eintr="<<interruptedWrite<<'\n';
    OPENHDK_FAIL_IF(45,bytesRead!=baseline.size() || bytesWritten!=baseline.size() || interruptedRead!=1 || interruptedWrite!=1 || p.cleanup(*a.artifact) || !trace("short-io-retry",p) || !preserved(root/"catalog",baseline,"short-io-primary"));p.release();
  }
  {
    const auto root=make("partial-write");putFixture(root/"catalog",baseline);LinuxCheckpointProvider p(root.string(),"catalog");enable(p);
    OPENHDK_FAIL_IF(46,p.acquire() || !trace("partial-write-open",p));Access::partialWrite(p,7);
    const auto a=p.createArtifact(StoreArtifactKind::Candidate);OPENHDK_FAIL_IF(47,!a.artifact);
    auto failed=p.writeArtifact(*a.artifact,baseline);
    const auto name=Access::artifactName(p,*a.artifact);const auto prefix=fixtureBytes(root/name);
    std::cout<<"PARTIAL_WRITE code="<<(failed?static_cast<int>(failed->code):-1)<<" native_errno="<<(failed?failed->nativeError:0)<<" injected=1 successful_prefix_bytes="<<prefix.size()<<'\n';
    OPENHDK_FAIL_IF(48,!errorIs(failed,StoreErrorCode::StorageFailure) || failed->nativeError!=EIO ||
        prefix!=CheckpointBytes(baseline.begin(),baseline.begin()+7) || !inspect(root/name,"partial-write-before-cleanup") || p.cleanup(*a.artifact) ||
        !trace("partial-write",p) || !preserved(root/"catalog",baseline,"partial-write-primary"));p.release();
  }
  {
    const auto root=make("allocation");putFixture(root/"catalog",baseline);LinuxCheckpointProvider p(root.string(),"catalog");enable(p);CatalogCheckpointStore store(p);
    auto opened=store.open();OPENHDK_FAIL_IF(49,!opened.succeeded() || !trace("allocation-open",p));const auto next=fixture(2);bool succeeded=false;unsigned failures=0;
    for(std::ptrdiff_t budget=0;budget<128;++budget) {
      const auto before=allocationThrows;allocationCountdown=budget;auto result=store.save(next,*opened.expectation);allocationCountdown=-1;
      std::cout<<"ALLOCATION budget="<<budget<<" injected=1 throws="<<(allocationThrows-before)<<'\n';saveReceipt("allocation-sweep",result);
      OPENHDK_FAIL_IF(50,!trace("allocation-sweep",p));
      if(result.succeeded()){succeeded=true;OPENHDK_FAIL_IF(51,!namespaceReceipt(root,"allocation-success"));break;}
      ++failures;OPENHDK_FAIL_IF(52,!result.error || result.error->code!=StoreErrorCode::StorageFailure || result.expectation || store.faulted() ||
          allocationThrows==before || !preserved(root/"catalog",baseline,"allocation-failure-primary"));
    }
    std::cout<<"ALLOCATION_SUMMARY failed_budgets="<<failures<<" reached_success="<<succeeded<<'\n';OPENHDK_FAIL_IF(53,!succeeded || failures==0);(void)store.close();
  }
  {
    const auto root=make("external-primary");putFixture(root/"catalog",baseline);LinuxCheckpointProvider p(root.string(),"catalog");enable(p);
    OPENHDK_FAIL_IF(54,p.acquire() || !p.readPrimary(256).bytes || !trace("external-primary-recheck",p));
    const auto a=p.createArtifact(StoreArtifactKind::Candidate);OPENHDK_FAIL_IF(55,!a.artifact || p.writeArtifact(*a.artifact,baseline) || p.syncArtifact(*a.artifact));
    putFixture(root/"incoming",replacement);fs::rename(root/"catalog",root/"old-primary");fs::rename(root/"incoming",root/"catalog");
    auto result=p.publish(*a.artifact,false);
    std::cout<<"PUBLISH_CASE tag=external-primary-after-read outcome="<<static_cast<int>(result.outcome)<<" code="<<(result.error?static_cast<int>(result.error->code):-1)<<" native_errno="<<(result.error?result.error->nativeError:0)<<" point=after-read-before-publish-identity-check\n";
    OPENHDK_FAIL_IF(56,result.outcome!=StorePublication::NotCommitted || !errorIs(result.error,StoreErrorCode::SourceChanged) || p.cleanup(*a.artifact) || !trace("external-primary",p) ||
        !preserved(root/"catalog",replacement,"external-primary-kept") || !preserved(root/"old-primary",baseline,"old-primary-kept"));p.release();
  }
  {
    const auto root=make("expected-absent");LinuxCheckpointProvider p(root.string(),"catalog");enable(p);OPENHDK_FAIL_IF(57,p.acquire() || !trace("expected-absent-open",p));
    const auto a=p.createArtifact(StoreArtifactKind::Candidate);OPENHDK_FAIL_IF(58,!a.artifact || p.writeArtifact(*a.artifact,baseline) || p.syncArtifact(*a.artifact));putFixture(root/"catalog",replacement);
    const auto result=p.publish(*a.artifact,true);
    std::cout<<"PUBLISH_CASE tag=expected-absent-race outcome="<<static_cast<int>(result.outcome)<<" native_errno="<<(result.error?result.error->nativeError:0)<<" injected=0\n";
    OPENHDK_FAIL_IF(59,result.outcome!=StorePublication::NotCommitted || !errorIs(result.error,StoreErrorCode::StaleCheckpoint) || result.error->nativeError!=EEXIST ||
        p.cleanup(*a.artifact) || !trace("expected-absent-race",p) || !preserved(root/"catalog",replacement,"expected-absent-primary"));p.release();
  }
  {
    const auto root=make("replaced-stage");putFixture(root/"catalog",baseline);LinuxCheckpointProvider p(root.string(),"catalog");enable(p);OPENHDK_FAIL_IF(60,p.acquire() || !trace("replaced-stage-open",p));
    const auto a=p.createArtifact(StoreArtifactKind::Candidate);OPENHDK_FAIL_IF(61,!a.artifact || p.writeArtifact(*a.artifact,baseline));
    const fs::path name=root/Access::artifactName(p,*a.artifact);fs::rename(name,root/"retained-stage");putFixture(name,replacement);
    const auto result=p.cleanup(*a.artifact);providerReceipt("replaced-stage-cleanup",result);
    OPENHDK_FAIL_IF(62,!errorIs(result,StoreErrorCode::SourceChanged) || !trace("replaced-stage-cleanup",p) || !preserved(name,replacement,"unrelated-stage-kept") ||
        !preserved(root/"retained-stage",baseline,"held-stage-kept") || !preserved(root/"catalog",baseline,"replaced-stage-primary"));p.release();
  }
  for(const auto schema:{-1,0,2}) {
    const auto root=make("invalid-schema-"+std::to_string(schema));auto malformed=baseline;
    if(schema<0)malformed[0]^=1;
    else {malformed[8]=static_cast<std::uint8_t>(schema);const auto digest=sourceRevision(std::span(malformed).first(malformed.size()-32)).sha256;std::copy(digest.begin(),digest.end(),malformed.end()-32);}
    putFixture(root/"catalog",malformed);putFixture(root/".ohk-stage-unrelated-valid",baseline);
    LinuxCheckpointProvider p(root.string(),"catalog");enable(p);CatalogCheckpointStore store(p);auto result=store.open();openReceipt("invalid-schema",result);
    const auto expected=schema<0?StoreErrorCode::InvalidCheckpoint:StoreErrorCode::UnsupportedSchema;
    std::cout<<"SCHEMA attempted="<<schema<<" expected_code="<<static_cast<int>(expected)<<'\n';
    OPENHDK_FAIL_IF(63,result.succeeded() || !result.error || result.error->code!=expected || result.expectation || result.projection || !trace("invalid-schema",p) ||
        !preserved(root/"catalog",malformed,"invalid-primary-kept") || !preserved(root/".ohk-stage-unrelated-valid",baseline,"valid-stage-not-promoted"));
  }
  {
    const auto root=make("missing-after-uncertainty");putFixture(root/"catalog",baseline);LinuxCheckpointProvider p(root.string(),"catalog");enable(p);CatalogCheckpointStore store(p);
    auto opened=store.open();OPENHDK_FAIL_IF(64,!opened.succeeded() || !trace("missing-open",p));Access::fault(p,LinuxProviderFault::Publish);
    auto uncertain=store.save(fixture(2),*opened.expectation);saveReceipt("missing-uncertain",uncertain);
    OPENHDK_FAIL_IF(65,uncertain.succeeded() || !store.faulted() || uncertain.expectation || !trace("missing-uncertain",p) || !namespaceReceipt(root,"uncertain-artifacts"));
    Access::fault(p,LinuxProviderFault::None);(void)store.close();fs::rename(root/"catalog",root/"retained-primary");
    auto missing=store.open();openReceipt("missing-after-uncertainty",missing);
    std::cout<<"MISSING_STATE faulted="<<store.faulted()<<" primary_exists="<<fs::exists(root/"catalog")<<'\n';
    OPENHDK_FAIL_IF(66,missing.succeeded() || !missing.error || missing.error->code!=StoreErrorCode::NotFound || missing.error->operation!=StoreOperation::Reconcile ||
        missing.expectation || !store.faulted() || fs::exists(root/"catalog") || !trace("missing-after-uncertainty",p) || !namespaceReceipt(root,"missing-no-promotion") ||
        !preserved(root/"retained-primary",baseline,"retained-primary-kept"));
    fs::rename(root/"retained-primary",root/"catalog");auto recovered=store.open();openReceipt("explicit-restoration",recovered);
    std::cout<<"RECOVERY_STATE faulted="<<store.faulted()<<" primary_exists="<<fs::exists(root/"catalog")<<'\n';
    OPENHDK_FAIL_IF(67,!recovered.succeeded() || store.faulted() || !trace("explicit-restoration",p));(void)store.close();
  }
  std::cout<<"MATRIX status=completed native-acceptance=Pending\n";return 0;
}

}
#endif
int main(int argc,char** argv) {
#ifndef __linux__
  (void)argc;(void)argv;std::cout<<"EVIDENCE platform=non-linux status=Skipped native-ext4-required\n";return 0;
#else
  nativeMode=std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR")!=nullptr;
  if(argc==4 && (std::string_view(argv[1])=="--matrix-contend" || std::string_view(argv[1])=="--matrix-hold")) {
    LinuxCheckpointProvider p(argv[2],"catalog");enable(p);const auto error=p.acquire();
    OwnershipPacket packet;packet.code=error?static_cast<int>(error->code):-1;packet.nativeError=error?error->nativeError:0;
    const int fd=std::stoi(argv[3]);ssize_t n;do{n=::write(fd,&packet,sizeof(packet));}while(n<0 && errno==EINTR);
    if(n!=static_cast<ssize_t>(sizeof(packet)))return 93;
    if(std::string_view(argv[1])=="--matrix-hold" && !error)for(;;)::pause();
    return 0;
  }
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
  const int matrix=matrixTests(root);if(matrix)return matrix;
  std::cout<<"EVIDENCE status=completed native-acceptance=Pending test_exit=0\n";
#endif
}
