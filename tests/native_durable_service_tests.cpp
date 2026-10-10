// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/DurableLibraryService.hpp"
#include "tests/TestCheck.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <new>
namespace {std::ptrdiff_t failAfter=-1;}
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
NOINLINE void* operator new(std::size_t n){if(failAfter==0)throw std::bad_alloc();if(failAfter>0)--failAfter;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
NOINLINE void operator delete(void* p) noexcept {std::free(p);}
NOINLINE void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
#undef NOINLINE
using namespace OpenHDK;
namespace fs=std::filesystem;
using Access=DurableLibraryServiceTestAccess;
#ifdef __linux__
namespace {
struct Temp {
 fs::path path;
 Temp(){const auto* parent=std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR");path=(parent?fs::path(parent):fs::temp_directory_path())/("openhdk-native-service-"+std::to_string(::getpid())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directory(path);}
 ~Temp(){std::error_code ec;fs::remove_all(path,ec);}
};
void write(const fs::path& p,std::span<const std::uint8_t> b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size()));}
CheckpointBytes read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
std::unique_ptr<SongDiscovery> owner(const fs::path& path){fs::create_directories(path);const std::vector<std::uint8_t> midi{'M','T','h','d',0,0,0,6,0,0,0,1,0,3,'M','T','r','k',0,0,0,9,0,0xff,5,1,'A',0,0xff,0x2f,0};write(path/"ready.kar",midi);auto o=std::make_unique<SongDiscovery>();auto root=o->registerRoot(path);if(!root.root || !o->scan(*root.root).succeeded())std::abort();return o;}
DurableServiceOpenResult create(std::unique_ptr<SongDiscovery> o,const fs::path& p,std::string name="catalog.ohkcat") {
 if(std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR"))return DurableLibraryService::createLinux(std::move(o),p.string(),std::move(name));
 return Access::createNative(std::move(o),p.string(),std::move(name));
}
DurableServiceOpenResult open(const fs::path& p){if(std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR"))return DurableLibraryService::openLinux(p.string(),"catalog.ohkcat");return Access::openNative(p.string(),"catalog.ohkcat");}
void banAllocations() noexcept {failAfter=0;}
std::size_t fds(){return static_cast<std::size_t>(std::distance(fs::directory_iterator("/proc/self/fd"),fs::directory_iterator{}));}
}
#endif
int main(){
#ifndef __linux__
 auto result=DurableLibraryService::createLinux(std::make_unique<SongDiscovery>(),"/unused","catalog.ohkcat");
 OPENHDK_FAIL_IF(1,result.service || !result.error || !result.error->store || result.error->store->code!=StoreErrorCode::UnsupportedStorage);
 std::cout<<"native Linux service unsupported; no native I/O\n";
#else
 Temp t;const auto store=t.path/"store",root=t.path/"songs";fs::create_directory(store);
 const auto initial=fds();auto source=owner(root);auto historical=source->snapshot();const auto id=historical->songs[0].id;
 auto started=create(std::move(source),store);OPENHDK_FAIL_IF(1,!started.succeeded() || !started.receipt || started.service->snapshot()!=historical);
 auto& service=*started.service;const auto before=read(store/"catalog.ohkcat");
 auto initialDisk=decodeCatalogCheckpoint(before);OPENHDK_FAIL_IF(2,!initialDisk.succeeded() || initialDisk.value->sequence!=1);
 auto changed=service.replaceUserOverrides(id,{"Thai ไทย",{}});OPENHDK_FAIL_IF(3,!changed.succeeded() || changed.receipt->token.sequence!=2 || service.snapshot()==historical);
 auto disk=decodeCatalogCheckpoint(read(store/"catalog.ohkcat"));OPENHDK_FAIL_IF(4,!disk.succeeded() || disk.value->songs[0].title!="Thai ไทย" || changed.snapshot->songs[0].state!=CatalogState::Ready);
 auto noop=service.replaceUserOverrides(id,{"Thai ไทย",{}});OPENHDK_FAIL_IF(5,!noop.succeeded() || noop.status!=DurableOverrideStatus::NoChange || noop.receipt->token!=changed.receipt->token);
 Access::nativeAfterPublication(service,banAllocations);auto noAlloc=service.replaceUserOverrides(id,{"Confirmed",{}});failAfter=-1;Access::nativeAfterPublication(service,nullptr);
 OPENHDK_FAIL_IF(6,!noAlloc.succeeded() || noAlloc.receipt->token.sequence!=3);
 OPENHDK_FAIL_IF(7,service.close() || service.state()!=DurableServiceState::Closed);started.service.reset();
 auto restored=open(store);OPENHDK_FAIL_IF(8,!restored.succeeded() || restored.receipt || restored.service->snapshot()->songs[0].state!=CatalogState::Invalid);
 const auto restoredId=restored.service->snapshot()->songs[0].id;auto invalidEdit=restored.service->replaceUserOverrides(restoredId,{"Offline override",{}});
 OPENHDK_FAIL_IF(9,!invalidEdit.succeeded() || invalidEdit.snapshot->songs[0].state!=CatalogState::Invalid);
 restored.service.reset();OPENHDK_FAIL_IF(10,fds()!=initial);
 // Store equal to / inside a registered song root must fail before baseline save.
 auto insideOwner=owner(t.path/"inside");const auto insideId=insideOwner->snapshot()->roots[0].id;
 auto rejected=create(std::move(insideOwner),t.path/"inside");
 OPENHDK_FAIL_IF(11,rejected.service || !rejected.error || !rejected.error->binding || rejected.error->binding->code!=NativeBindingErrorCode::RootOverlap || rejected.error->root!=insideId || fs::exists(t.path/"inside"/"catalog.ohkcat"));
 OPENHDK_FAIL_IF(12,!fs::exists(t.path/"inside"/"catalog.ohkcat.ohk-lock"));
 // An existing checkpoint cannot be overwritten by Create.
 const auto primary=read(store/"catalog.ohkcat");auto existing=create(owner(root),store);
 OPENHDK_FAIL_IF(13,existing.service || !existing.error || existing.error->code!=DurableServiceErrorCode::BaselineMismatch || read(store/"catalog.ohkcat")!=primary);
 // Final fence, after the caller's last checkpoint hook: no rename / memory swap.
 const auto fenceStore=t.path/"fence-store",fenceRoot=t.path/"fence-root";fs::create_directory(fenceStore);
 auto fenced=create(owner(fenceRoot),fenceStore);OPENHDK_FAIL_IF(14,!fenced.succeeded());
 auto old=fenced.service->snapshot();const auto oldBytes=read(fenceStore/"catalog.ohkcat");const auto fenceId=old->songs[0].id;
 StoreControl control;bool mutated=false;control.checkpoint=[&](StoreCheckpoint point){if(point==StoreCheckpoint::BeforePublication){std::error_code ec;fs::rename(fenceRoot,t.path/"prior-root",ec);if(!ec)fs::create_directory(fenceRoot,ec);mutated=!ec;}};
 auto failed=fenced.service->replaceUserOverrides(fenceId,{"Rejected",{}},control);
 OPENHDK_FAIL_IF(15,!mutated || failed.succeeded() || !failed.error || !failed.error->binding || failed.error->store->outcome!=StoreOutcome::NotCommitted);
 OPENHDK_FAIL_IF(16,fenced.service->snapshot()!=old || read(fenceStore/"catalog.ohkcat")!=oldBytes || fenced.service->state()!=DurableServiceState::RecoveryRequired);
 OPENHDK_FAIL_IF(17,fenced.service->replaceUserOverrides(fenceId,{}).error->code!=DurableServiceErrorCode::RecoveryRequired);
 fenced.service.reset();
 // NoChange must recheck after cancellation observation; it cannot skip admission.
 fs::rename(t.path/"prior-root",t.path/"another-root");auto nc=create(owner(t.path/"nc-root"),fenceStore,"noop.ohkcat");OPENHDK_FAIL_IF(18,!nc.succeeded());
 const auto ncOld=nc.service->snapshot();const auto ncId=ncOld->songs[0].id;const auto ncBytes=read(fenceStore/"noop.ohkcat");StoreControl ncControl;bool once=false;
 ncControl.cancelled=[&]{if(!once){once=true;std::error_code ec;fs::rename(t.path/"nc-root",t.path/"nc-prior",ec);fs::create_directory(t.path/"nc-root",ec);}return false;};
 auto ncFail=nc.service->replaceUserOverrides(ncId,{},ncControl);OPENHDK_FAIL_IF(19,ncFail.succeeded() || !ncFail.error->binding || nc.service->snapshot()!=ncOld || read(fenceStore/"noop.ohkcat")!=ncBytes);
 nc.service.reset();
 // Post-publication sync failure remains Uncertain, never presented as rollback.
 const auto us=t.path/"uncertain";fs::create_directory(us);auto uncertain=create(owner(t.path/"u-root"),us);OPENHDK_FAIL_IF(20,!uncertain.succeeded());
 const auto uOld=uncertain.service->snapshot();Access::nativeFault(*uncertain.service,LinuxProviderFault::SyncPublication);
 auto uf=uncertain.service->replaceUserOverrides(uOld->songs[0].id,{"Disk may be new",{}});
 OPENHDK_FAIL_IF(21,uf.succeeded() || !uf.error->store || uf.error->store->outcome!=StoreOutcome::Uncertain || uncertain.service->snapshot()!=uOld || uncertain.service->state()!=DurableServiceState::RecoveryRequired);
 uncertain.service.reset();auto reconciled=open(us);OPENHDK_FAIL_IF(22,!reconciled.succeeded() || reconciled.service->snapshot()->songs[0].overrides->title->bytes()!="Disk may be new");reconciled.service.reset();
 NativeStoreAdmissionLimits bad;bad.activeRoots=0;auto invalid=DurableLibraryService::createLinux(owner(root),store.string(),"invalid.ohkcat",{},bad);
 OPENHDK_FAIL_IF(23,invalid.service || !invalid.error || invalid.error->code!=DurableServiceErrorCode::InvalidConfiguration || fs::exists(store/"invalid.ohkcat.ohk-lock"));
 // Admission allocation failures close all provider/guard handles. Use ExpectedAbsent.
 bool succeeded=false;unsigned failedCount=0;
 for(std::ptrdiff_t n=0;n<256;++n){auto o=owner(root);auto name="sweep-"+std::to_string(n)+".ohkcat";auto parent=store.string();const auto primaryPath=store/name;failAfter=n;auto r=std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR")?DurableLibraryService::createLinux(std::move(o),std::move(parent),std::move(name)):Access::createNative(std::move(o),std::move(parent),std::move(name));failAfter=-1;
   if(r.service){succeeded=true;r.service.reset();break;}
   ++failedCount;OPENHDK_FAIL_IF(24,fs::exists(primaryPath) || fds()!=initial);
 }
 OPENHDK_FAIL_IF(25,!succeeded || failedCount==0);
 OPENHDK_FAIL_IF(26,historical->songs[0].overrides || fds()!=initial);
 std::cout<<"native-service checks=26 factory=experimental native-acceptance=Pending filesystem-bypass="<<(std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR")?0:1)<<"\n";
#endif
}
