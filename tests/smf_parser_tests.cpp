// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "audio/SmfParser.hpp"
#include "audio/SmfTrackEventDecoder.hpp"
#include "audio/SmfTimelineCompiler.hpp"
#include "audio/PlaybackSession.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace {

using OpenHDK::SmfParseErrorCode;
using OpenHDK::SmfTrackDecodeErrorCode;
using OpenHDK::SmfTimelineErrorCode;

template <std::size_t Size>
bool expectsSuccess(const std::array<std::uint8_t, Size>& fixture, std::uint16_t format,
                    std::uint16_t division, std::size_t tracks) {
    const auto result = OpenHDK::SmfParser::parse(fixture);
    const auto* file = result.file();
    if (!result.succeeded() || !file || result.error()) return false;
    return file->format() == format && file->division() == division
        && file->tracks().size() == tracks;
}

template <std::size_t Size>
bool expectsError(const std::array<std::uint8_t, Size>& fixture, SmfParseErrorCode expected,
                  std::size_t expectedOffset) {
    const auto result = OpenHDK::SmfParser::parse(fixture);
    const auto* error = result.error();
    return !result.succeeded() && !result.file() && error && error->code == expected
        && error->offset == expectedOffset && !error->message().empty();
}

template <std::size_t Size>
bool expectsTrackError(const std::array<std::uint8_t, Size>& fixture,
                       SmfTrackDecodeErrorCode expected, std::size_t expectedOffset) {
    const auto result = OpenHDK::SmfTrackEventDecoder::decode(fixture);
    const auto* error = result.error();
    return !result.succeeded() && !result.events() && error && error->code == expected
        && error->offset == expectedOffset && !error->message().empty();
}

} // namespace

