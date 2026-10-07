// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "lyrics/LyricMediaConsumer.hpp"
#include "audio/PlaybackSession.hpp"
#include "audio/SmfParser.hpp"
#include "tests/TestCheck.hpp"
#include <array>
#include <limits>
#include <stdexcept>
#include <type_traits>

using namespace OpenHDK;
namespace {
using Bytes = std::vector<std::uint8_t>;
void meta(Bytes& track, std::uint8_t delta, std::uint8_t type, std::string_view payload) {
  track.insert(track.end(), {delta,0xffU,type,static_cast<std::uint8_t>(payload.size())});
  track.insert(track.end(),payload.begin(),payload.end());
}
SmfTimelineCompileResult compile(Bytes track, std::uint16_t ppqn = 3) {
  meta(track,0,0x2f,"");
  Bytes bytes{'M','T','h','d',0,0,0,6,0,0,0,1,
      static_cast<std::uint8_t>(ppqn>>8),static_cast<std::uint8_t>(ppqn), 'M','T','r','k'};
  for (auto shift:{24U,16U,8U,0U}) bytes.push_back(static_cast<std::uint8_t>(track.size()>>shift));
  bytes.insert(bytes.end(),track.begin(),track.end());
  const auto parsed=SmfParser::parse(bytes);
  if (!parsed.file()) throw std::runtime_error("invalid synthetic SMF");
  return SmfTimelineCompiler::compile(*parsed.file());
}
std::vector<std::string> operations(const LyricCueBatch& batch) {
  std::vector<std::string> result;
  for (const auto& cue:batch.cues()) {
    for (const auto& op:cue.operations) {
      if (op.kind==LyricDisplayKind::Text) result.push_back("T:"+cue.decoded.substr(op.byteOffset,op.byteLength));
      else result.push_back(op.kind==LyricDisplayKind::LineBreak ? "L" : "P");
    }
  }
  return result;
}
}
static_assert(noexcept(std::declval<LyricMediaConsumer>().advance(0U)));
static_assert(noexcept(std::declval<LyricMediaConsumer>().reset()));
static_assert(noexcept(std::declval<LyricMediaConsumer>().stop()));
static_assert(std::is_same_v<decltype(std::declval<LyricCueBatch>().cues()),std::span<const KarLyricCue>>);

