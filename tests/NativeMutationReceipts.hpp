// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/DurableLibraryService.hpp"
#include "tests/NativeAdmissionReceipts.hpp"
#include "tests/TestCheck.hpp"
#include <fstream>
#ifdef __linux__
namespace OpenHDK::MutationEvidence {
namespace fs=std::filesystem;
using Access=DurableLibraryServiceTestAccess;
inline void hex(std::span<const std::uint8_t> bytes) {
  constexpr char digits[]="0123456789abcdef";
  for(auto x:bytes)std::cout<<digits[x>>4]<<digits[x&15];
}
inline void textHex(const std::optional<std::string>& text) {
  if(!text){std::cout<<"absent";return;}
  hex({reinterpret_cast<const std::uint8_t*>(text->data()),text->size()});
}
inline CheckpointBytes read(const fs::path& path) {
  std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};
}
// Only harness-owned checkpoints. Hints, absolute paths, authority pointers and
// epoch tickets are never emitted; numeric IDs below are detached wire values.
inline bool primary(std::string_view tag,std::string_view phase,std::span<const std::uint8_t> bytes) {
  auto decoded=decodeCatalogCheckpoint(bytes);
  if(!decoded.succeeded() || bytes.size()>16384 || decoded.value->roots.size()>32 || decoded.value->songs.size()>8)return false;
  const auto& p=*decoded.value;
  std::cout<<"MUTATION_PRIMARY case="<<tag<<" phase="<<phase<<" bytes="<<bytes.size()<<" sha256=";
  hex(sourceRevision(bytes).sha256);std::cout<<" digest=";hex(bytes.last(32));
  std::cout<<" sequence="<<p.sequence<<" revision="<<p.catalogRevision<<" nextRoot="<<p.nextRoot<<" nextSong="<<p.nextSong
      <<" roots="<<p.roots.size()<<" songs="<<p.songs.size()<<'\n';
  for(const auto& r:p.roots)std::cout<<"MUTATION_ROOT case="<<tag<<" phase="<<phase<<" id="<<r.id<<" generation="<<r.attachmentGeneration
      <<" mode="<<static_cast<int>(r.policy.mode)<<" encoding="<<static_cast<int>(r.policy.lyrics.encoding)
      <<" track="<<(r.policy.lyrics.trackIndex?std::to_string(*r.policy.lyrics.trackIndex):"none")
      <<" hint_present="<<r.hint.has_value()<<" hint_bytes="<<(r.hint?r.hint->size():0)<<'\n';
  for(const auto& song:p.songs) {
    std::cout<<"MUTATION_SONG case="<<tag<<" phase="<<phase<<" id="<<song.id<<" root="<<song.root<<" locator_hex=";
    hex({reinterpret_cast<const std::uint8_t*>(song.locator.data()),song.locator.size()});
    std::cout<<" title_hex=";textHex(song.title);std::cout<<" artist_hex=";textHex(song.artist);std::cout<<'\n';
  }
  return true;
}
inline const char* callName(LinuxProviderCall call) noexcept {
  switch(call) {
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
inline bool providerTrace(std::string_view tag,DurableLibraryService& service) {
  auto& provider=Access::nativeProvider(service);auto records=LinuxCheckpointProviderTestAccess::records(provider);
  std::cout<<"MUTATION_IO_TRACE case="<<tag<<" count="<<records.size()<<" overflow="<<LinuxCheckpointProviderTestAccess::overflow(provider)<<'\n';
  for(const auto& r:records) {
    std::cout<<"MUTATION_IO case="<<tag<<" call="<<callName(r.call)<<" rc="<<r.result<<" errno="<<r.nativeError<<" injected="<<r.injected<<'\n';
    if(r.call==LinuxProviderCall::CleanupSnapshot) {
      std::cout<<"MUTATION_ARTIFACT case="<<tag<<" dev="<<r.device<<" ino="<<r.inode<<" links="<<r.links<<" bytes="<<r.bytes
          <<" consumed="<<r.consumed<<" captured="<<r.capturedBytes<<" capture_errno="<<r.captureError<<" prefix_sha256=";
      if(r.capturedBytes>=0)hex(sourceRevision({r.wire.data(),static_cast<std::size_t>(r.capturedBytes)}).sha256);else std::cout<<"none";
      std::cout<<'\n';
    }
  }
  return !LinuxCheckpointProviderTestAccess::overflow(provider);
}
struct PublicationProbe {
  DurableLibraryService* service;std::shared_ptr<const CatalogSnapshot> old;
  bool invoked=false,memoryOld=false,cancelLate=false,cancelled=false;
  inline static PublicationProbe* current=nullptr;
  static void hit() noexcept {current->invoked=true;current->memoryOld=current->service->snapshot()==current->old;if(current->cancelLate)current->cancelled=true;}
};
inline std::size_t fdCount() {return static_cast<std::size_t>(std::distance(fs::directory_iterator("/proc/self/fd"),fs::directory_iterator{}));}
inline const char* name(unsigned operation) noexcept {
  constexpr const char* names[]{"register","reattach","scan","relocate","remove"};return names[operation];
}
inline DurableCatalogMutationResult invoke(DurableLibraryService& service,unsigned operation,const fs::path& target,const StoreControl& control={}) {
  const auto snapshot=service.snapshot();
  if(operation==0){auto r=service.registerRoot(target,{},control);return {std::move(r.snapshot),std::move(r.receipt),std::move(r.error)};}
  if(operation==1){auto r=service.reattachRoot(snapshot->roots[0].id,target,control);return {std::move(r.snapshot),std::move(r.receipt),std::move(r.error)};}
  if(operation==2){auto r=service.scan(snapshot->roots[0].id,{}, {},control);return {std::move(r.snapshot),std::move(r.receipt),std::move(r.error)};}
  if(operation==3)return service.relocateSong(snapshot->songs[0].id,snapshot->roots[0].id,"logical.kar",control);
  return service.removeSong(snapshot->songs[0].id,control);
}
inline bool acknowledgment(std::string_view tag,const DurableCatalogMutationResult& result,std::span<const std::uint8_t> bytes) {
  const auto p=decodeCatalogCheckpoint(bytes);if(!p.succeeded())return false;
  bool matches=result.receipt && result.receipt->token.sequence==p.value->sequence && result.receipt->capturedRevision==p.value->catalogRevision
      && std::equal(result.receipt->token.digest.begin(),result.receipt->token.digest.end(),bytes.end()-32);
  std::cout<<"MUTATION_ACK case="<<tag<<" present="<<result.receipt.has_value()<<" token_matches_primary="<<matches
      <<" sequence="<<(result.receipt?result.receipt->token.sequence:0)<<" revision="<<(result.receipt?result.receipt->capturedRevision:0)<<'\n';
  return result.succeeded()?matches:!result.receipt;
}
// Separate owned fixtures for each scenario; no source path is accepted from a
// receipt or used as authority. All observations occur before Temp cleanup.
template<class Owner,class Create,class Open>
int run(const fs::path& parent,Owner makeOwner,Create create,Open open) {
  std::size_t cases=0;
  for(unsigned operation=0;operation<5;++operation)for(unsigned scenario=0;scenario<5;++scenario) {
    constexpr const char* scenarios[]{"saved","write-failure","late-fence","uncertain","late-cancel"};
    const auto tag=std::string(name(operation))+"-"+scenarios[scenario];
    const auto store=parent/("receipt-store-"+tag),root=parent/("receipt-root-"+tag),target=parent/("receipt-target-"+tag);
    fs::create_directory(store);fs::create_directory(target);
    auto started=create(makeOwner(root),store);OPENHDK_FAIL_IF(79,!started.succeeded());auto& service=*started.service;
    auto edited=service.replaceUserOverrides(service.snapshot()->songs[0].id,{"Keep ไทย",{}});
    OPENHDK_FAIL_IF(79,!edited.succeeded());const auto old=service.snapshot();const auto before=read(store/"catalog.ohkcat");
    OPENHDK_FAIL_IF(79,!primary(tag,"before",before));
    const auto oldAdmission=Access::nativeAdmission(service);
    PublicationProbe probe{&service,old};probe.cancelLate=scenario==4;PublicationProbe::current=&probe;
    Access::nativeAfterPublication(service,PublicationProbe::hit);
    AdmissionEvidence::begin();LinuxCheckpointProviderTestAccess::receipts(Access::nativeProvider(service));
    StoreControl control;
    if(scenario==1)Access::nativeFault(service,LinuxProviderFault::Write);
    if(scenario==2)control.checkpoint=[&](StoreCheckpoint point){if(point==StoreCheckpoint::BeforePublication){
      const auto& drift=operation<2?target:root;fs::rename(drift,parent/("receipt-retained-"+tag));fs::create_directory(drift);}};
    if(scenario==3)Access::nativeFault(service,LinuxProviderFault::SyncPublication);
    if(scenario==4)control.cancelled=[&]{return probe.cancelled;};
    const auto result=invoke(service,operation,target,control);
    Access::nativeAfterPublication(service,nullptr);PublicationProbe::current=nullptr;
    const auto after=read(store/"catalog.ohkcat");
    if(scenario==3)LinuxCheckpointProviderTestAccess::snapshotRetainedArtifacts(Access::nativeProvider(service));
    OPENHDK_FAIL_IF(79,!primary(tag,"after",after) || !acknowledgment(tag,result,after)
        || !AdmissionEvidence::trace(tag,"MUTATION_ADMISSION") || !providerTrace(tag,service));
    const bool retained=service.snapshot()==old,wireRetained=before==after;
    const bool mapped=!result.error || !result.error->binding || (result.error->root &&
        (operation==0?std::none_of(old->roots.begin(),old->roots.end(),[&](const auto& r){return r.id==result.error->root;})
            :result.error->root==old->roots[0].id));
    std::cout<<"MUTATION_CASE case="<<tag<<" operation="<<name(operation)<<" scenario="<<scenarios[scenario]
        <<" succeeded="<<result.succeeded()<<" memory_preserved="<<retained<<" primary_preserved="<<wireRetained
        <<" staged_snapshot="<<bool(result.snapshot)<<" pending_cleared="<<!Access::pendingAdmission(service)<<" root_mapping="<<mapped
        <<" guards_shared="<<NativeStoreBindingTestAccess::shared(*oldAdmission,0,*Access::nativeAdmission(service),0)
        <<" memory_at_rename="<<probe.memoryOld<<" rename_observed="<<probe.invoked<<" late_cancelled="<<probe.cancelled
        <<" error="<<(result.error?static_cast<int>(result.error->code):-1)
        <<" store_error="<<(result.error && result.error->store?static_cast<int>(result.error->store->code):-1)
        <<" binding_error="<<(result.error && result.error->binding?static_cast<int>(result.error->binding->code):-1)
        <<" state="<<(service.state()==DurableServiceState::Ready?"Ready":"RecoveryRequired")<<'\n';
    OPENHDK_FAIL_IF(79,!mapped || Access::pendingAdmission(service));
    if(scenario==0 || scenario==4)OPENHDK_FAIL_IF(79,!result.succeeded() || retained || wireRetained || result.snapshot!=service.snapshot());
    OPENHDK_FAIL_IF(79,(scenario==0 || scenario>=3)?!probe.invoked || !probe.memoryOld:probe.invoked);
    OPENHDK_FAIL_IF(79,scenario==4 && !probe.cancelled);
    if(scenario>0 && scenario<4)OPENHDK_FAIL_IF(79,result.succeeded() || !retained || result.snapshot || result.receipt
        || wireRetained!=(scenario!=3) || service.state()!=(scenario==1?DurableServiceState::Ready:DurableServiceState::RecoveryRequired));
    if(scenario==3) {
      started.service.reset();auto recovered=open(store);OPENHDK_FAIL_IF(79,!recovered.succeeded());
      const auto captured=CatalogCheckpointCapture::acquire(Access::owner(*recovered.service));const auto persisted=decodeCatalogCheckpoint(after);
      OPENHDK_FAIL_IF(79,!captured.succeeded());auto recoveredProjection=captured.captured->projection();recoveredProjection.sequence=persisted.value->sequence;
      OPENHDK_FAIL_IF(79,recoveredProjection!=*persisted.value || recovered.service->snapshot()==old
          || !std::all_of(recovered.service->snapshot()->roots.begin(),recovered.service->snapshot()->roots.end(),[&](const auto& r){return Access::attached(*recovered.service,r.id)==false;}));
      std::cout<<"MUTATION_RECOVERY case="<<tag<<" fresh_owner=1 projection_matches=1 unattached=1 revision="<<recovered.service->snapshot()->revision<<'\n';
    }
    ++cases;
  }
  // Targeted missing-root repair and valid same-path NoChange.
  {
    const auto store=parent/"receipt-repair-store",root=parent/"receipt-repair-root",target=parent/"receipt-repair-target";
    fs::create_directory(store);fs::create_directory(target);auto started=create(makeOwner(root),store);auto& service=*started.service;
    const auto old=service.snapshot();const auto before=read(store/"catalog.ohkcat");fs::rename(root,parent/"receipt-missing-old");
    OPENHDK_FAIL_IF(80,!primary("targeted-repair","before",before));AdmissionEvidence::begin();
    auto repair=service.reattachRoot(old->roots[0].id,target);const auto repaired=read(store/"catalog.ohkcat");
    OPENHDK_FAIL_IF(80,!repair.succeeded() || !primary("targeted-repair","after",repaired)
        || !AdmissionEvidence::trace("targeted-repair","MUTATION_ADMISSION"));
    std::cout<<"MUTATION_SPECIAL case=targeted-repair old_missing=1 target_only=1 saved=1\n";++cases;
    const auto prior=service.snapshot();OPENHDK_FAIL_IF(80,!primary("same-path-nochange","before",repaired));AdmissionEvidence::begin();
    auto noop=service.reattachRoot(prior->roots[0].id,target);const auto unchanged=read(store/"catalog.ohkcat");
    OPENHDK_FAIL_IF(80,!noop.succeeded() || noop.status!=RootReattachmentStatus::Unchanged || service.snapshot()!=prior
        || unchanged!=repaired || !primary("same-path-nochange","after",unchanged)
        || !AdmissionEvidence::trace("same-path-nochange","MUTATION_ADMISSION"));
    std::cout<<"MUTATION_SPECIAL case=same-path-nochange memory_preserved=1 primary_preserved=1 token_preserved="<<(noop.receipt->token==repair.receipt->token)<<'\n';++cases;
  }
  // Equal-key relocation invalidates; removal/rescan cannot resurrect overrides.
  {
    const auto store=parent/"receipt-lifecycle-store",root=parent/"receipt-lifecycle-root";fs::create_directory(store);
    auto source=makeOwner(root);const auto history=source->snapshot();auto prepared=source->prepare(history,history->songs[0].id);
    auto started=create(std::move(source),store);auto& service=*started.service;const auto id=history->songs[0].id;const auto rootId=history->roots[0].id;
    auto edited=service.replaceUserOverrides(id,{"Keep ไทย",{}});OPENHDK_FAIL_IF(81,!edited.succeeded());
    const auto before=read(store/"catalog.ohkcat");OPENHDK_FAIL_IF(81,!primary("equal-key-relocate","before",before));
    auto moved=service.relocateSong(id,rootId,"ready.kar");const auto invalidated=read(store/"catalog.ohkcat");
    OPENHDK_FAIL_IF(81,!moved.succeeded() || moved.snapshot->songs[0].state!=CatalogState::Invalid || moved.snapshot->songs[0].sourceRevision()
        || moved.snapshot->songs[0].metadata || !primary("equal-key-relocate","after",invalidated));
    std::cout<<"MUTATION_SPECIAL case=equal-key-relocate source_invalid=1 same_key=1 overrides_retained="<<(moved.snapshot->songs[0].overrides==edited.snapshot->songs[0].overrides)<<'\n';++cases;
    OPENHDK_FAIL_IF(81,!primary("remove-rediscovery","before",invalidated));auto removed=service.removeSong(id);
    OPENHDK_FAIL_IF(81,!removed.succeeded() || !primary("remove-rediscovery","removed",read(store/"catalog.ohkcat")));
    auto scanned=service.scan(rootId);const auto after=read(store/"catalog.ohkcat");
    OPENHDK_FAIL_IF(81,!scanned.succeeded() || !primary("remove-rediscovery","after",after)
        || scanned.snapshot->songs[0].id==id || scanned.snapshot->songs[0].overrides || !prepared.succeeded()
        || prepared.prepared->lyrics()->cues().size()!=1 || !fs::exists(root/"ready.kar"));
    std::cout<<"MUTATION_SPECIAL case=remove-rediscovery new_id=1 removed_overrides_absent=1 file_preserved=1 prepared_retained=1\n";++cases;
  }

  for(unsigned scenario=0;scenario<3;++scenario) {
    constexpr const char* tags[]{"same-path-drift","unrelated-drift","store-containment"};const std::string tag=tags[scenario];
    const auto store=parent/("receipt-special-store-"+tag),root=parent/("receipt-special-root-"+tag),other=parent/("receipt-other-"+tag),target=parent/("receipt-special-target-"+tag);
    fs::create_directory(store);fs::create_directory(other);fs::create_directory(target);auto source=makeOwner(root);
    auto otherId=source->registerRoot(other).root;OPENHDK_FAIL_IF(80,!otherId);
    auto started=create(std::move(source),store);auto& service=*started.service;const auto old=service.snapshot();const auto before=read(store/"catalog.ohkcat");
    OPENHDK_FAIL_IF(80,!primary(tag,"before",before));AdmissionEvidence::begin();
    if(scenario<2){const auto& replaced=scenario==0?root:other;fs::rename(replaced,parent/("receipt-original-"+tag));fs::create_directory(replaced);}
    auto rejected=invoke(service,scenario==2?0:1,scenario==2?parent:(scenario==0?root:target));
    const auto after=read(store/"catalog.ohkcat");
    OPENHDK_FAIL_IF(80,rejected.succeeded() || rejected.snapshot || rejected.receipt || service.snapshot()!=old || before!=after
        || !primary(tag,"after",after) || !AdmissionEvidence::trace(tag,"MUTATION_ADMISSION") || Access::pendingAdmission(service));
    if(scenario==1)OPENHDK_FAIL_IF(80,rejected.error->root!=otherId);
    std::cout<<"MUTATION_SPECIAL case="<<tag<<" memory_preserved=1 primary_preserved=1 root_mapping="<<(scenario!=1 || rejected.error->root==otherId)
        <<" binding_error="<<static_cast<int>(rejected.error->binding->code)<<" state="<<(service.state()==DurableServiceState::Ready?"Ready":"RecoveryRequired")<<'\n';++cases;
  }
  {
    const auto store=parent/"receipt-hint-store",root=parent/"receipt-hint-root";fs::create_directory(store);
    auto original=create(makeOwner(root),store);OPENHDK_FAIL_IF(80,!original.succeeded());original.service.reset();auto restored=open(store);
    OPENHDK_FAIL_IF(80,!restored.succeeded());auto& service=*restored.service;const auto before=read(store/"catalog.ohkcat");const auto old=service.snapshot();
    OPENHDK_FAIL_IF(80,Access::attached(service,old->roots[0].id)!=false || !primary("saved-hint-attachment","before",before));AdmissionEvidence::begin();
    auto attached=service.reattachRoot(old->roots[0].id,root);const auto after=read(store/"catalog.ohkcat");
    OPENHDK_FAIL_IF(80,!attached.succeeded() || attached.status!=RootReattachmentStatus::Updated || Access::attached(service,old->roots[0].id)!=true
        || !primary("saved-hint-attachment","after",after) || !AdmissionEvidence::trace("saved-hint-attachment","MUTATION_ADMISSION"));
    std::cout<<"MUTATION_SPECIAL case=saved-hint-attachment before_unattached=1 explicit_selection=1 changed=1\n";++cases;
  }
  {
    const auto store=parent/"receipt-peak-store",root=parent/"receipt-peak-root",target=parent/"receipt-peak-target";fs::create_directory(store);fs::create_directory(target);
    auto source=makeOwner(root);for(unsigned i=1;i<32;++i){const auto r=parent/("receipt-peak-other-"+std::to_string(i));fs::create_directory(r);if(!source->registerRoot(r).root)return 80;}
    auto started=create(std::move(source),store);OPENHDK_FAIL_IF(80,!started.succeeded());auto& service=*started.service;const auto old=service.snapshot();const auto before=read(store/"catalog.ohkcat");
    OPENHDK_FAIL_IF(80,!primary("descriptor-peak","before",before));const auto beforeFds=fdCount();AdmissionEvidence::begin();
    auto rejected=service.reattachRoot(old->roots[0].id,target);const auto afterFds=fdCount();const auto after=read(store/"catalog.ohkcat");
    OPENHDK_FAIL_IF(80,rejected.succeeded() || rejected.error->binding->code!=NativeBindingErrorCode::LimitExceeded
        || before!=after || service.snapshot()!=old || beforeFds!=afterFds || !primary("descriptor-peak","after",after)
        || !AdmissionEvidence::trace("descriptor-peak","MUTATION_ADMISSION"));
    std::cout<<"MUTATION_SPECIAL case=descriptor-peak active_roots=32 prospective_peak=46 descriptor_limit=45 rejected=1 fd_before="<<beforeFds<<" fd_after="<<afterFds<<" memory_preserved=1 primary_preserved=1\n";++cases;
  }
  std::cout<<"MUTATION_SUMMARY cases="<<cases<<" native_acceptance=Pending paths_emitted=0 namespace_alias=Skipped\n";
  return 0;
}
} // namespace OpenHDK::MutationEvidence
#endif
