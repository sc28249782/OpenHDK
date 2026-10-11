// SPDX-License-Identifier: GPL-3.0-or-later
#include "tests/PreparationFakeProvider.hpp"
#include "app/LibraryCliSession.hpp"
#include "tests/TestCheck.hpp"
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstdlib>
namespace { std::ptrdiff_t failAfter = -1; }
#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif
TEST_NOINLINE void* operator new(std::size_t n) {
  if (failAfter == 0) throw std::bad_alloc();
  if (failAfter > 0) --failAfter;
  if (auto p = std::malloc(n ? n : 1U)) return p;
  throw std::bad_alloc();
}
TEST_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
TEST_NOINLINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
#undef TEST_NOINLINE
using namespace OpenHDK;
using namespace PreparationFixture;
using Access=DurableLibraryServiceTestAccess;
int main() {
  const auto base=std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR")?std::filesystem::path(std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR")):std::filesystem::temp_directory_path();
  const auto dir=base/
      ("openhdk-durable-prepare-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(dir);
  struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}} cleanup{dir};
  const auto file=dir/"song.kar";
  const std::vector<std::uint8_t> bytes{'M','T','h','d',0,0,0,6,0,0,0,1,0,3,'M','T','r','k',0,0,0,9,0,0xff,5,1,'A',0,0xff,0x2f,0};
  const auto write=[&](const auto& data){std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),data.size());};write(bytes);
  auto owner=std::make_unique<SongDiscovery>();owner->registerRoot(dir);owner->scan(owner->snapshot()->roots[0].id);
  Image image;auto provider=std::make_unique<Fake>(image);auto* p=provider.get();
  auto opened=Access::create(std::move(owner),std::move(provider));
  OPENHDK_FAIL_IF(1,!opened.succeeded());
  auto& service=*opened.service;const auto snapshot=service.snapshot();const auto id=snapshot->songs[0].id;
  const auto wire=*image.primary;const auto saves=p->publications,reads=p->reads;
  auto prepared=service.prepare(id);
  OPENHDK_FAIL_IF(2,!prepared.succeeded() || prepared.prepared->catalogSnapshot()!=snapshot || prepared.prepared->lyrics()->cues().size()!=1);
  OPENHDK_FAIL_IF(3,image.primary!=wire || p->publications!=saves || p->reads!=reads || service.snapshot()!=snapshot);
  PreparationOptions override;override.lyricOverride=LyricSelectionPolicy{LyricTextEncoding::Tis620,0};
  auto selected=service.prepare(id,override);
  OPENHDK_FAIL_IF(4,!selected.succeeded() || selected.prepared->effectivePolicy().encoding!=LyricTextEncoding::Tis620);
  override.lyricOverride->trackIndex=99;auto invalidTrack=service.prepare(id,override);
  OPENHDK_FAIL_IF(5,!invalidTrack.error || invalidTrack.error->code!=DurableServiceErrorCode::PreparationFailure
      || invalidTrack.error->preparation->code!=PreparationErrorCode::InvalidLyrics || !invalidTrack.error->preparation->lyricError);
  OPENHDK_FAIL_IF(6,service.state()!=DurableServiceState::Ready || image.primary!=wire);
  auto changed=bytes;changed[26]='B';write(changed);auto stale=service.prepare(id);
  OPENHDK_FAIL_IF(7,!stale.error || stale.error->preparation->code!=PreparationErrorCode::SourceChanged || service.state()!=DurableServiceState::Ready);
  write(bytes);
  PreparationOptions limit;limit.stagedBytes=1;
  OPENHDK_FAIL_IF(8,service.prepare(id,limit).error->preparation->code!=PreparationErrorCode::LimitExceeded);
  limit.stagedBytes=MetadataPayloadBudget::kMaxBytes+1;
  OPENHDK_FAIL_IF(9,service.prepare(id,limit).error->preparation->code!=PreparationErrorCode::InvalidConfiguration);
  OPENHDK_FAIL_IF(10,service.prepare(id,{},{},MetadataPayloadBudget::kMaxBytes).error->preparation->code!=PreparationErrorCode::LimitExceeded);
  bool cancelled=true;
  OPENHDK_FAIL_IF(11,service.prepare(id,{},PreparationControl{[&]{return cancelled;},{}}).error->preparation->code!=PreparationErrorCode::Cancelled);
  bool sawBusy=false;PreparationControl reentry;
  reentry.checkpoint=[&](PreparationCheckpoint){auto r=service.removeSong(id);sawBusy=r.error && r.error->code==DurableServiceErrorCode::Busy;};
  OPENHDK_FAIL_IF(12,!service.prepare(id,{},reentry).succeeded() || !sawBusy || image.primary!=wire);
  PreparationControl lateChange;lateChange.checkpoint=[&](PreparationCheckpoint point){if(point==PreparationCheckpoint::AfterExtraction)write(changed);};
  auto late=service.prepare(id,{},lateChange);write(bytes);
  OPENHDK_FAIL_IF(13,!late.error || late.error->preparation->code!=PreparationErrorCode::SourceChanged
      || late.error->preparation->operation!=PreparationOperation::Verify || late.prepared);
  // The returned value owns everything through later overrides and logical removal.
  OPENHDK_FAIL_IF(14,!service.replaceUserOverrides(id,{"ไทย","Artist"}).succeeded());
  auto titled=service.prepare(id);OPENHDK_FAIL_IF(15,!titled.succeeded() || titled.prepared->displayMetadata().title.text!="ไทย");
  const auto beforeRemoval=*image.primary;
  OPENHDK_FAIL_IF(16,!service.removeSong(id).succeeded() || prepared.prepared->midi().events().empty() || prepared.prepared->song().state!=CatalogState::Ready);
  OPENHDK_FAIL_IF(17,service.prepare(id).error->preparation->code!=PreparationErrorCode::NotFound);
  service.close();OPENHDK_FAIL_IF(18,service.prepare(id).error->code!=DurableServiceErrorCode::InvalidConfiguration);
  auto restored=Access::open(std::make_unique<Fake>(image));
  OPENHDK_FAIL_IF(19,!restored.succeeded() || restored.service->snapshot()->songs.size()!=0);
  restored.service->close();

  Image restoredImage;restoredImage.primary=beforeRemoval;
  auto unattached=Access::open(std::make_unique<Fake>(restoredImage));
  const auto restoredSnapshot=unattached.service->snapshot();const auto restoredId=restoredSnapshot->songs[0].id;
  unsigned checkpoints=0;PreparationControl countHooks;countHooks.checkpoint=[&](PreparationCheckpoint){++checkpoints;};
  auto unbound=unattached.service->prepare(restoredId,{},countHooks);
  OPENHDK_FAIL_IF(31,!unbound.error || unbound.error->preparation->code!=PreparationErrorCode::InvalidRoot || checkpoints!=0);
  OPENHDK_FAIL_IF(32,!unattached.service->reattachRoot(restoredSnapshot->roots[0].id,dir).succeeded()
      || unattached.service->prepare(restoredId).error->preparation->code!=PreparationErrorCode::NotReady);
  OPENHDK_FAIL_IF(33,!unattached.service->scan(restoredSnapshot->roots[0].id).succeeded());
  auto resumed=unattached.service->prepare(restoredId);unattached.service->close();unattached.service.reset();
  OPENHDK_FAIL_IF(34,!resumed.succeeded() || resumed.prepared->displayMetadata().title.text!="ไทย" || resumed.prepared->lyrics()->cues().size()!=1);
  // Fresh fixture owner tests authority failure separately from source errors.
  auto fresh=std::make_unique<SongDiscovery>();fresh->registerRoot(dir);fresh->scan(fresh->snapshot()->roots[0].id);
  Image authorityImage;auto authority=Access::create(std::move(fresh),std::make_unique<Fake>(authorityImage));
  const auto aid=authority.service->snapshot()->songs[0].id;const auto authorityWire=*authorityImage.primary;
  Access::failAdmission(*authority.service,true);PreparationOptions invalidConfig;invalidConfig.stagedBytes=0;
  const auto checks=Access::admissionChecks(*authority.service);
  auto rejectedConfig=authority.service->prepare(aid,invalidConfig);
  OPENHDK_FAIL_IF(35,!rejectedConfig.error || rejectedConfig.error->preparation->code!=PreparationErrorCode::InvalidConfiguration
      || authority.service->state()!=DurableServiceState::Ready || Access::admissionChecks(*authority.service)!=checks);
  Access::failAdmission(*authority.service,false);
  PreparationControl drift;drift.checkpoint=[&](PreparationCheckpoint point){if(point==PreparationCheckpoint::AfterExtraction)Access::failAdmission(*authority.service,true);};
  auto drifted=authority.service->prepare(aid,{},drift);
  OPENHDK_FAIL_IF(20,!drifted.error || drifted.error->code!=DurableServiceErrorCode::NativeAdmissionFailure || drifted.prepared
      || authority.service->state()!=DurableServiceState::RecoveryRequired || authorityImage.primary!=authorityWire);
  OPENHDK_FAIL_IF(21,authority.service->prepare(aid).error->code!=DurableServiceErrorCode::RecoveryRequired);
  authority.service->close();
  auto cliOwner=std::make_unique<SongDiscovery>();cliOwner->registerRoot(dir);cliOwner->scan(cliOwner->snapshot()->roots[0].id);
  Image cliImage;auto cliService=Access::create(std::move(cliOwner),std::make_unique<Fake>(cliImage));
  auto* cli=cliService.service.get();LibraryCliSession session(std::move(cliService.service));const auto cliWire=*cliImage.primary;
  auto summary=session.dispatch("prepare 1");
  OPENHDK_FAIL_IF(22,!summary.output.bytes || summary.output.bytes->find("\"type\":\"prepared\"")==std::string::npos
      || summary.output.bytes->find("\"cue_count\":1")==std::string::npos || summary.output.bytes->find("Saved")!=std::string::npos
      || summary.commit!=LibraryCliCommitOutcome::Unknown || cliImage.primary!=cliWire);
  write(changed);auto contentError=session.dispatch("prepare 1");write(bytes);
  OPENHDK_FAIL_IF(23,!contentError.output.bytes || contentError.output.bytes->find("preparation_code")==std::string::npos || contentError.terminal);
  Access::failAdmission(*cli,true);auto cliDrift=session.dispatch("prepare 1");
  OPENHDK_FAIL_IF(24,!cliDrift.terminal || cliDrift.exit!=76 || cliImage.primary!=cliWire);

  bool allocationPassed=false;
  auto sweepOwner=std::make_unique<SongDiscovery>();sweepOwner->registerRoot(dir);sweepOwner->scan(sweepOwner->snapshot()->roots[0].id);
  Image sweepImage;auto sweep=Access::create(std::move(sweepOwner),std::make_unique<Fake>(sweepImage));
  const auto sweepId=sweep.service->snapshot()->songs[0].id;const auto sweepWire=*sweepImage.primary;const auto sweepSnapshot=sweep.service->snapshot();
  for(std::ptrdiff_t n=0;n<512;++n) {
    failAfter=n;auto result=sweep.service->prepare(sweepId);failAfter=-1;
    OPENHDK_FAIL_IF(25,sweepImage.primary!=sweepWire || sweep.service->snapshot()!=sweepSnapshot || sweep.service->state()!=DurableServiceState::Ready
        || (!result.succeeded() && (!result.error || result.prepared)));
    if(result.succeeded()) {allocationPassed=true;break;}
  }
  OPENHDK_FAIL_IF(26,!allocationPassed);
  sweep.service->close();