int main() {
  Bytes track; meta(track,0,5,"zero"); meta(track,1,5,"A\r\nB");
  meta(track,0,5,"/literal"); meta(track,1,5,"\r\n"); meta(track,1,5,"last");
  auto compiled=compile(track);
  OPENHDK_FAIL_IF(1, !compiled.timeline());
  auto extracted=KarLyricExtractor::extract(*compiled.timeline());
  OPENHDK_FAIL_IF(2, !extracted.succeeded());
  auto timeline=extracted.timeline();
  LyricMediaConsumer consumer;
  auto unprepared=consumer.advance(123);
  OPENHDK_FAIL_IF(3, unprepared.succeeded() || unprepared.error()->code!=LyricConsumerErrorCode::Unprepared
      || unprepared.error()->requestedMicroseconds!=123 || unprepared.error()->previousMicroseconds
      || !unprepared.batch().cues().empty());
  OPENHDK_FAIL_IF(4, consumer.prepare(nullptr) || consumer.prepared());
  OPENHDK_FAIL_IF(5, !consumer.prepare(timeline) || consumer.mediaPositionMicroseconds()
      || !consumer.observed().cues().empty());
  auto zero=consumer.advance(0);
  OPENHDK_FAIL_IF(6, !zero.succeeded() || operations(zero.batch())!=std::vector<std::string>{"T:zero"}
      || consumer.mediaPositionMicroseconds()!=0 || consumer.observed().cues().size()!=1);
  OPENHDK_FAIL_IF(7, !consumer.advance(0).batch().cues().empty());
  OPENHDK_FAIL_IF(8, !consumer.advance(166665).batch().cues().empty());
  auto boundary=consumer.advance(166666);
  OPENHDK_FAIL_IF(9, !boundary.succeeded() || boundary.batch().cues().size()!=2
      || operations(boundary.batch())!=std::vector<std::string>{"T:A","L","T:B","T:/literal"});
  OPENHDK_FAIL_IF(10, boundary.batch().cues()[0].position!=LyricSourcePosition{1,166666,0,1}
      || boundary.batch().cues()[1].position!=LyricSourcePosition{1,166666,0,2});
  const auto oldState=consumer.observed();
  auto backward=consumer.advance(166665);
  OPENHDK_FAIL_IF(11, backward.succeeded() || backward.error()->code!=LyricConsumerErrorCode::BackwardPosition
      || backward.error()->previousMicroseconds!=166666 || backward.error()->requestedMicroseconds!=166665
      || !backward.batch().cues().empty() || consumer.mediaPositionMicroseconds()!=166666
      || consumer.observed().cues().size()!=3 || operations(consumer.observed())!=operations(oldState));
  OPENHDK_FAIL_IF(12, consumer.prepare(nullptr) || consumer.mediaPositionMicroseconds()!=166666
      || consumer.observed().cues().size()!=3);
  auto actionOnly=consumer.advance(333333);
  OPENHDK_FAIL_IF(13, actionOnly.batch().cues().size()!=1
      || operations(actionOnly.batch())!=std::vector<std::string>{"L"});
  auto final=consumer.advance(500000);
  OPENHDK_FAIL_IF(14, final.batch().cues().size()!=1 || operations(final.batch())!=std::vector<std::string>{"T:last"});
  const auto finalState=consumer.observed();
  OPENHDK_FAIL_IF(15, finalState.cues().size()!=5 || !consumer.advance(500000).batch().cues().empty()
      || !consumer.advance(std::numeric_limits<std::uint64_t>::max()).batch().cues().empty()
      || operations(consumer.observed())!=operations(finalState));
  consumer.reset();
  OPENHDK_FAIL_IF(16, !consumer.prepared() || consumer.mediaPositionMicroseconds()
      || !consumer.observed().cues().empty() || !consumer.advance(0).succeeded());
  consumer.stop();
  OPENHDK_FAIL_IF(17, consumer.prepared() || consumer.mediaPositionMicroseconds()
      || !consumer.observed().cues().empty() || consumer.advance(0).succeeded());
  OPENHDK_FAIL_IF(18, finalState.cues().size()!=5 || boundary.batch().cues().size()!=2);
  // Poll partition invariance: flatten the same cue source indices, not just text.
  OPENHDK_FAIL_IF(19, !consumer.prepare(timeline));
  std::vector<std::size_t> split;
  for (auto position:std::array<std::uint64_t,9>{0,0,1,166665,166666,166666,333333,499999,500000}) {
    auto result=consumer.advance(position);
    OPENHDK_FAIL_IF(20, !result.succeeded());
    for (const auto& cue:result.batch().cues()) split.push_back(cue.position.sourceIndex);
  }
  OPENHDK_FAIL_IF(21, !consumer.prepare(timeline));
  auto all=consumer.advance(500000);
  std::vector<std::size_t> whole;
  for (const auto& cue:all.batch().cues()) whole.push_back(cue.position.sourceIndex);
  OPENHDK_FAIL_IF(22, split!=whole || whole!=std::vector<std::size_t>{0,1,2,3,4});
  // Use the real frame-derived PlaybackSession clock, including pause/completion.
  PlaybackSession session;
  OPENHDK_FAIL_IF(23, !session.prepare(*compiled.timeline(),6).succeeded()
      || !session.play().succeeded() || !consumer.prepare(timeline));
  auto block=session.render(1);
  OPENHDK_FAIL_IF(24, !block.succeeded() || session.mediaTimeMicroseconds()!=166666);
  auto due=consumer.advance(session.mediaTimeMicroseconds());
  OPENHDK_FAIL_IF(25, due.batch().cues().size()!=3 || session.state()!=PlaybackSessionState::Playing);
  OPENHDK_FAIL_IF(26, !session.pause().succeeded());
  auto paused=session.render(1000);
  OPENHDK_FAIL_IF(27, !paused.succeeded() || session.mediaTimeMicroseconds()!=166666
      || !consumer.advance(session.mediaTimeMicroseconds()).batch().cues().empty()
      || session.state()!=PlaybackSessionState::Paused);
  OPENHDK_FAIL_IF(28, !session.play().succeeded() || !session.render(2).succeeded()
      || session.mediaTimeMicroseconds()!=500000);
  auto completed=consumer.advance(session.mediaTimeMicroseconds());
  OPENHDK_FAIL_IF(29, completed.batch().cues().size()!=2 || !session.endOfTimelineReached()
      || session.state()!=PlaybackSessionState::Playing);
  OPENHDK_FAIL_IF(30, !session.completeReleaseTail().succeeded());
  const auto completeState=consumer.observed();
  OPENHDK_FAIL_IF(31, session.state()!=PlaybackSessionState::Finished
      || !consumer.advance(session.mediaTimeMicroseconds()).batch().cues().empty()
      || consumer.observed().cues().size()!=5);
  OPENHDK_FAIL_IF(32, !session.stop().succeeded());
  consumer.stop();
  OPENHDK_FAIL_IF(33, !consumer.observed().cues().empty() || session.state()!=PlaybackSessionState::Idle);
  OPENHDK_FAIL_IF(34, !session.prepare(*compiled.timeline(),6).succeeded()
      || !session.play().succeeded() || !consumer.prepare(timeline));
  OPENHDK_FAIL_IF(35, consumer.advance(0).batch().cues().size()!=1 || completeState.cues().size()!=5);
  // Distinct ticks can have equal microseconds: emit both once, in compiler order.
  Bytes tiny; meta(tiny,0,0x51,std::string("\0\0\1",3)); meta(tiny,1,5,"one"); meta(tiny,1,5,"two");
  auto tinyCompiled=compile(tiny,32767);
  OPENHDK_FAIL_IF(36, !tinyCompiled.timeline());
  auto tinyExtracted=KarLyricExtractor::extract(*tinyCompiled.timeline());
  OPENHDK_FAIL_IF(37, !tinyExtracted.succeeded() || !consumer.prepare(tinyExtracted.timeline()));
  auto collapsed=consumer.advance(0);
  OPENHDK_FAIL_IF(38, collapsed.batch().cues().size()!=2 || collapsed.batch().cues()[0].position.tick!=1
      || collapsed.batch().cues()[1].position.tick!=2 || !consumer.advance(0).batch().cues().empty());
  // NoLyrics is prepared successfully and has empty current/due state.
  auto emptyCompiled=compile({});
  auto empty=KarLyricExtractor::extract(*emptyCompiled.timeline());
  OPENHDK_FAIL_IF(39, !empty.succeeded() || !consumer.prepare(empty.timeline())
      || !consumer.advance(0).succeeded() || !consumer.observed().cues().empty()
      || !consumer.advance(123).batch().cues().empty());
  auto emptyBackward=consumer.advance(0);
  OPENHDK_FAIL_IF(40, emptyBackward.succeeded() || emptyBackward.error()->code!=LyricConsumerErrorCode::BackwardPosition);
  // A returned batch owns its source independently of every producer/consumer.
  const auto owned=[] {
    auto localCompiled=compile(Bytes{0,0xff,5,1,'X'});
    auto localExtracted=KarLyricExtractor::extract(*localCompiled.timeline());
    LyricMediaConsumer local;
    if (!local.prepare(localExtracted.timeline())) throw std::runtime_error("prepare failed");
    return local.advance(0);
  }();
  OPENHDK_FAIL_IF(41, !owned.succeeded() || operations(owned.batch())!=std::vector<std::string>{"T:X"});
  // Consumer never mutates source cues, metadata, bytes or timing.
  OPENHDK_FAIL_IF(42, timeline->cues().size()!=5 || timeline->cues()[1].decoded!="A\r\nB"
      || timeline->cues()[1].raw!=Bytes{'A','\r','\n','B'}
      || timeline->cues()[1].position!=LyricSourcePosition{1,166666,0,1});
  Bytes words; meta(words,0,3,"Words"); meta(words,0,1,"@TTitle");
  meta(words,0,1,"/\\Hello\r\nWorld"); meta(words,0,1,"/\\");
  auto wordsCompiled=compile(words);
  auto wordsExtracted=KarLyricExtractor::extract(*wordsCompiled.timeline());
  OPENHDK_FAIL_IF(43, !wordsExtracted.succeeded() || !consumer.prepare(wordsExtracted.timeline()));
  auto wordsDue=consumer.advance(0);
  OPENHDK_FAIL_IF(44, wordsDue.batch().cues().size()!=2
      || operations(wordsDue.batch())!=std::vector<std::string>{"L","P","T:Hello","L","T:World","L","P"}
      || consumer.observed().cues().size()!=2 || !consumer.advance(0).batch().cues().empty());
  // reset permits backward restart even after all cues were emitted.
  consumer.reset();
  OPENHDK_FAIL_IF(45, consumer.mediaPositionMicroseconds() || !consumer.observed().cues().empty()
      || consumer.advance(0).batch().cues().size()!=2);
  return 0;
}
