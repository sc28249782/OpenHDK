// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "lyrics/KarLyricExtractor.hpp"
#include "audio/SmfParser.hpp"
#include "tests/TestCheck.hpp"
#include <array>
#include <stdexcept>
#include <type_traits>

using namespace OpenHDK;
namespace {
using Bytes = std::vector<std::uint8_t>;
void vlq(Bytes& bytes, std::size_t value) {
  std::array<std::uint8_t, 4> packed{};
  std::size_t n = 0;
  do { packed[n++] = static_cast<std::uint8_t>(value & 0x7fU); value >>= 7U; } while (value);
  while (n) { --n; bytes.push_back(static_cast<std::uint8_t>(packed[n] | (n ? 0x80U : 0U))); }
}
void meta(Bytes& track, std::uint8_t type, std::string_view payload, std::size_t delta = 0) {
  vlq(track, delta); track.insert(track.end(), {0xffU, type}); vlq(track, payload.size());
  track.insert(track.end(), payload.begin(), payload.end());
}
SmfTimelineCompileResult compile(std::vector<Bytes> tracks, std::uint16_t ppqn = 3) {
  Bytes bytes{'M','T','h','d',0,0,0,6,0,static_cast<std::uint8_t>(tracks.size()>1),
              static_cast<std::uint8_t>(tracks.size()>>8),static_cast<std::uint8_t>(tracks.size()),
              static_cast<std::uint8_t>(ppqn>>8),static_cast<std::uint8_t>(ppqn)};
  for (auto& t : tracks) {
    meta(t, 0x2f, ""); bytes.insert(bytes.end(), {'M','T','r','k'});
    for (auto shift : {24U,16U,8U,0U}) bytes.push_back(static_cast<std::uint8_t>(t.size()>>shift));
    bytes.insert(bytes.end(), t.begin(), t.end());
  }
  auto parsed = SmfParser::parse(bytes);
  if (!parsed.file()) throw std::runtime_error("invalid synthetic SMF");
  return SmfTimelineCompiler::compile(*parsed.file());
}
KarLyricExtractResult extract(std::vector<Bytes> tracks, KarLyricOptions options = {}) {
  auto compiled = compile(std::move(tracks));
  if (!compiled.timeline()) throw std::runtime_error("invalid synthetic timeline");
  return KarLyricExtractor::extract(*compiled.timeline(), options);
}
Bytes track(std::uint8_t type, std::string_view text, std::string_view name = "") {
  Bytes t; if (!name.empty()) meta(t,3,name); meta(t,type,text); return t;
}
bool fails(const KarLyricExtractResult& r, KarLyricErrorCode code) {
  return !r.succeeded() && !r.timeline() && r.error() && r.error()->code == code;
}
std::vector<std::string> display(const KarLyricCue& cue) {
  std::vector<std::string> out;
  for (const auto& op : cue.operations) {
    if (op.kind == LyricDisplayKind::Text) out.push_back("T:" + cue.decoded.substr(op.byteOffset,op.byteLength));
    else out.push_back(op.kind == LyricDisplayKind::LineBreak ? "L" : "P");
  }
  return out;
}
}
static_assert(std::is_same_v<decltype(std::declval<KarLyricTimeline>().cues()),std::span<const KarLyricCue>>);
static_assert(std::is_same_v<decltype(std::declval<KarLyricExtractResult>().timeline()),std::shared_ptr<const KarLyricTimeline>>);

