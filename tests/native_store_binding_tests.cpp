// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/NativeStoreBinding.hpp"
#include "library/LinuxCheckpointProvider.hpp"
#include "tests/TestCheck.hpp"
#include "tests/NativeAdmissionReceipts.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <new>
#include <type_traits>
namespace {std::ptrdiff_t failAfter=-1;}
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
using Access=NativeStoreBindingTestAccess;
static_assert(!std::is_default_constructible_v<NativeStoreBinding>);
static_assert(!std::is_constructible_v<NativeStoreBinding,int>);
static_assert(!std::is_copy_constructible_v<NativeStoreBinding>);
static_assert(!std::is_default_constructible_v<NativeStoreAdmission>);
bool code(const std::optional<NativeBindingError>& e,NativeBindingErrorCode c){return e && e->code==c;}
#ifdef __linux__
namespace {
struct Temp {
  fs::path path;
  Temp(){const char* parent=std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR");
    path=(parent?fs::path(parent):fs::temp_directory_path())/("openhdk-binding-"+std::to_string(::getpid())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directory(path);}
  ~Temp(){std::error_code ec;fs::remove_all(path,ec);}
};
std::optional<StoreProviderError> acquire(LinuxCheckpointLease& l,const fs::path& p,std::string_view name="catalog.ohkcat") {
  if(std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR"))return l.acquire(p.string(),name);
  return LinuxCheckpointLeaseTestAccess::acquire(l,p.string(),name);
}
std::size_t fdCount(){return static_cast<std::size_t>(std::distance(fs::directory_iterator("/proc/self/fd"),fs::directory_iterator{}));}
std::string read(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
fs::path hookPath,hookSaved;
void replaceRoot() noexcept {std::error_code ec;fs::rename(hookPath,hookSaved,ec);if(!ec)fs::create_directory(hookPath,ec);}
NativeStoreAdmissionResult admit(const LinuxCheckpointLease& l,const std::shared_ptr<const NativeStoreBinding>& b,
    const fs::path& root,NativeStoreAdmissionLimits limits={},std::size_t owned=0) {
  const auto text=root.string();const std::array<std::string_view,1> paths{text};
  return Access::admit(l,b,paths,limits,owned);
}
}
#endif
int main() {
#ifndef __linux__
  LinuxCheckpointLease lease;auto b=Access::binding(lease);
  OPENHDK_FAIL_IF(1,b.binding || !code(b.error,NativeBindingErrorCode::Unbound));
  std::cout<<"native binding unavailable on non-Linux; no I/O\n";
#else
  Temp temp;fs::create_directory(temp.path/"store");fs::create_directory(temp.path/"songs");
  const auto store=temp.path/"store",songs=temp.path/"songs";
  {std::ofstream f(store/"catalog.ohkcat");f<<"primary sentinel";}
  const auto initialFds=fdCount();LinuxCheckpointLease l;
  OPENHDK_FAIL_IF(1,Access::binding(l).binding || !code(Access::binding(l).error,NativeBindingErrorCode::Unbound));
  OPENHDK_FAIL_IF(2,acquire(l,store));
  const auto heldFds=fdCount(),leaseBytes=l.ownedBytes();AdmissionEvidence::begin();auto result=Access::binding(l);
  OPENHDK_FAIL_IF(3,result.error || !result.binding || fdCount()!=heldFds+1);
  auto b=result.binding;result.binding.reset();
  OPENHDK_FAIL_IF(4,b->parentMapping()!=store.string() || b->primaryName()!="catalog.ohkcat" || l.ownedBytes()!=leaseBytes+b->retainedBytes());
  OPENHDK_FAIL_IF(5,(::fcntl(Access::descriptor(*b),F_GETFD)&FD_CLOEXEC)==0);
  auto same=Access::binding(l);OPENHDK_FAIL_IF(6,same.binding!=b || same.error || fdCount()!=heldFds+1);same.binding.reset();
  OPENHDK_FAIL_IF(67,!AdmissionEvidence::trace("fresh-and-cached-export"));
  std::cout<<"BINDING_CASE case=export mapping_match="<<(b->parentMapping()==store.string())<<" primary_match="<<(b->primaryName()=="catalog.ohkcat")<<" cloexec=1 cache_same=1 duplicate_fds=1\n";
  AdmissionEvidence::begin();auto a=admit(l,b,songs);OPENHDK_FAIL_IF(7,a.error || !a.admission || Access::recheck(*a.admission,l));
  OPENHDK_FAIL_IF(8,a.admission->retainedBytes()!=b->retainedBytes()+songs.string().size());
  OPENHDK_FAIL_IF(68,!AdmissionEvidence::trace("sibling-admission"));
  AdmissionEvidence::begin();auto equal=admit(l,b,store);OPENHDK_FAIL_IF(9,equal.admission || !code(equal.error,NativeBindingErrorCode::RootOverlap));
  auto parent=admit(l,b,temp.path);OPENHDK_FAIL_IF(10,parent.admission || !code(parent.error,NativeBindingErrorCode::RootOverlap));
  Access::skipLexical(true);auto ancestors=admit(l,b,temp.path);Access::skipLexical(false);
  OPENHDK_FAIL_IF(11,ancestors.admission || !code(ancestors.error,NativeBindingErrorCode::RootOverlap));
  OPENHDK_FAIL_IF(69,!AdmissionEvidence::trace("equal-parent-ancestry-overlap"));
  std::cout<<"BINDING_CASE case=overlap lexical_equal=RootOverlap lexical_parent=RootOverlap handle_parent=RootOverlap lexical_bypass=1\n";
  fs::create_directory(store/"song-child");auto child=admit(l,b,store/"song-child");OPENHDK_FAIL_IF(12,child.error || !child.admission);child.admission.reset();
  fs::create_directory(temp.path/"store2");auto neighbor=admit(l,b,temp.path/"store2");OPENHDK_FAIL_IF(13,neighbor.error);neighbor.admission.reset();
  auto absent=admit(l,b,temp.path/"missing");OPENHDK_FAIL_IF(14,absent.admission || !code(absent.error,NativeBindingErrorCode::SourceChanged));
  fs::create_directory_symlink(songs,temp.path/"root-link");auto link=admit(l,b,temp.path/"root-link");OPENHDK_FAIL_IF(15,link.admission || !code(link.error,NativeBindingErrorCode::SourceChanged));
  fs::create_directory(songs/"nested");auto ancestorLink=admit(l,b,temp.path/"root-link"/"nested");OPENHDK_FAIL_IF(16,!code(ancestorLink.error,NativeBindingErrorCode::SourceChanged));
  auto noRoots=Access::admit(l,b,{});OPENHDK_FAIL_IF(17,noRoots.error || !noRoots.admission);noRoots.admission.reset();
  LinuxCheckpointLease foreign;OPENHDK_FAIL_IF(18,acquire(foreign,songs));
  AdmissionEvidence::begin();auto foreignUse=Access::admit(foreign,b,{});OPENHDK_FAIL_IF(19,foreignUse.admission || !code(foreignUse.error,NativeBindingErrorCode::ForeignBinding));foreign.release();OPENHDK_FAIL_IF(70,!AdmissionEvidence::trace("foreign-owner"));
  NativeStoreAdmissionLimits limits;limits.stagedBytes=b->retainedBytes();auto exact=Access::binding(l,limits);
  OPENHDK_FAIL_IF(20,exact.error || exact.binding!=b);exact.binding.reset();--limits.stagedBytes;
  OPENHDK_FAIL_IF(21,!code(Access::binding(l,limits).error,NativeBindingErrorCode::LimitExceeded));
  limits={};limits.stagedBytes=b->retainedBytes()+songs.string().size();auto budget=admit(l,b,songs,limits);
  OPENHDK_FAIL_IF(22,budget.error || !budget.admission);budget.admission.reset();--limits.stagedBytes;
  OPENHDK_FAIL_IF(23,!code(admit(l,b,songs,limits).error,NativeBindingErrorCode::LimitExceeded));
  limits={};limits.stagedBytes=100;OPENHDK_FAIL_IF(24,!code(admit(l,b,songs,limits,101).error,NativeBindingErrorCode::InvalidConfiguration));
  limits={};limits.descriptors=13;OPENHDK_FAIL_IF(25,!code(admit(l,b,songs,limits).error,NativeBindingErrorCode::LimitExceeded));
  limits.descriptors=14;auto fdExact=admit(l,b,songs,limits);OPENHDK_FAIL_IF(26,fdExact.error);fdExact.admission.reset();
  limits={};limits.parentSteps=1;OPENHDK_FAIL_IF(27,!code(admit(l,b,songs,limits).error,NativeBindingErrorCode::LimitExceeded));
  const std::array<NativeStoreAdmissionLimits,12> invalid{{
    {0,4096,32,32,45,100},{33,4096,32,32,45,100},{32,0,32,32,45,100},{32,4097,32,32,45,100},
    {32,4096,0,32,45,100},{32,4096,33,32,45,100},{32,4096,32,0,45,100},{32,4096,32,33,45,100},
    {32,4096,32,32,0,100},{32,4096,32,32,46,100},{32,4096,32,32,45,0},{32,4096,32,32,45,64U*1024U*1024U+1}}};
  for(const auto limit:invalid)OPENHDK_FAIL_IF(28,!code(Access::binding(l,limit).error,NativeBindingErrorCode::InvalidConfiguration));
  limits={};limits.pathBytes=store.string().size()-1;OPENHDK_FAIL_IF(29,!code(Access::binding(l,limits).error,NativeBindingErrorCode::LimitExceeded));
  AdmissionEvidence::begin();Access::missingMount(true);auto missing=admit(l,b,songs);Access::missingMount(false);OPENHDK_FAIL_IF(30,missing.admission || !code(missing.error,NativeBindingErrorCode::UnsupportedStorage));
  Access::differentRootMount(true);auto different=admit(l,b,songs);Access::differentRootMount(false);OPENHDK_FAIL_IF(31,different.admission || !code(different.error,NativeBindingErrorCode::UnsupportedStorage));
  auto proc=admit(l,b,"/proc");OPENHDK_FAIL_IF(32,proc.admission || !code(proc.error,NativeBindingErrorCode::UnsupportedStorage));
  OPENHDK_FAIL_IF(71,!AdmissionEvidence::trace("mount-rejections"));
  std::cout<<"BINDING_CASE case=mount-rejections missing=injected different=injected proc=actual result=UnsupportedStorage\n";
  AdmissionEvidence::begin();hookPath=songs;hookSaved=temp.path/"old-songs";Access::afterRoots(replaceRoot);auto replaced=admit(l,b,songs);Access::afterRoots(nullptr);
  OPENHDK_FAIL_IF(33,replaced.admission || !code(replaced.error,NativeBindingErrorCode::SourceChanged));
  auto changed=Access::recheck(*a.admission,l);OPENHDK_FAIL_IF(34,!code(changed,NativeBindingErrorCode::SourceChanged) || changed->operation!=NativeBindingOperation::Recheck);
  OPENHDK_FAIL_IF(72,!AdmissionEvidence::trace("root-replacement"));
  std::cout<<"BINDING_CASE case=root-replacement admit=SourceChanged recheck=SourceChanged root_index="<<*changed->rootIndex<<"\n";
  a.admission.reset();fs::remove(songs);fs::rename(hookSaved,songs);
  AdmissionEvidence::begin();auto again=admit(l,b,songs);OPENHDK_FAIL_IF(35,again.error);l.release();
  OPENHDK_FAIL_IF(36,!code(Access::recheck(*again.admission,l),NativeBindingErrorCode::StaleBinding));
  OPENHDK_FAIL_IF(37,b->parentMapping()!=store.string() || ::fcntl(Access::descriptor(*b),F_GETFD)<0);
  OPENHDK_FAIL_IF(38,acquire(l,store));auto next=Access::binding(l);
  OPENHDK_FAIL_IF(39,next.error || next.binding==b || !code(Access::recheck(*again.admission,l),NativeBindingErrorCode::StaleBinding));
  OPENHDK_FAIL_IF(73,!AdmissionEvidence::trace("release-and-reacquire"));
  std::cout<<"BINDING_CASE case=epoch old_retained=1 released=StaleBinding reacquired=StaleBinding new_binding=1\n";
  again.admission.reset();b.reset();l.release();next.binding.reset();OPENHDK_FAIL_IF(40,fdCount()!=initialFds);
  // Allocation failure sweeps must not publish a cache or leak a duplicate/root FD.
  OPENHDK_FAIL_IF(41,acquire(l,store));const auto beforeExport=fdCount();bool exported=false;unsigned throws=0;
  for(std::ptrdiff_t n=0;n<32;++n) {
    failAfter=n;auto v=Access::binding(l);failAfter=-1;
    if(v.binding){b=v.binding;exported=true;break;}
    OPENHDK_FAIL_IF(42,!code(v.error,NativeBindingErrorCode::StorageFailure) || fdCount()!=beforeExport);++throws;
  }
  OPENHDK_FAIL_IF(43,!exported || throws==0);const auto beforeAdmit=fdCount();bool admitted=false;throws=0;
  for(std::ptrdiff_t n=0;n<64;++n) {
    const auto text=songs.string();const std::array<std::string_view,1> paths{text};failAfter=n;auto v=Access::admit(l,b,paths);failAfter=-1;
    if(v.admission){admitted=true;break;}
    OPENHDK_FAIL_IF(44,!code(v.error,NativeBindingErrorCode::StorageFailure) || fdCount()!=beforeAdmit);++throws;
  }
  OPENHDK_FAIL_IF(45,!admitted || throws==0);
  std::array<std::string,33> names;std::array<std::string_view,33> paths;
  for(std::size_t i=0;i<names.size();++i){const auto path=temp.path/("root-"+std::to_string(i));fs::create_directory(path);names[i]=path.string();paths[i]=names[i];}
  auto cap=Access::admit(l,b,std::span<const std::string_view>(paths).first(32));OPENHDK_FAIL_IF(46,cap.error || !cap.admission || fdCount()>initialFds+35);cap.admission.reset();
  OPENHDK_FAIL_IF(47,!code(Access::admit(l,b,paths).error,NativeBindingErrorCode::LimitExceeded));
  OPENHDK_FAIL_IF(48,read(store/"catalog.ohkcat")!="primary sentinel");
  l.release();b.reset();OPENHDK_FAIL_IF(49,fdCount()!=initialFds);
  std::shared_ptr<const NativeStoreBinding> orphan;
  {LinuxCheckpointLease local;OPENHDK_FAIL_IF(50,acquire(local,store));orphan=Access::binding(local).binding;}
  OPENHDK_FAIL_IF(51,!orphan || orphan->parentMapping()!=store.string() || ::fcntl(Access::descriptor(*orphan),F_GETFD)<0);
  OPENHDK_FAIL_IF(52,acquire(l,store));OPENHDK_FAIL_IF(53,!code(Access::admit(l,orphan,{}).error,NativeBindingErrorCode::ForeignBinding));l.release();orphan.reset();
  // A retained store mapping that was replaced cannot authorize later admission.
  OPENHDK_FAIL_IF(54,acquire(l,store));b=Access::binding(l).binding;fs::rename(store,temp.path/"old-store");fs::create_directory(store);
  OPENHDK_FAIL_IF(55,!code(admit(l,b,songs).error,NativeBindingErrorCode::SourceChanged));l.release();b.reset();
  OPENHDK_FAIL_IF(56,read(temp.path/"old-store"/"catalog.ohkcat")!="primary sentinel" || fs::exists(store/"catalog.ohkcat") || fdCount()!=initialFds);
  // Exact path/component limits use an already existing sibling root.
  fs::create_directory(temp.path/"limits");LinuxCheckpointLease bounded;
  OPENHDK_FAIL_IF(57,acquire(bounded,temp.path/"limits"));auto boundedBinding=Access::binding(bounded).binding;
  const auto rootText=songs.string();const auto parentText=(temp.path/"limits").string();
  NativeStoreAdmissionLimits exactLimits;
  exactLimits.pathBytes=parentText.size();auto pathExact=Access::binding(bounded,exactLimits);
  OPENHDK_FAIL_IF(58,pathExact.error);pathExact.binding.reset();--exactLimits.pathBytes;
  OPENHDK_FAIL_IF(59,!code(Access::binding(bounded,exactLimits).error,NativeBindingErrorCode::LimitExceeded));
  exactLimits={};exactLimits.components=static_cast<std::size_t>(std::count(parentText.begin(),parentText.end(),'/'));
  auto componentExact=Access::binding(bounded,exactLimits);OPENHDK_FAIL_IF(60,componentExact.error);componentExact.binding.reset();--exactLimits.components;
  OPENHDK_FAIL_IF(61,!code(Access::binding(bounded,exactLimits).error,NativeBindingErrorCode::LimitExceeded));
  exactLimits={};exactLimits.pathBytes=rootText.size();auto rootExact=admit(bounded,boundedBinding,songs,exactLimits);
  OPENHDK_FAIL_IF(62,rootExact.error);rootExact.admission.reset();--exactLimits.pathBytes;
  OPENHDK_FAIL_IF(63,!code(admit(bounded,boundedBinding,songs,exactLimits).error,NativeBindingErrorCode::InvalidConfiguration));
  exactLimits={};exactLimits.activeRoots=1;const std::array<std::string_view,2> tooMany{rootText,rootText};
  OPENHDK_FAIL_IF(64,!code(Access::admit(bounded,boundedBinding,tooMany,exactLimits).error,NativeBindingErrorCode::LimitExceeded));
  failAfter=0;auto cacheNoAlloc=Access::binding(bounded);failAfter=-1;
  OPENHDK_FAIL_IF(65,cacheNoAlloc.error || cacheNoAlloc.binding!=boundedBinding);cacheNoAlloc.binding.reset();
  bounded.release();boundedBinding.reset();OPENHDK_FAIL_IF(66,fdCount()!=initialFds);
  // Deliberately overflow the fixed buffer: evidence collection must fail closed.
  Access::receipts();for(unsigned i=0;i<257;++i)(void)Access::admit(l,{},{});
  OPENHDK_FAIL_IF(74,!Access::overflow() || Access::records().size()!=256);
  Access::receipts(false);
  std::cout<<"ADMISSION_OVERFLOW_TEST capacity=256 detected=1 synthetic=1\n";
  {
  // Prospective primitives only; no service fence/mutation publication yet.
  // Run against harness-owned directories with the same eligibility choice.
  const auto prospectiveStore=temp.path/"prospective-store";fs::create_directory(prospectiveStore);
  OPENHDK_FAIL_IF(75,acquire(l,prospectiveStore));b=Access::binding(l).binding;
  auto current=Access::admit(l,b,std::span<const std::string_view>(paths).first(1));
  OPENHDK_FAIL_IF(76,current.error || !current.admission);
  using Row=Access::ProspectiveRoot;
  std::array<Row,2> add{{{paths[0],0},{paths[1],{}}}};
  const auto currentFds=fdCount();
  auto staged=Access::stage(l,*current.admission,add);
  OPENHDK_FAIL_IF(77,staged.error || !staged.admission || fdCount()!=currentFds+1
      || !Access::shared(*current.admission,0,*staged.admission,0)
      || staged.admission->retainedBytes()!=b->retainedBytes()+names[0].size()+names[1].size());
  OPENHDK_FAIL_IF(78,Access::recheck(*current.admission,l) || Access::recheck(*staged.admission,l));
  staged.admission.reset();OPENHDK_FAIL_IF(79,fdCount()!=currentFds);
  NativeStoreAdmissionLimits peak;peak.descriptors=14;
  OPENHDK_FAIL_IF(80,!code(Access::stage(l,*current.admission,add,{},peak).error,NativeBindingErrorCode::LimitExceeded)
      || fdCount()!=currentFds);
  auto paid=b->retainedBytes()+names[0].size()+names[1].size();
  NativeStoreAdmissionLimits payload;payload.stagedBytes=paid;
  auto exact=Access::stage(l,*current.admission,add,{},payload);
  OPENHDK_FAIL_IF(81,exact.error || !exact.admission);exact.admission.reset();
  OPENHDK_FAIL_IF(82,!code(Access::stage(l,*current.admission,add,{},payload,1).error,NativeBindingErrorCode::LimitExceeded));
  std::array<Row,2> duplicates{{{paths[0],0},{paths[0],0}}};
  std::array<Row,1> forgotten{{{paths[1],{}}}};
  OPENHDK_FAIL_IF(83,!code(Access::stage(l,*current.admission,duplicates).error,NativeBindingErrorCode::InvalidConfiguration)
      || !code(Access::stage(l,*current.admission,forgotten).error,NativeBindingErrorCode::InvalidConfiguration));
  std::array<Row,1> sameTarget{{{paths[0],{}}}};
  OPENHDK_FAIL_IF(84,!code(Access::stage(l,*current.admission,sameTarget,0).error,NativeBindingErrorCode::SourceChanged)
      || !code(Access::stage(l,*current.admission,sameTarget,1).error,NativeBindingErrorCode::InvalidConfiguration));
  std::array<Row,2> overlapRows{{{paths[0],0},{b->parentMapping(),{}}}};
  OPENHDK_FAIL_IF(85,!code(Access::stage(l,*current.admission,overlapRows).error,NativeBindingErrorCode::RootOverlap)
      || fdCount()!=currentFds);
  Access::differentRootMount(true);auto different=Access::stage(l,*current.admission,add);Access::differentRootMount(false);
  OPENHDK_FAIL_IF(86,!code(different.error,NativeBindingErrorCode::SourceChanged)
      || different.admission || fdCount()!=currentFds);
  bool stagedSuccess=false;unsigned stageThrows=0;
  for(std::ptrdiff_t n=0;n<128;++n) {
    failAfter=n;auto v=Access::stage(l,*current.admission,add);failAfter=-1;
    if(v.admission){stagedSuccess=true;break;}
    OPENHDK_FAIL_IF(87,!code(v.error,NativeBindingErrorCode::StorageFailure) || fdCount()!=currentFds);++stageThrows;
  }
  OPENHDK_FAIL_IF(88,!stagedSuccess || stageThrows==0 || fdCount()!=currentFds);
  hookPath=fs::path(names[1]);hookSaved=temp.path/"prior-candidate";
  Access::afterRoots(replaceRoot);auto changedTarget=Access::stage(l,*current.admission,add);Access::afterRoots(nullptr);
  OPENHDK_FAIL_IF(89,!code(changedTarget.error,NativeBindingErrorCode::SourceChanged) || changedTarget.admission
      || Access::recheck(*current.admission,l) || fdCount()!=currentFds);
  // Existing root replacement cannot be silently adopted by fresh staging.
  hookPath=fs::path(names[0]);hookSaved=temp.path/"retired-root";replaceRoot();
  auto unrelated=Access::stage(l,*current.admission,add);
  OPENHDK_FAIL_IF(90,!code(unrelated.error,NativeBindingErrorCode::SourceChanged) || unrelated.error->rootIndex!=0);
  fs::remove(fs::path(names[0])); // Old mapping is now missing; retained FD lives.
  std::array<Row,1> repair{{{paths[1],{}}}};
  auto repaired=Access::stage(l,*current.admission,repair,0);
  OPENHDK_FAIL_IF(91,repaired.error || !repaired.admission || fdCount()!=currentFds+1
      || !code(Access::recheck(*current.admission,l),NativeBindingErrorCode::SourceChanged)
      || Access::recheck(*repaired.admission,l));
  repaired.admission.reset();current.admission.reset();fs::rename(hookSaved,fs::path(names[0]));
  // A retired target does not exempt an unrelated root. Reordered candidate
  // failures report candidate indices for the owning service's RootId mapping.
  const std::array<std::string_view,2> oldPaths{paths[0],paths[2]};current=Access::admit(l,b,oldPaths);
  OPENHDK_FAIL_IF(92,current.error);const auto twoFds=fdCount();
  fs::rename(fs::path(names[0]),temp.path/"missing-old");
  std::array<Row,2> changed{{{paths[2],1},{paths[1],{}}}};
  auto sharedRepair=Access::stage(l,*current.admission,changed,0);
  OPENHDK_FAIL_IF(93,sharedRepair.error || !sharedRepair.admission || fdCount()!=twoFds+1
      || !Access::shared(*current.admission,1,*sharedRepair.admission,0));
  sharedRepair.admission.reset();hookPath=fs::path(names[2]);hookSaved=temp.path/"other-prior";replaceRoot();
  auto unrelatedRepair=Access::stage(l,*current.admission,changed,0);
  OPENHDK_FAIL_IF(94,!code(unrelatedRepair.error,NativeBindingErrorCode::SourceChanged)
      || unrelatedRepair.error->rootIndex!=0 || unrelatedRepair.admission || fdCount()!=twoFds);
  fs::remove(fs::path(names[2]));fs::rename(hookSaved,fs::path(names[2]));
  // Final recheck still observes replacement of a shared unchanged guard.
  hookPath=fs::path(names[2]);hookSaved=temp.path/"late-other-prior";
  Access::afterRoots(replaceRoot);auto lateOther=Access::stage(l,*current.admission,changed,0);Access::afterRoots(nullptr);
  OPENHDK_FAIL_IF(95,!code(lateOther.error,NativeBindingErrorCode::SourceChanged) || lateOther.error->rootIndex!=0
      || lateOther.admission || fdCount()!=twoFds);
  fs::remove(fs::path(names[2]));fs::rename(hookSaved,fs::path(names[2]));
  current.admission.reset();fs::rename(temp.path/"missing-old",fs::path(names[0]));
  // Peak 31 -> 32 shares old descriptors; 32 + a replacement cannot fit 45.
  current=Access::admit(l,b,std::span<const std::string_view>(paths).first(31));
  std::array<Row,32> atCapacity;
  for(std::size_t i=0;i<31;++i){atCapacity[i]={paths[i],i};}
  atCapacity[31]={paths[31],{}};
  const auto capacityFds=fdCount();auto full=Access::stage(l,*current.admission,atCapacity);
  OPENHDK_FAIL_IF(96,full.error || !full.admission || fdCount()!=capacityFds+1);
  current.admission.reset();current.admission=std::move(full.admission);const auto fullFds=fdCount();
  atCapacity[31].previous=31;atCapacity[0]={paths[32],{}};
  auto tooManyGuards=Access::stage(l,*current.admission,atCapacity,0);
  OPENHDK_FAIL_IF(97,!code(tooManyGuards.error,NativeBindingErrorCode::LimitExceeded)
      || tooManyGuards.admission || fdCount()!=fullFds || Access::recheck(*current.admission,l));
  // Candidate lifetime independently retains shared guards and binding.
  std::array<Row,32> noChange;
  for(std::size_t i=0;i<32;++i)noChange[i]={paths[i],i};
  auto independent=Access::stage(l,*current.admission,noChange);
  OPENHDK_FAIL_IF(98,independent.error || !independent.admission || fdCount()!=fullFds);
  current.admission.reset();OPENHDK_FAIL_IF(99,fdCount()!=fullFds || Access::recheck(*independent.admission,l));
  l.release();OPENHDK_FAIL_IF(100,!code(Access::recheck(*independent.admission,l),NativeBindingErrorCode::StaleBinding));
  OPENHDK_FAIL_IF(101,acquire(l,prospectiveStore));auto freshBinding=Access::binding(l);
  OPENHDK_FAIL_IF(102,!code(Access::stage(l,*independent.admission,noChange).error,NativeBindingErrorCode::StaleBinding));
  // Foreign owner cannot borrow these identically shaped retained guards.
  LinuxCheckpointLease foreignLease;const auto foreignStore=temp.path/"foreign-stage-store";fs::create_directory(foreignStore);
  OPENHDK_FAIL_IF(104,acquire(foreignLease,foreignStore)
      || !code(Access::stage(foreignLease,*independent.admission,noChange).error,NativeBindingErrorCode::ForeignBinding));
  foreignLease.release();
  independent.admission.reset();b.reset();l.release();freshBinding.binding.reset();
  OPENHDK_FAIL_IF(103,fdCount()!=initialFds);
  OPENHDK_FAIL_IF(105,acquire(l,prospectiveStore));b=Access::binding(l).binding;
  auto emptyCurrent=Access::admit(l,b,{});std::array<Row,1> firstRoot{{{paths[1],{}}}};
  Access::differentRootMount(true);auto newDifferentMount=Access::stage(l,*emptyCurrent.admission,firstRoot);Access::differentRootMount(false);
  OPENHDK_FAIL_IF(106,!code(newDifferentMount.error,NativeBindingErrorCode::UnsupportedStorage) || newDifferentMount.admission);
  const auto emptyFds=fdCount();hookPath=fs::path(names[1]);hookSaved=temp.path/"new-root-prior";
  Access::afterRoots(replaceRoot);auto noSilentFirstRoot=Access::stage(l,*emptyCurrent.admission,firstRoot);Access::afterRoots(nullptr);
  OPENHDK_FAIL_IF(107,!code(noSilentFirstRoot.error,NativeBindingErrorCode::SourceChanged) || fdCount()!=emptyFds);
  emptyCurrent.admission.reset();l.release();b.reset();OPENHDK_FAIL_IF(108,fdCount()!=initialFds);
  }
  std::cout<<"binding checks=108 factory=experimental native-acceptance=Pending\n";
  std::cout<<"mount-alias namespace case=Skipped reason=private-namespace-harness-not-implemented\n";
  std::cout<<"filesystem-bypass="<<(std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR")?0:1)<<"\n";
#endif
}
