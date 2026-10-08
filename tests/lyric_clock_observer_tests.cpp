// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "lyrics/LyricClockObserver.hpp"
#include "audio/PlaybackSession.hpp"
#include "audio/SmfParser.hpp"
#include "tests/TestCheck.hpp"
#include <stdexcept>
#include <type_traits>
#include <vector>
using namespace OpenHDK;
namespace {
SmfTimelineCompileResult compile(bool lyrics = true) {
  std::vector<std::uint8_t> track;
  if (lyrics) track = {0,0xff,5,1,'A',1,0xff,5,1,'B',0,0xff,5,1,'C',1,0xff,5,1,'D'};
  track.insert(track.end(), {0,0xff,0x2f,0});
  std::vector<std::uint8_t> bytes{'M','T','h','d',0,0,0,6,0,0,0,1,0,3,'M','T','r','k'};
  for (auto shift : {24U,16U,8U,0U}) bytes.push_back(static_cast<std::uint8_t>(track.size() >> shift));
  bytes.insert(bytes.end(), track.begin(), track.end());
  const auto parsed = SmfParser::parse(bytes);
  if (!parsed.file()) throw std::runtime_error("invalid synthetic fixture");
  return SmfTimelineCompiler::compile(*parsed.file());
}
MediaClockReadResult read(std::uint64_t gen, std::uint64_t time,
                         MediaClockPhase phase = MediaClockPhase::Playing) {
  MediaClockSnapshot s;
  s.generation=gen; s.mediaMicroseconds=time; s.source=MediaClockSource::CompiledTimeline;
  s.phase=phase; s.revision=2;
  return {MediaClockReadStatus::Snapshot,s};
}
std::vector<std::size_t> indices(const LyricCueBatch& batch) {
  std::vector<std::size_t> result;
  for (const auto& cue : batch.cues()) result.push_back(cue.position.sourceIndex);
  return result;
}
}
static_assert(noexcept(std::declval<LyricClockObserver>().poll({})));
static_assert(noexcept(std::declval<LyricClockObserver>().stop()));
int main() {
  const auto compiled=compile();
  OPENHDK_FAIL_IF(1,!compiled.timeline());
  const auto extracted=KarLyricExtractor::extract(*compiled.timeline());
  OPENHDK_FAIL_IF(2,!extracted.succeeded());
  const auto timeline=extracted.timeline();
  LyricClockObserver observer;
  OPENHDK_FAIL_IF(3,observer.bind(timeline,{0}) || observer.bind(nullptr,{1})
      || observer.expectedGeneration() || observer.poll(read(1,0)).status!=LyricClockPollStatus::Unbound);
  OPENHDK_FAIL_IF(4,!observer.bind(timeline,{1}));
  OPENHDK_FAIL_IF(5,observer.poll(read(1,333333,MediaClockPhase::Preparing)).status!=LyricClockPollStatus::Held
      || observer.mediaPositionMicroseconds() || !observer.observed().cues().empty());
  auto first=observer.poll(read(1,0));
  OPENHDK_FAIL_IF(6,!first.batch || indices(*first.batch)!=std::vector<std::size_t>{0});
  OPENHDK_FAIL_IF(7,observer.bind(nullptr,{2}) || observer.bind(timeline,{0})
      || observer.bind(timeline,{1}) || observer.expectedGeneration()!=1
      || observer.observed().cues().size()!=1);
  OPENHDK_FAIL_IF(8,observer.poll({MediaClockReadStatus::Unstable,read(2,333333).snapshot}).status
      !=LyricClockPollStatus::Held || observer.mediaPositionMicroseconds()!=0);
  auto boundary=observer.poll(read(1,166666,MediaClockPhase::Paused));
  OPENHDK_FAIL_IF(9,!boundary.batch || indices(*boundary.batch)!=std::vector<std::size_t>{1,2});
  auto equal=observer.poll(read(1,166666));
  OPENHDK_FAIL_IF(10,!equal.batch || !equal.batch->cues().empty());
  auto final=observer.poll(read(1,333333,MediaClockPhase::Finished));
  OPENHDK_FAIL_IF(11,!final.batch || indices(*final.batch)!=std::vector<std::size_t>{3}
      || observer.observed().cues().size()!=4);
  OPENHDK_FAIL_IF(12,!observer.poll(read(1,333333,MediaClockPhase::Finished)).batch->cues().empty());
  const auto retained=observer.observed();
  OPENHDK_FAIL_IF(13,observer.poll(read(2,0)).status!=LyricClockPollStatus::Cleared
      || observer.expectedGeneration() || !observer.observed().cues().empty()
      || retained.cues().size()!=4 || observer.bind(timeline,{1}));
  // A snapshot cannot establish a binding, including after mismatch cleared it.
  OPENHDK_FAIL_IF(14,observer.poll(read(2,333333)).status!=LyricClockPollStatus::Unbound
      || !observer.observed().cues().empty());
  std::uint64_t generation=2;
  for (auto phase : {MediaClockPhase::Failed,MediaClockPhase::Stopped,MediaClockPhase::Unavailable}) {
    OPENHDK_FAIL_IF(15,!observer.bind(timeline,{generation}));
    const auto result=observer.poll(read(generation++,333333,phase));
    OPENHDK_FAIL_IF(16,result.status!=LyricClockPollStatus::Cleared || result.batch
        || observer.expectedGeneration() || !observer.observed().cues().empty());
  }
  OPENHDK_FAIL_IF(17,!observer.bind(timeline,{generation}));
  auto legacy=read(generation++,333333);legacy.snapshot->source=MediaClockSource::LegacyPlayer;
  OPENHDK_FAIL_IF(18,observer.poll(legacy).status!=LyricClockPollStatus::Cleared);
  OPENHDK_FAIL_IF(19,!observer.bind(timeline,{generation++}));
  OPENHDK_FAIL_IF(20,observer.poll({MediaClockReadStatus::Exhausted,std::nullopt}).status
      !=LyricClockPollStatus::Exhausted || observer.expectedGeneration());
  // Fail closed on malformed/stale time, while old batches remain historical.
  OPENHDK_FAIL_IF(21,!observer.bind(timeline,{generation}));
  auto progressed=observer.poll(read(generation,166666));
  OPENHDK_FAIL_IF(22,!progressed.batch || progressed.batch->cues().size()!=3);
  OPENHDK_FAIL_IF(23,observer.poll(read(generation++,0)).status!=LyricClockPollStatus::BackwardPosition
      || observer.expectedGeneration() || !observer.observed().cues().empty());
  for (int malformed=0;malformed<4;++malformed) {
    OPENHDK_FAIL_IF(24,!observer.bind(timeline,{generation}));
    auto value=read(generation++,0);
    if(malformed==0)value.snapshot.reset();
    if(malformed==1)value.snapshot->revision=3;
    if(malformed==2)value.snapshot->phase=static_cast<MediaClockPhase>(999);
    if(malformed==3)value.snapshot->failure=MediaClockFailure::RenderFailed;
    OPENHDK_FAIL_IF(25,observer.poll(value).status!=LyricClockPollStatus::InvalidSnapshot
        || observer.expectedGeneration());
  }
  // Partition invariant order, using actual publication cell reads.
  MediaClockPublicationCell cell;
  OPENHDK_FAIL_IF(26,!observer.bind(timeline,{generation}));
  std::vector<std::size_t> split;
  for(auto t : {0U,0U,166665U,166666U,333333U}) {
    auto value=read(generation,t);
    OPENHDK_FAIL_IF(27,cell.publish(*value.snapshot)!=MediaClockPublishStatus::Published);
    auto due=observer.poll(cell.tryRead());
    OPENHDK_FAIL_IF(28,!due.batch);
    const auto ids=indices(*due.batch);split.insert(split.end(),ids.begin(),ids.end());
  }
  observer.stop();
  OPENHDK_FAIL_IF(29,observer.expectedGeneration() || observer.mediaPositionMicroseconds()
      || !observer.observed().cues().empty() || progressed.batch->cues().size()!=3);
  OPENHDK_FAIL_IF(30,!observer.bind(timeline,{++generation}));
  auto whole=observer.poll(read(generation,333333,MediaClockPhase::Finished));
  OPENHDK_FAIL_IF(31,!whole.batch || split!=indices(*whole.batch)
      || split!=std::vector<std::size_t>{0,1,2,3});
  // Real session clock -> cell -> observer; consumer never controls the session.
  PlaybackSession session;
  observer.stop();
  OPENHDK_FAIL_IF(32,!observer.bind(timeline,{++generation})
      || !session.prepare(*compiled.timeline(),6).succeeded() || !session.play().succeeded());
  auto block=session.render(1);
  OPENHDK_FAIL_IF(33,!block.succeeded() || cell.publish(*read(generation,block.blockEndMicroseconds()).snapshot)
      !=MediaClockPublishStatus::Published);
  OPENHDK_FAIL_IF(34,observer.poll(cell.tryRead()).batch->cues().size()!=3
      || session.state()!=PlaybackSessionState::Playing);
  observer.stop(); // Must happen before the controller asks playback to stop.
  OPENHDK_FAIL_IF(35,!session.stop().succeeded() || observer.expectedGeneration());
  const auto emptyCompiled=compile(false);
  const auto empty=KarLyricExtractor::extract(*emptyCompiled.timeline());
  OPENHDK_FAIL_IF(36,!empty.succeeded() || !observer.bind(empty.timeline(),{++generation}));
  const auto noLyrics=observer.poll(read(generation,123));
  OPENHDK_FAIL_IF(37,noLyrics.status!=LyricClockPollStatus::Advanced || !noLyrics.batch
      || !noLyrics.batch->cues().empty() || observer.mediaPositionMicroseconds()!=123);
  // A due batch survives destruction of the bound observer and its producers.
  const auto owned=[] {
    const auto localCompiled=compile();
    const auto localLyrics=KarLyricExtractor::extract(*localCompiled.timeline());
    LyricClockObserver local;
    if(!local.bind(localLyrics.timeline(),{1})) throw std::runtime_error("bind failed");
    return local.poll(read(1,333333,MediaClockPhase::Finished));
  }();
  OPENHDK_FAIL_IF(38,!owned.batch || indices(*owned.batch)!=std::vector<std::size_t>{0,1,2,3});
  OPENHDK_FAIL_IF(39,timeline->cues().size()!=4 || timeline->cues()[1].decoded!="B"
      || timeline->cues()[1].raw!=std::vector<std::uint8_t>{'B'}
      || timeline->cues()[1].position.timeMicroseconds!=166666);
  return 0;
}