#ifdef __linux__
  const auto store=dir/"store",songs=dir/"native-songs";std::filesystem::create_directory(store);std::filesystem::create_directory(songs);
  {std::ofstream out(songs/"native.kar",std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
  auto nativeOwner=std::make_unique<SongDiscovery>();nativeOwner->registerRoot(songs);nativeOwner->scan(nativeOwner->snapshot()->roots[0].id);
  auto native=std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR")
      ? DurableLibraryService::createLinux(std::move(nativeOwner),store.string(),"catalog.ohk")
      : Access::createNative(std::move(nativeOwner),store.string(),"catalog.ohk");
  OPENHDK_FAIL_IF(27,!native.succeeded());
  const auto nativeSnapshot=native.service->snapshot();const auto nativeId=nativeSnapshot->songs[0].id;
  const auto read=[&](){std::ifstream in(store/"catalog.ohk",std::ios::binary);return CheckpointBytes{std::istreambuf_iterator<char>(in),{}};};
  const auto nativeWire=read();auto nativePrepared=native.service->prepare(nativeId);
  OPENHDK_FAIL_IF(28,!nativePrepared.succeeded() || read()!=nativeWire || native.service->snapshot()!=nativeSnapshot);
  PreparationControl nativeDrift;nativeDrift.checkpoint=[&](PreparationCheckpoint point){
    if(point==PreparationCheckpoint::AfterExtraction) {
      std::filesystem::rename(songs,dir/"old-native-songs");std::filesystem::create_directory(songs);
    }
  };
  auto replaced=native.service->prepare(nativeId,{},nativeDrift);
  OPENHDK_FAIL_IF(29,!replaced.error || replaced.error->code!=DurableServiceErrorCode::NativeAdmissionFailure || replaced.prepared
      || replaced.error->root!=nativeSnapshot->roots[0].id || read()!=nativeWire
      || native.service->snapshot()!=nativeSnapshot || native.service->state()!=DurableServiceState::RecoveryRequired);
  OPENHDK_FAIL_IF(30,nativePrepared.prepared->lyrics()->cues().size()!=1 || !replaced.error->preparation
      || replaced.error->preparation->code!=PreparationErrorCode::SourceChanged);
  native.service->close();
  std::cout<<"{\"native_query_regression\":true,\"filesystem_bypass\":"<<(std::getenv("OPENHDK_NATIVE_CHECKPOINT_DIR")?"false":"true")<<",\"acceptance\":\"Pending\"}\n";
#endif
  std::cout<<*summary.output.bytes<<*contentError.output.bytes<<*cliDrift.output.bytes;
  return 0;
}
