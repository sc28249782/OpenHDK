// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "library/SongDiscovery.hpp"
#include "lyrics/LyricClockObserver.hpp"
#include "tests/TestCheck.hpp"
#include <chrono>
#include <fstream>
#include <stdexcept>
namespace fs=std::filesystem;
using namespace OpenHDK;
namespace {
struct TemporaryRoot {
  fs::path path=fs::temp_directory_path() / ("openhdk-preparation-"+
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryRoot(){if(!fs::create_directory(path))throw std::runtime_error("fixture directory failed");}
  ~TemporaryRoot(){std::error_code ec;fs::remove_all(path,ec);}
};
using Bytes=std::vector<std::uint8_t>;
Bytes midi(std::string text="A") {
  Bytes track{0,0xff,5,static_cast<std::uint8_t>(text.size())};
  track.insert(track.end(),text.begin(),text.end());
  track.insert(track.end(),{1,0xff,5,1,'B',0,0xff,0x2f,0});
  Bytes bytes{'M','T','h','d',0,0,0,6,0,0,0,1,0,3,'M','T','r','k'};
  for(auto shift:{24U,16U,8U,0U})bytes.push_back(static_cast<std::uint8_t>(track.size()>>shift));
  bytes.insert(bytes.end(),track.begin(),track.end());return bytes;
}
void write(const fs::path& path,const Bytes& bytes){
  std::ofstream f(path,std::ios::binary|std::ios::trunc);
  f.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
  if(!f)throw std::runtime_error("fixture write failed");
}
bool failed(const SongPreparationResult& r,PreparationErrorCode code){
  return !r.succeeded() && !r.prepared && r.error && r.error->code==code && !r.error->message().empty();
}
}
int main(){
  TemporaryRoot temp;const auto file=temp.path/"song.kar";const auto bytes=midi();write(file,bytes);
  SongDiscovery library;const auto registered=library.registerRoot(temp.path);
  OPENHDK_FAIL_IF(1,!registered.root || !library.scan(*registered.root).succeeded());
  const auto snapshot=library.snapshot();const auto id=snapshot->songs[0].id;
  auto ready=library.prepare(snapshot,id);
  OPENHDK_FAIL_IF(2,!ready.succeeded() || ready.error || ready.prepared->song().id!=id
      || ready.prepared->catalogSnapshot()!=snapshot || ready.prepared->song().sourceRevision!=sourceRevision(bytes)
      || ready.prepared->lyrics()->cues().size()!=2 || ready.prepared->lyrics()->cues()[0].decoded!="A"
      || ready.prepared->lyrics()->cues()[1].position.timeMicroseconds!=166666);
  OPENHDK_FAIL_IF(3,!failed(library.prepare(nullptr,id),PreparationErrorCode::InvalidConfiguration));
  auto absent=std::make_shared<CatalogSnapshot>(*snapshot);absent->songs.clear();
  OPENHDK_FAIL_IF(4,!failed(library.prepare(absent,id),PreparationErrorCode::NotFound));
  for(auto state:{CatalogState::Invalid,CatalogState::Missing,CatalogState::UnsupportedProfile}){
    auto bad=std::make_shared<CatalogSnapshot>(*snapshot);bad->songs[0].state=state;
    OPENHDK_FAIL_IF(5,!failed(library.prepare(bad,id),PreparationErrorCode::NotReady));
  }
  auto noToken=std::make_shared<CatalogSnapshot>(*snapshot);noToken->songs[0].sourceRevision.reset();
  OPENHDK_FAIL_IF(6,!failed(library.prepare(noToken,id),PreparationErrorCode::NotReady));
  PreparationOptions small;small.smfBytes=bytes.size();
  OPENHDK_FAIL_IF(7,!library.prepare(snapshot,id,small).succeeded());
  --small.smfBytes;
  OPENHDK_FAIL_IF(8,!failed(library.prepare(snapshot,id,small),PreparationErrorCode::LimitExceeded));
  small.smfBytes=0;
  OPENHDK_FAIL_IF(9,!failed(library.prepare(snapshot,id,small),PreparationErrorCode::InvalidConfiguration));
  const auto time=fs::last_write_time(file);write(file,midi("Z"));fs::last_write_time(file,time);
  OPENHDK_FAIL_IF(10,!failed(library.prepare(snapshot,id),PreparationErrorCode::SourceChanged)
      || library.snapshot()!=snapshot || ready.prepared->lyrics()->cues()[0].decoded!="A");
  OPENHDK_FAIL_IF(11,!library.scan(*registered.root).succeeded()
      || library.snapshot()->songs[0].id!=id);
  const auto updated=library.snapshot();const auto replacement=library.prepare(updated,id);
  OPENHDK_FAIL_IF(12,!replacement.succeeded() || replacement.prepared->lyrics()->cues()[0].decoded!="Z"
      || ready.prepared->catalogSnapshot()!=snapshot);
  write(file,bytes);
  PreparationControl control;
  control.cancelled=[] {return true;};
  OPENHDK_FAIL_IF(13,!failed(library.prepare(snapshot,id,{},control),PreparationErrorCode::Cancelled));
  for(auto point:{PreparationCheckpoint::AfterRead,PreparationCheckpoint::AfterExtraction}){
    control.cancelled={};control.checkpoint=[&](auto at){if(at==point)write(file,midi("X"));};
    OPENHDK_FAIL_IF(14,!failed(library.prepare(snapshot,id,{},control),PreparationErrorCode::SourceChanged)
        || library.snapshot()!=updated || ready.prepared->lyrics()->cues()[0].decoded!="A");
    write(file,bytes);
  }
  bool cancel=false;control.cancelled=[&]{return cancel;};
  control.checkpoint=[&](auto at){if(at==PreparationCheckpoint::AfterExtraction)cancel=true;};
  OPENHDK_FAIL_IF(15,!failed(library.prepare(snapshot,id,{},control),PreparationErrorCode::Cancelled));
  fs::remove(file);
  OPENHDK_FAIL_IF(16,!failed(library.prepare(snapshot,id),PreparationErrorCode::SourceUnreadable));
  write(file,bytes);
  control={};control.checkpoint=[&](auto at){if(at==PreparationCheckpoint::AfterExtraction)fs::remove(file);};
  OPENHDK_FAIL_IF(17,!failed(library.prepare(snapshot,id,{},control),PreparationErrorCode::SourceChanged));
  // Native no-follow reader rejects a substituted symlink where supported.
  std::error_code ec;fs::create_symlink("other.kar",file,ec);
  if(!ec){OPENHDK_FAIL_IF(18,!failed(library.prepare(snapshot,id),PreparationErrorCode::AmbiguousPath));fs::remove(file);}
  write(file,midi(std::string(1,static_cast<char>(0xa1))));
  OPENHDK_FAIL_IF(19,!library.scan(*registered.root).succeeded());
  auto thai=library.snapshot();auto invalidText=library.prepare(thai,id);
  OPENHDK_FAIL_IF(20,!failed(invalidText,PreparationErrorCode::InvalidLyrics)
      || !invalidText.error->lyricError || invalidText.error->lyricError->code!=KarLyricErrorCode::InvalidText
      || !invalidText.error->lyricError->payloadByteOffset);
  PreparationOptions tis;tis.lyrics.encoding=LyricTextEncoding::Tis620;
  auto decoded=library.prepare(thai,id,tis);
  OPENHDK_FAIL_IF(21,!decoded.succeeded() || decoded.prepared->lyrics()->cues()[0].decoded!="\xe0\xb8\x81"
      || decoded.prepared->options().lyrics.encoding!=LyricTextEncoding::Tis620);
  tis.lyrics.limits.cues=1;
  OPENHDK_FAIL_IF(22,!failed(library.prepare(thai,id,tis),PreparationErrorCode::LimitExceeded));
  tis.lyrics.trackIndex=99;
  auto invalidTrack=library.prepare(thai,id,tis);
  OPENHDK_FAIL_IF(23,!failed(invalidTrack,PreparationErrorCode::InvalidLyrics)
      || invalidTrack.error->lyricError->code!=KarLyricErrorCode::InvalidLyricTrack);
  // Inject a Ready descriptor to verify nested canonical-validation errors.
  Bytes corrupt{0};write(file,corrupt);
  auto forged=std::make_shared<CatalogSnapshot>(*snapshot);forged->songs[0].sourceRevision=sourceRevision(corrupt);
  auto invalidSmf=library.prepare(forged,id);
  OPENHDK_FAIL_IF(24,!failed(invalidSmf,PreparationErrorCode::InvalidSmf) || !invalidSmf.error->parseError);
  auto malformed=bytes;malformed[23]=0xf4;write(file,malformed);
  forged->songs[0].sourceRevision=sourceRevision(malformed);
  auto invalidEvents=library.prepare(forged,id);
  OPENHDK_FAIL_IF(25,!failed(invalidEvents,PreparationErrorCode::InvalidSmf)
      || !invalidEvents.error->timelineError || !invalidEvents.error->timelineError->trackDecodeError());
  // Prepared lyrics can be bound to an acknowledged start without losing identity.
  LyricClockObserver observer;
  OPENHDK_FAIL_IF(26,!observer.bind(ready.prepared->lyrics(),{1}));
  MediaClockPublicationCell cell;
  OPENHDK_FAIL_IF(27,cell.publish({1,166666,MediaClockSource::CompiledTimeline,
      MediaClockPhase::Finished,MediaClockFailure::None})!=MediaClockPublishStatus::Published);
  auto due=observer.poll(cell.tryRead());
  OPENHDK_FAIL_IF(28,!due.batch || due.batch->cues().size()!=2 || ready.prepared->song().id!=id);
  // NoLyrics is a successful preparation; source/owner lifetime is independent.
  const auto retained=[] {
    TemporaryRoot local;write(local.path/"plain.mid",Bytes{'M','T','h','d',0,0,0,6,0,0,0,1,0,3,
        'M','T','r','k',0,0,0,4,0,0xff,0x2f,0});
    SongDiscovery owner;const auto root=owner.registerRoot(local.path);
    if(!root.root || !owner.scan(*root.root).succeeded())throw std::runtime_error("scan failed");
    auto snap=owner.snapshot();return owner.prepare(snap,snap->songs[0].id);
  }();
  OPENHDK_FAIL_IF(29,!retained.succeeded() || retained.prepared->lyrics()->profile()!=KarLyricProfile::NoLyrics
      || retained.prepared->midi().events().size()!=1 || retained.prepared->song().locator!="plain.mid");
  write(file,bytes);
  PreparationOptions invalidConfig;invalidConfig.smfBytes=64U*1048576U+1U;
  OPENHDK_FAIL_IF(30,!failed(library.prepare(snapshot,id,invalidConfig),PreparationErrorCode::InvalidConfiguration));
  invalidConfig={};invalidConfig.lyrics.encoding=static_cast<LyricTextEncoding>(999);
  const auto invalidEncoding=library.prepare(snapshot,id,invalidConfig);
  OPENHDK_FAIL_IF(31,!failed(invalidEncoding,PreparationErrorCode::InvalidConfiguration)
      || !invalidEncoding.error->lyricError);
  PreparationOptions lyricBound;lyricBound.lyrics.limits.sourceBytes=2;
  OPENHDK_FAIL_IF(32,!library.prepare(snapshot,id,lyricBound).succeeded());
  lyricBound.lyrics.limits.sourceBytes=1;
  OPENHDK_FAIL_IF(33,!failed(library.prepare(snapshot,id,lyricBound),PreparationErrorCode::LimitExceeded));
  auto removedRoot=temp.path;fs::rename(temp.path,temp.path.string()+"-moved");
  const auto rootFailure=library.prepare(snapshot,id);
  fs::rename(temp.path.string()+"-moved",removedRoot);
  OPENHDK_FAIL_IF(34,!failed(rootFailure,PreparationErrorCode::InvalidRoot)
      || ready.prepared->lyrics()->cues()[0].decoded!="A");
  // Distinct catalogs can have identical local IDs, locators and content.
  TemporaryRoot foreignRoot;write(foreignRoot.path/"song.kar",bytes);
  SongDiscovery foreign;const auto foreignRegistration=foreign.registerRoot(foreignRoot.path);
  OPENHDK_FAIL_IF(35,!foreignRegistration.root || !foreign.scan(*foreignRegistration.root).succeeded());
  const auto foreignSnapshot=foreign.snapshot();
  OPENHDK_FAIL_IF(36,foreignSnapshot->songs[0].id!=id
      || foreignSnapshot->songs[0].root!=snapshot->songs[0].root
      || foreignSnapshot->songs[0].sourceRevision!=snapshot->songs[0].sourceRevision);
  unsigned checkpoints=0U;PreparationControl rejectedControl;
  rejectedControl.cancelled=[&]{++checkpoints;return false;};
  rejectedControl.checkpoint=[&](auto){++checkpoints;};
  const auto foreignResult=library.prepare(foreignSnapshot,id,{},rejectedControl);
  OPENHDK_FAIL_IF(37,!failed(foreignResult,PreparationErrorCode::InvalidConfiguration)
      || foreignResult.error->operation!=PreparationOperation::Resolve || checkpoints!=0U
      || library.snapshot()!=thai || ready.prepared->lyrics()->cues()[0].decoded!="A");
  auto unowned=std::make_shared<CatalogSnapshot>();unowned->songs=snapshot->songs;
  OPENHDK_FAIL_IF(38,!failed(library.prepare(unowned,id),PreparationErrorCode::InvalidConfiguration));
  // Rescan does not invalidate older snapshots from the original catalog.
  OPENHDK_FAIL_IF(39,!library.prepare(snapshot,id).succeeded());
  // Retaining an old token prevents address reuse after its owner dies.
  const auto orphan=[] { SongDiscovery oldOwner;return oldOwner.snapshot(); }();
  SongDiscovery newOwner;
  OPENHDK_FAIL_IF(40,!failed(newOwner.prepare(orphan,id),PreparationErrorCode::InvalidConfiguration));
  return 0;
}