int main() {
  auto selected = extract({track(1,"/Text"," Words\t"),track(5,"Lyric")});
  OPENHDK_FAIL_IF(1, !selected.succeeded() || selected.timeline()->profile()!=KarLyricProfile::LyricMeta
      || selected.timeline()->trackIndex()!=1U || selected.timeline()->cues().size()!=1U
      || selected.timeline()->cues()[0].decoded!="Lyric");
  OPENHDK_FAIL_IF(2, !fails(extract({track(5,"A"),track(5,"B")}),KarLyricErrorCode::AmbiguousLyricTrack));
  KarLyricOptions options; options.trackIndex=1;
  auto explicitTrack=extract({track(5,"A"),track(5,"B")},options);
  OPENHDK_FAIL_IF(3, !explicitTrack.succeeded() || explicitTrack.timeline()->cues()[0].decoded!="B");
  options.trackIndex=2;
  OPENHDK_FAIL_IF(4, !fails(extract({track(5,"A"),Bytes{}},options),KarLyricErrorCode::InvalidLyricTrack));
  options.trackIndex=1;
  auto emptyTrack=extract({track(5,"A"),Bytes{}},options);
  OPENHDK_FAIL_IF(5, !emptyTrack.succeeded() || emptyTrack.timeline()->profile()!=KarLyricProfile::NoLyrics);
  options={};
  auto none=extract({track(1,"unnamed"),track(1,"@TOnly metadata","Words"),track(5,"")});
  OPENHDK_FAIL_IF(6, !none.succeeded() || none.timeline()->profile()!=KarLyricProfile::NoLyrics
      || !none.timeline()->cues().empty() || none.timeline()->trackIndex());
  auto fallback=extract({track(1,"ignore"),track(1,"/Hello","\t lYrIcS ")});
  OPENHDK_FAIL_IF(7, !fallback.succeeded() || fallback.timeline()->profile()!=KarLyricProfile::NamedText
      || display(fallback.timeline()->cues()[0])!=std::vector<std::string>{"L","T:Hello"});
  OPENHDK_FAIL_IF(8, !fails(extract({track(1,"A","Words"),track(1,"B","Lyrics")}),KarLyricErrorCode::AmbiguousLyricTrack));
  Bytes named; meta(named,3,"Other"); meta(named,1,"before name"); meta(named,3,"WORDS");
  OPENHDK_FAIL_IF(9, extract({named}).timeline()->cues().size()!=1U);
  OPENHDK_FAIL_IF(10, extract({track(1,"A","Words\n")}).timeline()->profile()!=KarLyricProfile::NoLyrics);
  Bytes same=track(1,"ignored","Words"); meta(same,5,"chosen");
  OPENHDK_FAIL_IF(11, extract({same}).timeline()->cues()[0].decoded!="chosen");
  // Each independently specified display vector from contract section 3.
  struct Vector { std::uint8_t type; std::string payload; std::vector<std::string> expected; };
  const std::vector<Vector> vectors{
    {5,"A\r\nB",{"T:A","L","T:B"}}, {5,"\r\n",{"L"}},
    {5,"/A\\B@",{"T:/A\\B@"}}, {1,"/\\/Hello",{"L","P","L","T:Hello"}},
    {1,"/A\r\nB",{"L","T:A","L","T:B"}}, {1,"A/B\\C",{"T:A/B\\C"}},
    {1,"/\\",{"L","P"}}, {5,"A\n\rB",{"T:A","L","L","T:B"}},
    {1," /A",{"T: /A"}}, {5,"\t",{"T:\t"}}
  };
  for (const auto& v : vectors) {
    auto r=extract({track(v.type,v.payload,"Words")});
    OPENHDK_FAIL_IF(12, !r.succeeded() || r.timeline()->cues().size()!=1U);
    const auto& cue=r.timeline()->cues()[0];
    OPENHDK_FAIL_IF(13, display(cue)!=v.expected || cue.decoded!=v.payload
        || cue.raw!=Bytes(v.payload.begin(),v.payload.end()));
    for (const auto& op : cue.operations) {
      OPENHDK_FAIL_IF(14, op.byteOffset>cue.decoded.size() || op.byteLength>cue.decoded.size()-op.byteOffset
          || (op.kind==LyricDisplayKind::Text && op.byteLength==0));
    }
  }
  Bytes split; meta(split,5,"A\r"); meta(split,5,"\nB"); meta(split,5,"");
  auto splitResult=extract({split});
  OPENHDK_FAIL_IF(15, splitResult.timeline()->cues().size()!=2U
      || display(splitResult.timeline()->cues()[0])!=std::vector<std::string>{"T:A","L"}
      || display(splitResult.timeline()->cues()[1])!=std::vector<std::string>{"L","T:B"});
  Bytes tagged; meta(tagged,3,"Words"); meta(tagged,1,"@T"); meta(tagged,1,"@T Title/Artist ");
  meta(tagged,1,"@TAdditional"); meta(tagged,1,"@LThai"); meta(tagged,1,"/Hello");
  auto tags=extract({tagged});
  OPENHDK_FAIL_IF(16, !tags.succeeded() || tags.timeline()->title()!=std::optional<std::string>{" Title/Artist "}
      || tags.timeline()->metadata().size()!=4U || tags.timeline()->cues().size()!=1U
      || tags.timeline()->metadata()[2].decoded!="@TAdditional");
  auto literal=extract({track(5,"@TNot metadata")});
  OPENHDK_FAIL_IF(17, !literal.timeline()->metadata().empty() || literal.timeline()->title());
  // Decode failure cannot fall back to the otherwise valid named track.
  auto bad=extract({track(5,std::string("A\xc2",2)),track(1,"Good","Words")});
  OPENHDK_FAIL_IF(18, !fails(bad,KarLyricErrorCode::InvalidText)
      || !bad.error()->position || bad.error()->position->trackIndex!=0U
      || bad.error()->position->sourceIndex!=0U || bad.error()->payloadByteOffset!=2U);
  auto badTag=extract({track(1,"@T\xc2","Words")}); // metadata alone is NoLyrics
  OPENHDK_FAIL_IF(19, !badTag.succeeded() || badTag.timeline()->profile()!=KarLyricProfile::NoLyrics);
  Bytes badMetadata=track(1,"@T\xc2","Words"); meta(badMetadata,1,"text");
  OPENHDK_FAIL_IF(20, !fails(extract({badMetadata}),KarLyricErrorCode::InvalidText));
  options.encoding=LyricTextEncoding::Tis620;
  auto thai=extract({track(5,"\xa1\xe8")},options);
  OPENHDK_FAIL_IF(21, !thai.succeeded() || thai.timeline()->cues()[0].decoded!="\xe0\xb8\x81\xe0\xb9\x88"
      || thai.timeline()->cues()[0].raw!=Bytes{0xa1,0xe8});
  auto undefined=extract({track(5,"A\xa0")},options);
  OPENHDK_FAIL_IF(22, !fails(undefined,KarLyricErrorCode::InvalidText) || undefined.error()->payloadByteOffset!=1U);
  options={};
  // Known independent PPQN=3/tempo expectations, plus same-time source order.
  Bytes music{0,0xb0,7,100,0,0x90,60,64};
  meta(music,0x51,std::string("\x03\xd0\x90",3),2); // 250000 us/quarter after tick 2
  Bytes timed; meta(timed,5,"first",1); meta(timed,5,"second",1);
  meta(timed,5,"equal"); meta(timed,5,"third",1);
  auto compiled=compile({music,timed});
  OPENHDK_FAIL_IF(23, !compiled.timeline());
  const auto before=compiled.timeline()->events();
  std::vector<Bytes> retained; std::vector<LyricSourcePosition> positions;
  for (const auto& e:before) {
    retained.emplace_back(e.event().data().begin(),e.event().data().end());
    positions.push_back({e.tick(),e.timeMicroseconds(),e.trackIndex(),e.sourceIndex()});
  }
  auto timedResult=KarLyricExtractor::extract(*compiled.timeline());
  OPENHDK_FAIL_IF(24, !timedResult.succeeded() || timedResult.timeline()->cues().size()!=4U);
  auto cues=timedResult.timeline()->cues();
  OPENHDK_FAIL_IF(25, cues[0].position!=LyricSourcePosition{1,166666,1,0}
      || cues[1].position!=LyricSourcePosition{2,333333,1,1}
      || cues[2].position!=LyricSourcePosition{2,333333,1,2}
      || cues[3].position!=LyricSourcePosition{3,416666,1,3});
  for (std::size_t i=0;i<before.size();++i) {
    const auto& e=before[i];
    OPENHDK_FAIL_IF(26, positions[i]!=LyricSourcePosition{e.tick(),e.timeMicroseconds(),e.trackIndex(),e.sourceIndex()}
        || retained[i]!=Bytes(e.event().data().begin(),e.event().data().end()));
  }
  // Published ownership outlives both the compiler and extraction result.
  std::shared_ptr<const KarLyricTimeline> held;
  { auto local=extract({track(5,"owned")}); held=local.timeline(); }
  OPENHDK_FAIL_IF(27, held->cues()[0].decoded!="owned" || display(held->cues()[0])!=std::vector<std::string>{"T:owned"});
  for (auto limits:std::array<KarLyricLimits,8>{{
      {0,1,1,1},{1,0,1,1},{1,1,0,1},{1,1,1,0},
      {4194305,1,1,1},{1,100001,1,1},{1,1,4097,1},{1,1,1,67108865}}}) {
    options.limits=limits;
    OPENHDK_FAIL_IF(28, !fails(extract({track(5,"A")},options),KarLyricErrorCode::InvalidConfiguration));
  }
  options={}; options.encoding=static_cast<LyricTextEncoding>(99);
  OPENHDK_FAIL_IF(29, !fails(extract({Bytes{}},options),KarLyricErrorCode::InvalidConfiguration));
  options={}; options.limits.sourceBytes=2;
  OPENHDK_FAIL_IF(30, !extract({track(5,"AB")},options).succeeded());
  OPENHDK_FAIL_IF(31, !fails(extract({track(5,"ABC")},options),KarLyricErrorCode::LimitExceeded));
  Bytes cumulative; meta(cumulative,5,"A"); meta(cumulative,5,"B"); meta(cumulative,5,"C");
  OPENHDK_FAIL_IF(32, !fails(extract({cumulative},options),KarLyricErrorCode::LimitExceeded));
  options={}; options.limits.cues=1;
  OPENHDK_FAIL_IF(33, !extract({track(5,"A")},options).succeeded()
      || !fails(extract({split},options),KarLyricErrorCode::LimitExceeded));
  options={}; options.limits.titleBytes=3;
  Bytes title=track(1,"@TABC","Words"); meta(title,1,"a");
  OPENHDK_FAIL_IF(34, !extract({title},options).succeeded());
  Bytes longTitle=track(1,"@TABCD","Words"); meta(longTitle,1,"a");
  OPENHDK_FAIL_IF(35, !fails(extract({longTitle},options),KarLyricErrorCode::LimitExceeded));
  // Logical staging charges raw + decoded + fixed record + one operation.
  options={}; options.limits.stagedBytes=sizeof(KarLyricCue)+sizeof(LyricDisplayOperation)+2;
  OPENHDK_FAIL_IF(36, !extract({track(5,"A")},options).succeeded());
  --options.limits.stagedBytes;
  OPENHDK_FAIL_IF(37, !fails(extract({track(5,"A")},options),KarLyricErrorCode::LimitExceeded));
  options={}; options.limits.stagedBytes=1;
  OPENHDK_FAIL_IF(38, !fails(extract({track(5,"A")},options),KarLyricErrorCode::LimitExceeded));
  options={}; options.limits.sourceBytes=5;
  OPENHDK_FAIL_IF(39, !fails(extract({title},options),KarLyricErrorCode::LimitExceeded));
  options={}; options.trackIndex=0;
  auto explicitFallback=extract({track(1,"text","Words"),track(5,"lyric")},options);
  OPENHDK_FAIL_IF(40, !explicitFallback.succeeded()
      || explicitFallback.timeline()->profile()!=KarLyricProfile::NamedText);
  Bytes fastTempo; meta(fastTempo,0x51,std::string("\x00\x00\x01",3));
  Bytes collapsed; meta(collapsed,5,"tick1",1); meta(collapsed,5,"tick2",1);
  auto tiny=compile({fastTempo,collapsed},32767);
  OPENHDK_FAIL_IF(41, !tiny.timeline());
  auto tinyLyrics=KarLyricExtractor::extract(*tiny.timeline());
  OPENHDK_FAIL_IF(42, !tinyLyrics.succeeded() || tinyLyrics.timeline()->cues().size()!=2
      || tinyLyrics.timeline()->cues()[0].position!=LyricSourcePosition{1,0,1,0}
      || tinyLyrics.timeline()->cues()[1].position!=LyricSourcePosition{2,0,1,1});
  options={}; options.encoding=LyricTextEncoding::Tis620;
  Bytes thaiTitle=track(1,"@T\xa1\xe8","Words"); meta(thaiTitle,1,"/\xa1\xe8");
  auto titleThai=extract({thaiTitle},options);
  OPENHDK_FAIL_IF(43, !titleThai.succeeded() || titleThai.timeline()->title()!=std::optional<std::string>{"\xe0\xb8\x81\xe0\xb9\x88"}
      || display(titleThai.timeline()->cues()[0])!=std::vector<std::string>{"L","T:\xe0\xb8\x81\xe0\xb9\x88"});
  // Per-result staging includes title's duplicate bytes and metadata records.
  options={}; options.limits.stagedBytes=sizeof(KarLyricMetadata)+sizeof(KarLyricCue)
      +sizeof(LyricDisplayOperation)+10+3+2; // @TABC raw+decoded, title, 'a' raw+decoded
  OPENHDK_FAIL_IF(44, !extract({title},options).succeeded());
  --options.limits.stagedBytes;
  OPENHDK_FAIL_IF(45, !fails(extract({title},options),KarLyricErrorCode::LimitExceeded));
  options={}; options.encoding=LyricTextEncoding::Tis620;
  options.limits.stagedBytes=sizeof(KarLyricCue)+sizeof(LyricDisplayOperation)+1+3;
  OPENHDK_FAIL_IF(46, !extract({track(5,"\xa1")},options).succeeded());
  --options.limits.stagedBytes;
  OPENHDK_FAIL_IF(47, !fails(extract({track(5,"\xa1")},options),KarLyricErrorCode::LimitExceeded));
  auto nul=extract({track(5,std::string("A\0B",3))});
  OPENHDK_FAIL_IF(48, !fails(nul,KarLyricErrorCode::InvalidText) || nul.error()->payloadByteOffset!=1U);
  return 0;
}