int main() {
    static_assert(!std::is_default_constructible_v<OpenHDK::SmfFile>);
    static_assert(!std::is_default_constructible_v<OpenHDK::SmfParseResult>);
    static_assert(!std::is_default_constructible_v<OpenHDK::SmfTrackEventList>);
    static_assert(!std::is_default_constructible_v<OpenHDK::SmfTrackDecodeResult>);
    static_assert(!std::is_default_constructible_v<OpenHDK::SmfTimeline>);
    static_assert(!std::is_default_constructible_v<OpenHDK::SmfTimelineCompileResult>);

    constexpr std::array<std::uint8_t, 26> format0{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,4, 0,0xff,0x2f,0};
    if (!expectsSuccess(format0, 0U, 96U, 1U)) return 1;
    auto mutableFormat0 = format0;
    const auto ownedResult = OpenHDK::SmfParser::parse(mutableFormat0);
    mutableFormat0[22] = 0x7fU;
    if (!ownedResult.file() || ownedResult.file()->tracks()[0].bytes()[0] != 0U) return 7;
    const auto decodedOwnedTrack = OpenHDK::SmfTrackEventDecoder::decode(ownedResult.file()->tracks()[0]);
    if (!decodedOwnedTrack.succeeded() || !decodedOwnedTrack.events()
        || decodedOwnedTrack.events()->events().size() != 1U) return 23;

    constexpr std::array<std::uint8_t, 34> format1{
        'M','T','h','d', 0,0,0,6, 0,1, 0,2, 1,0xe0,
        'M','T','r','k', 0,0,0,2, 0,0xff,
        'M','T','r','k', 0,0,0,2, 0,0xff};
    if (!expectsSuccess(format1, 1U, 480U, 2U)) return 2;

    constexpr std::array<std::uint8_t, 7> truncatedHeader{'M','T','h','d', 0,0,0};
    if (!expectsError(truncatedHeader, SmfParseErrorCode::TruncatedHeader, 7U)) return 3;

    constexpr std::array<std::uint8_t, 8> invalidHeaderLength{
        'M','T','h','d', 0,0,0,5};
    if (!expectsError(invalidHeaderLength, SmfParseErrorCode::InvalidHeaderLength, 4U)) return 8;

    constexpr std::array<std::uint8_t, 4> invalidMThdId{'M','T','x','d'};
    if (!expectsError(invalidMThdId, SmfParseErrorCode::InvalidHeaderChunk, 2U)) return 19;

    constexpr std::array<std::uint8_t, 14> unsupportedFormat{
        'M','T','h','d', 0,0,0,6, 0,2, 0,1, 0,96};
    if (!expectsError(unsupportedFormat, SmfParseErrorCode::UnsupportedFormat, 8U)) return 4;

    constexpr std::array<std::uint8_t, 14> invalidDivision{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,0};
    if (!expectsError(invalidDivision, SmfParseErrorCode::InvalidDivision, 12U)) return 5;

    constexpr std::array<std::uint8_t, 22> truncatedTrack{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,1};
    if (!expectsError(truncatedTrack, SmfParseErrorCode::TruncatedTrackChunk, 22U)) return 6;

    constexpr std::array<std::uint8_t, 3> partialMThdId{'M','T','h'};
    if (!expectsError(partialMThdId, SmfParseErrorCode::TruncatedHeader, 3U)) return 9;

    constexpr std::array<std::uint8_t, 6> partialMThdLength{'M','T','h','d', 0,0};
    if (!expectsError(partialMThdLength, SmfParseErrorCode::TruncatedHeader, 6U)) return 10;

    constexpr std::array<std::uint8_t, 12> partialMThdFields{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1};
    if (!expectsError(partialMThdFields, SmfParseErrorCode::TruncatedHeader, 12U)) return 11;

    constexpr std::array<std::uint8_t, 9> partialMThdFormat{
        'M','T','h','d', 0,0,0,6, 0};
    if (!expectsError(partialMThdFormat, SmfParseErrorCode::TruncatedHeader, 9U)) return 20;

    constexpr std::array<std::uint8_t, 11> partialMThdTrackCount{
        'M','T','h','d', 0,0,0,6, 0,0, 0};
    if (!expectsError(partialMThdTrackCount, SmfParseErrorCode::TruncatedHeader, 11U)) return 21;

    constexpr std::array<std::uint8_t, 13> partialMThdDivision{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0};
    if (!expectsError(partialMThdDivision, SmfParseErrorCode::TruncatedHeader, 13U)) return 22;

    constexpr std::array<std::uint8_t, 17> partialMTrkId{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96, 'M','T','r'};
    if (!expectsError(partialMTrkId, SmfParseErrorCode::TruncatedTrackChunk, 17U)) return 12;

    constexpr std::array<std::uint8_t, 20> partialMTrkLength{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96, 'M','T','r','k', 0,0};
    if (!expectsError(partialMTrkLength, SmfParseErrorCode::TruncatedTrackChunk, 20U)) return 13;

    constexpr std::array<std::uint8_t, 23> truncatedPayload{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,2, 0};
    if (!expectsError(truncatedPayload, SmfParseErrorCode::TruncatedTrackChunk, 23U)) return 14;

    constexpr std::array<std::uint8_t, 22> invalidMTrkId{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','x','k', 0,0,0,0};
    if (!expectsError(invalidMTrkId, SmfParseErrorCode::InvalidTrackChunk, 16U)) return 15;

    constexpr std::array<std::uint8_t, 14> format0TwoTracks{
        'M','T','h','d', 0,0,0,6, 0,0, 0,2, 0,96};
    if (!expectsError(format0TwoTracks, SmfParseErrorCode::InvalidTrackCount, 10U)) return 16;

    constexpr std::array<std::uint8_t, 14> format1ZeroTracks{
        'M','T','h','d', 0,0,0,6, 0,1, 0,0, 0,96};
    if (!expectsError(format1ZeroTracks, SmfParseErrorCode::InvalidTrackCount, 10U)) return 17;

    constexpr std::array<std::uint8_t, 27> trailingData{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,4, 0,0xff,0x2f,0, 0};
    if (!expectsError(trailingData, SmfParseErrorCode::TrailingData, 26U)) return 18;

    constexpr std::array<std::uint8_t, 42> validEvents{
        0, 0x90, 60, 100,
        0x81,0, 0x80, 60, 0,
        0, 0xb0, 7, 100,
        0, 0xc0, 5,
        0, 0xe0, 0, 64,
        0, 0xff, 0x51, 3, 7, 0xa1, 0x20,
        0, 0xff, 1, 2, 'x', 'y',
        0, 0xf0, 2, 0x7d, 1,
        0, 0xff, 0x2f, 0};
    const auto validEventsResult = OpenHDK::SmfTrackEventDecoder::decode(validEvents);
    if (!validEventsResult.succeeded() || !validEventsResult.events() || validEventsResult.error()) return 24;
    const auto events = validEventsResult.events()->events();
    if (events.size() != 9U || events[0].kind() != OpenHDK::SmfMidiEventKind::NoteOn
        || events[1].kind() != OpenHDK::SmfMidiEventKind::NoteOff || events[1].tick() != 128U
        || events[2].kind() != OpenHDK::SmfMidiEventKind::Controller || events[2].tick() != 128U
        || events[3].kind() != OpenHDK::SmfMidiEventKind::ProgramChange
        || events[4].kind() != OpenHDK::SmfMidiEventKind::PitchBend
        || events[5].kind() != OpenHDK::SmfMidiEventKind::Tempo || events[5].data().size() != 3U
        || events[6].kind() != OpenHDK::SmfMidiEventKind::Meta
        || events[7].kind() != OpenHDK::SmfMidiEventKind::SysEx
        || events[8].kind() != OpenHDK::SmfMidiEventKind::EndOfTrack) return 25;
    auto mutableEvents = validEvents;
    const auto ownedEventsResult = OpenHDK::SmfTrackEventDecoder::decode(mutableEvents);
    mutableEvents[3] = 0U;
    if (!ownedEventsResult.events() || ownedEventsResult.events()->events()[0].data()[1] != 100U) return 33;

    constexpr std::array<std::uint8_t, 11> runningStatus{
        0, 0x90, 60, 64, 5, 61, 65, 0, 0xff, 0x2f, 0};
    const auto runningStatusResult = OpenHDK::SmfTrackEventDecoder::decode(runningStatus);
    if (!runningStatusResult.succeeded() || !runningStatusResult.events()
        || runningStatusResult.events()->events().size() != 3U
        || runningStatusResult.events()->events()[1].tick() != 5U
        || runningStatusResult.events()->events()[1].data()[0] != 61U
        || runningStatusResult.events()->events()[2].kind() != OpenHDK::SmfMidiEventKind::EndOfTrack) return 26;

    constexpr std::array<std::uint8_t, 4> malformedVlq{0x81, 0x80, 0x80, 0x80};
    if (!expectsTrackError(malformedVlq, SmfTrackDecodeErrorCode::MalformedVlq, 3U)) return 27;

    constexpr std::array<std::uint8_t, 2> missingRunningStatus{0, 60};
    if (!expectsTrackError(missingRunningStatus, SmfTrackDecodeErrorCode::MissingRunningStatus, 1U)) return 28;

    constexpr std::array<std::uint8_t, 3> truncatedEventData{0, 0x90, 60};
    if (!expectsTrackError(truncatedEventData, SmfTrackDecodeErrorCode::TruncatedEvent, 3U)) return 29;

    constexpr std::array<std::uint8_t, 6> invalidTempoLength{0, 0xff, 0x51, 2, 0, 0};
    if (!expectsTrackError(invalidTempoLength, SmfTrackDecodeErrorCode::InvalidMetaLength, 3U)) return 30;

    constexpr std::array<std::uint8_t, 5> truncatedMetaPayload{0, 0xff, 1, 2, 'x'};
    if (!expectsTrackError(truncatedMetaPayload, SmfTrackDecodeErrorCode::TruncatedEvent, 5U)) return 31;

    constexpr std::array<std::uint8_t, 4> truncatedSysExPayload{0, 0xf0, 2, 0x7d};
    if (!expectsTrackError(truncatedSysExPayload, SmfTrackDecodeErrorCode::TruncatedEvent, 4U)) return 32;

    constexpr std::array<std::uint8_t, 4> missingEndOfTrack{0, 0x90, 60, 64};
    if (!expectsTrackError(missingEndOfTrack, SmfTrackDecodeErrorCode::MissingEndOfTrack, 4U)) return 34;

    constexpr std::array<std::uint8_t, 8> trailingEventAfterEndOfTrack{
        0, 0xff, 0x2f, 0, 0, 0x90, 60, 64};
    if (!expectsTrackError(trailingEventAfterEndOfTrack,
                           SmfTrackDecodeErrorCode::TrailingDataAfterEndOfTrack, 4U)) return 35;

    constexpr std::array<std::uint8_t, 5> trailingDataAfterEndOfTrack{0, 0xff, 0x2f, 0, 0};
    if (!expectsTrackError(trailingDataAfterEndOfTrack,
                           SmfTrackDecodeErrorCode::TrailingDataAfterEndOfTrack, 4U)) return 36;

    constexpr std::array<std::uint8_t, 53> crossTrackTimeline{
        'M','T','h','d', 0,0,0,6, 0,1, 0,2, 0,96,
        'M','T','r','k', 0,0,0,12,
        0, 0x90, 60, 64, 0x60, 0x80, 60, 0, 0, 0xff, 0x2f, 0,
        'M','T','r','k', 0,0,0,11,
        0, 0xc0, 5, 0x30, 0xb0, 7, 100, 0x30, 0xff, 0x2f, 0};
    const auto crossTrackFile = OpenHDK::SmfParser::parse(crossTrackTimeline);
    if (!crossTrackFile.file()) return 37;
    const auto crossTrackResult = OpenHDK::SmfTimelineCompiler::compile(*crossTrackFile.file());
    if (!crossTrackResult.succeeded() || !crossTrackResult.timeline() || crossTrackResult.error()) return 38;
    const auto crossTrackEvents = crossTrackResult.timeline()->events();
    if (crossTrackEvents.size() != 6U || crossTrackEvents[0].trackIndex() != 0U
        || crossTrackEvents[1].trackIndex() != 1U || crossTrackEvents[0].tick() != 0U
        || crossTrackEvents[1].tick() != 0U || crossTrackEvents[2].tick() != 48U
        || crossTrackEvents[2].timeMicroseconds() != 250000U
        || crossTrackEvents[3].tick() != 96U || crossTrackEvents[3].trackIndex() != 0U
        || crossTrackEvents[3].timeMicroseconds() != 500000U
        || crossTrackEvents[5].event().kind() != OpenHDK::SmfMidiEventKind::EndOfTrack) return 39;

    constexpr std::array<std::uint8_t, 48> tempoTimeline{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,100,
        'M','T','r','k', 0,0,0,26,
        0, 0xff, 0x51, 3, 7, 0xa1, 0x20,
        0x64, 0x90, 60, 64,
        0x64, 0xff, 0x51, 3, 3, 0xd0, 0x90,
        0x64, 0x80, 60, 0,
        0, 0xff, 0x2f, 0};
    const auto tempoFile = OpenHDK::SmfParser::parse(tempoTimeline);
    if (!tempoFile.file()) return 40;
    const auto tempoResult = OpenHDK::SmfTimelineCompiler::compile(*tempoFile.file());
    if (!tempoResult.timeline() || tempoResult.timeline()->events().size() != 5U) return 41;
    const auto tempoEvents = tempoResult.timeline()->events();
    if (tempoEvents[1].timeMicroseconds() != 500000U
        || tempoEvents[2].timeMicroseconds() != 1000000U
        || tempoEvents[3].timeMicroseconds() != 1250000U) return 42;

    constexpr std::array<std::uint8_t, 56> sameTickTempoTimeline{
        'M','T','h','d', 0,0,0,6, 0,1, 0,2, 0,100,
        'M','T','r','k', 0,0,0,11,
        0, 0xff, 0x51, 3, 7, 0xa1, 0x20, 0x64, 0xff, 0x2f, 0,
        'M','T','r','k', 0,0,0,15,
        0, 0xff, 0x51, 3, 3, 0xd0, 0x90,
        0x64, 0x90, 60, 64, 0, 0xff, 0x2f, 0};
    const auto sameTickTempoFile = OpenHDK::SmfParser::parse(sameTickTempoTimeline);
    if (!sameTickTempoFile.file()) return 43;
    const auto sameTickTempoResult = OpenHDK::SmfTimelineCompiler::compile(*sameTickTempoFile.file());
    if (!sameTickTempoResult.timeline() || sameTickTempoResult.timeline()->events().size() != 5U) return 44;
    const auto sameTickTempoEvents = sameTickTempoResult.timeline()->events();
    if (sameTickTempoEvents[0].trackIndex() != 0U || sameTickTempoEvents[1].trackIndex() != 1U
        || sameTickTempoEvents[3].event().kind() != OpenHDK::SmfMidiEventKind::NoteOn
        || sameTickTempoEvents[3].timeMicroseconds() != 250000U) return 45;

    constexpr std::array<std::uint8_t, 26> decoderFailureTimeline{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,4, 0, 0x90, 60, 64};
    const auto decoderFailureFile = OpenHDK::SmfParser::parse(decoderFailureTimeline);
    if (!decoderFailureFile.file()) return 46;
    const auto decoderFailureResult = OpenHDK::SmfTimelineCompiler::compile(*decoderFailureFile.file());
    if (decoderFailureResult.succeeded() || !decoderFailureResult.error()
        || decoderFailureResult.error()->code() != SmfTimelineErrorCode::TrackDecodeFailed
        || decoderFailureResult.error()->trackIndex() != 0U
        || !decoderFailureResult.error()->trackDecodeError()
        || decoderFailureResult.error()->trackDecodeError()->code != SmfTrackDecodeErrorCode::MissingEndOfTrack
        || decoderFailureResult.error()->trackDecodeError()->offset != 4U) return 47;

    constexpr std::array<std::uint8_t, 24> smpteTimeline{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0xe7,40,
        'M','T','r','k', 0,0,0,2, 0, 0x90};
    const auto smpteFile = OpenHDK::SmfParser::parse(smpteTimeline);
    if (!smpteFile.file()) return 48;
    const auto smpteResult = OpenHDK::SmfTimelineCompiler::compile(*smpteFile.file());
    if (smpteResult.succeeded() || !smpteResult.error()
        || smpteResult.error()->code() != SmfTimelineErrorCode::UnsupportedSmpteDivision) return 49;

    constexpr std::array<std::uint8_t, 34> fractionalTimeline{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,3,
        'M','T','r','k', 0,0,0,12,
        1, 0x90, 60, 64, 1, 0x80, 60, 0, 0, 0xff, 0x2f, 0};
    const auto fractionalFile = OpenHDK::SmfParser::parse(fractionalTimeline);
    if (!fractionalFile.file()) return 52;
    const auto fractionalResult = OpenHDK::SmfTimelineCompiler::compile(*fractionalFile.file());
    if (!fractionalResult.timeline() || fractionalResult.timeline()->events().size() != 3U
        || fractionalResult.timeline()->events()[0].timeMicroseconds() != 166666U
        || fractionalResult.timeline()->events()[1].timeMicroseconds() != 333333U) return 53;

    std::vector<std::uint8_t> overflowTimeline{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,1,
        'M','T','r','k', 0,0,0x70,0x12,
        0, 0xff, 0x51, 3, 0xff, 0xff, 0xff};
    for (std::size_t index = 0U; index < 4097U; ++index) {
        overflowTimeline.insert(overflowTimeline.end(), {0xff, 0xff, 0xff, 0x7f, 0x90, 60, 64});
    }
    overflowTimeline.insert(overflowTimeline.end(), {0, 0xff, 0x2f, 0});
    const auto overflowFile = OpenHDK::SmfParser::parse(overflowTimeline);
    if (!overflowFile.file()) return 50;
    const auto overflowResult = OpenHDK::SmfTimelineCompiler::compile(*overflowFile.file());
    if (overflowResult.succeeded() || !overflowResult.error()
        || overflowResult.error()->code() != SmfTimelineErrorCode::TimeOverflow) return 51;

    OpenHDK::PlaybackSession invalidSession;
    const auto invalidPrepare = invalidSession.prepare(*crossTrackResult.timeline(), 0U);
    if (invalidPrepare.succeeded() || !invalidPrepare.error()
        || invalidPrepare.error()->code() != OpenHDK::PlaybackSessionErrorCode::InvalidSampleRate
        || invalidSession.state() != OpenHDK::PlaybackSessionState::Failed) return 54;
    if (!invalidSession.stop().succeeded() || invalidSession.state() != OpenHDK::PlaybackSessionState::Idle) return 55;

    OpenHDK::PlaybackSession session;
    const auto idlePlay = session.play();
    if (idlePlay.succeeded() || !idlePlay.error()
        || idlePlay.error()->code() != OpenHDK::PlaybackSessionErrorCode::IllegalTransition
        || session.state() != OpenHDK::PlaybackSessionState::Idle) return 56;
    if (!session.prepare(*crossTrackResult.timeline(), 1000000U).succeeded()
        || session.state() != OpenHDK::PlaybackSessionState::Ready) return 57;
    if (session.pause().succeeded()) return 58;
    if (!session.play().succeeded() || session.state() != OpenHDK::PlaybackSessionState::Playing) return 59;

    const auto zeroBlock = session.render(0U);
    if (!zeroBlock.succeeded() || zeroBlock.blockStartMicroseconds() != 0U
        || zeroBlock.blockEndMicroseconds() != 0U || zeroBlock.events().size() != 2U
        || zeroBlock.events()[0].trackIndex() != 0U || zeroBlock.events()[1].trackIndex() != 1U
        || session.mediaTimeMicroseconds() != 0U) return 60;
    const auto prematureComplete = session.completeReleaseTail();
    if (prematureComplete.succeeded() || !prematureComplete.error()
        || prematureComplete.error()->code()
            != OpenHDK::PlaybackSessionErrorCode::CompletionBeforeEndOfTimeline) return 61;

    if (!session.pause().succeeded() || session.state() != OpenHDK::PlaybackSessionState::Paused) return 62;
    const auto pausedBlock = session.render(100U);
    if (!pausedBlock.succeeded() || !pausedBlock.events().empty()
        || pausedBlock.blockStartMicroseconds() != 0U || pausedBlock.blockEndMicroseconds() != 0U
        || session.mediaTimeMicroseconds() != 0U) return 63;
    if (!session.play().succeeded()) return 64;
    const auto boundaryBlock = session.render(250000U);
    if (!boundaryBlock.succeeded() || boundaryBlock.blockStartMicroseconds() != 0U
        || boundaryBlock.blockEndMicroseconds() != 250000U || boundaryBlock.events().size() != 1U
        || boundaryBlock.events()[0].timeMicroseconds() != 250000U) return 65;
    const auto finalBlock = session.render(250000U);
    if (!finalBlock.succeeded() || finalBlock.events().size() != 3U
        || finalBlock.blockStartMicroseconds() != 250000U || finalBlock.blockEndMicroseconds() != 500000U
        || !session.endOfTimelineReached() || session.state() != OpenHDK::PlaybackSessionState::Playing) return 66;
    if (!session.completeReleaseTail().succeeded()
        || session.state() != OpenHDK::PlaybackSessionState::Finished) return 67;
    if (!session.stop().succeeded() || session.state() != OpenHDK::PlaybackSessionState::Idle
        || session.mediaTimeMicroseconds() != 0U) return 68;
    if (session.stop().succeeded()) return 69;

    OpenHDK::PlaybackSession splitOneBlock;
    OpenHDK::PlaybackSession splitTwoBlocks;
    if (!splitOneBlock.prepare(*crossTrackResult.timeline(), 3U).succeeded()
        || !splitTwoBlocks.prepare(*crossTrackResult.timeline(), 3U).succeeded()
        || !splitOneBlock.play().succeeded() || !splitTwoBlocks.play().succeeded()) return 70;
    if (!splitOneBlock.render(2U).succeeded() || !splitTwoBlocks.render(1U).succeeded()
        || !splitTwoBlocks.render(1U).succeeded()
        || splitOneBlock.mediaTimeMicroseconds() != 666666U
        || splitTwoBlocks.mediaTimeMicroseconds() != 666666U) return 71;

    OpenHDK::PlaybackSession overflowSession;
    if (!overflowSession.prepare(*crossTrackResult.timeline(), 1U).succeeded()
        || !overflowSession.play().succeeded()) return 72;
    const auto clockOverflow = overflowSession.render(std::numeric_limits<std::uint64_t>::max());
    if (clockOverflow.succeeded() || !clockOverflow.error()
        || clockOverflow.error()->code() != OpenHDK::PlaybackSessionErrorCode::ClockOverflow
        || overflowSession.state() != OpenHDK::PlaybackSessionState::Failed) return 73;

    std::cout << "SMF parser fixtures passed\n";
    return 0;
}
