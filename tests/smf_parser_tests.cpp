// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "TestCheck.hpp"
#include "audio/AudioBackend.hpp"
#include "audio/MidiChannelDiagnostics.hpp"
#include "audio/MidiRuntimeMixer.hpp"
#include "audio/SmfParser.hpp"
#include "audio/SmfTrackEventDecoder.hpp"
#include "audio/SmfTimelineCompiler.hpp"
#include "audio/PlaybackSession.hpp"
#include "audio/SmfMidiEventDispatcher.hpp"

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

enum class FakeMidiCommandKind {
    NoteOn,
    NoteOff,
    Controller,
    ProgramChange,
    PitchBend,
};

struct FakeMidiCommand {
    FakeMidiCommandKind kind;
    std::uint8_t channel;
    std::uint8_t first;
    std::uint16_t second;
};

class FakeMidiCommandSink final : public OpenHDK::MidiCommandSink {
public:
    void noteOn(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity) override {
        commands_[count_++] = {FakeMidiCommandKind::NoteOn, channel, note, velocity};
    }

    void noteOff(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity) override {
        commands_[count_++] = {FakeMidiCommandKind::NoteOff, channel, note, velocity};
    }

    void controller(std::uint8_t channel, std::uint8_t controller, std::uint8_t value) override {
        commands_[count_++] = {FakeMidiCommandKind::Controller, channel, controller, value};
    }

    void programChange(std::uint8_t channel, std::uint8_t program) override {
        commands_[count_++] = {FakeMidiCommandKind::ProgramChange, channel, program, 0U};
    }

    void pitchBend(std::uint8_t channel, std::uint16_t value) override {
        commands_[count_++] = {FakeMidiCommandKind::PitchBend, channel, 0U, value};
    }

    [[nodiscard]] std::size_t count() const { return count_; }
    [[nodiscard]] const FakeMidiCommand& command(std::size_t index) const { return commands_[index]; }

private:
    std::array<FakeMidiCommand, 8> commands_{};
    std::size_t count_{};
};

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
    OPENHDK_FAIL_IF(87, OpenHDK::isNormalizedVolume(std::numeric_limits<float>::quiet_NaN())
        || OpenHDK::isNormalizedVolume(std::numeric_limits<float>::infinity())
        || OpenHDK::isNormalizedVolume(-std::numeric_limits<float>::infinity())
        || !OpenHDK::isNormalizedVolume(0.0F) || !OpenHDK::isNormalizedVolume(1.0F)
        || OpenHDK::isNormalizedVolume(-0.01F) || OpenHDK::isNormalizedVolume(1.01F));
    const auto defaultChannelGains = OpenHDK::defaultMidiChannelGains();
    OPENHDK_FAIL_IF(94, !OpenHDK::areNormalizedMidiChannelGains(defaultChannelGains)
        || OpenHDK::applyMidiChannelGain(100U, 1.0F) != 100U
        || OpenHDK::applyMidiChannelGain(100U, 0.5F) != 50U
        || OpenHDK::applyMidiChannelGain(127U, 0.5F) != 64U
        || OpenHDK::applyMidiChannelGain(127U, 0.0F) != 0U);
    auto invalidChannelGains = defaultChannelGains;
    invalidChannelGains[9] = std::numeric_limits<float>::quiet_NaN();
    OPENHDK_FAIL_IF(95, OpenHDK::areNormalizedMidiChannelGains(invalidChannelGains));
    OpenHDK::MidiRuntimeMixer runtimeMixer;
    const auto defaultMixer = runtimeMixer.snapshot();
    OPENHDK_FAIL_IF(102, defaultMixer.revision != 0U || defaultMixer.outputGain(0U) != 1.0F
        || defaultMixer.outputGain(OpenHDK::kMidiChannelCount) != 0.0F
        || !runtimeMixer.isDefault());
    OPENHDK_FAIL_IF(103, !runtimeMixer.setGain(9U, 0.5F) || !runtimeMixer.setMuted(1U, true)
        || !runtimeMixer.setSoloed(9U, true) || runtimeMixer.setGain(16U, 0.5F)
        || runtimeMixer.setGain(0U, std::numeric_limits<float>::infinity())
        || runtimeMixer.setMuted(16U, true) || runtimeMixer.setSoloed(16U, true));
    const auto soloedMixer = runtimeMixer.snapshot();
    OPENHDK_FAIL_IF(104, soloedMixer.revision != 3U || soloedMixer.outputGain(1U) != 0.0F
        || soloedMixer.outputGain(8U) != 0.0F || soloedMixer.outputGain(9U) != 0.5F
        || OpenHDK::applyMidiChannelGain(110U, soloedMixer.outputGain(9U)) != 55U
        || runtimeMixer.isDefault());
    OPENHDK_FAIL_IF(105, !runtimeMixer.setSoloed(9U, false) || !runtimeMixer.setMuted(1U, false)
        || !runtimeMixer.setGain(9U, 1.0F) || !runtimeMixer.isDefault());
    OPENHDK_FAIL_IF(106, !runtimeMixer.setGain(9U, 0.5F));
    runtimeMixer.reset();
    OPENHDK_FAIL_IF(107, !runtimeMixer.isDefault());

    constexpr std::array<std::uint8_t, 26> format0{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,4, 0,0xff,0x2f,0};
    OPENHDK_FAIL_IF(1, !expectsSuccess(format0, 0U, 96U, 1U));
    auto mutableFormat0 = format0;
    const auto ownedResult = OpenHDK::SmfParser::parse(mutableFormat0);
    mutableFormat0[22] = 0x7fU;
    OPENHDK_FAIL_IF(7, !ownedResult.file() || ownedResult.file()->tracks()[0].bytes()[0] != 0U);
    const auto decodedOwnedTrack = OpenHDK::SmfTrackEventDecoder::decode(ownedResult.file()->tracks()[0]);
    OPENHDK_FAIL_IF(23, !decodedOwnedTrack.succeeded() || !decodedOwnedTrack.events()
        || decodedOwnedTrack.events()->events().size() != 1U);

    constexpr std::array<std::uint8_t, 34> format1{
        'M','T','h','d', 0,0,0,6, 0,1, 0,2, 1,0xe0,
        'M','T','r','k', 0,0,0,2, 0,0xff,
        'M','T','r','k', 0,0,0,2, 0,0xff};
    OPENHDK_FAIL_IF(2, !expectsSuccess(format1, 1U, 480U, 2U));

    constexpr std::array<std::uint8_t, 7> truncatedHeader{'M','T','h','d', 0,0,0};
    OPENHDK_FAIL_IF(3, !expectsError(truncatedHeader, SmfParseErrorCode::TruncatedHeader, 7U));

    constexpr std::array<std::uint8_t, 8> invalidHeaderLength{
        'M','T','h','d', 0,0,0,5};
    OPENHDK_FAIL_IF(8, !expectsError(invalidHeaderLength, SmfParseErrorCode::InvalidHeaderLength, 4U));

    constexpr std::array<std::uint8_t, 4> invalidMThdId{'M','T','x','d'};
    OPENHDK_FAIL_IF(19, !expectsError(invalidMThdId, SmfParseErrorCode::InvalidHeaderChunk, 2U));

    constexpr std::array<std::uint8_t, 14> unsupportedFormat{
        'M','T','h','d', 0,0,0,6, 0,2, 0,1, 0,96};
    OPENHDK_FAIL_IF(4, !expectsError(unsupportedFormat, SmfParseErrorCode::UnsupportedFormat, 8U));

    constexpr std::array<std::uint8_t, 14> invalidDivision{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,0};
    OPENHDK_FAIL_IF(5, !expectsError(invalidDivision, SmfParseErrorCode::InvalidDivision, 12U));

    constexpr std::array<std::uint8_t, 22> truncatedTrack{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,1};
    OPENHDK_FAIL_IF(6, !expectsError(truncatedTrack, SmfParseErrorCode::TruncatedTrackChunk, 22U));

    constexpr std::array<std::uint8_t, 3> partialMThdId{'M','T','h'};
    OPENHDK_FAIL_IF(9, !expectsError(partialMThdId, SmfParseErrorCode::TruncatedHeader, 3U));

    constexpr std::array<std::uint8_t, 6> partialMThdLength{'M','T','h','d', 0,0};
    OPENHDK_FAIL_IF(10, !expectsError(partialMThdLength, SmfParseErrorCode::TruncatedHeader, 6U));

    constexpr std::array<std::uint8_t, 12> partialMThdFields{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1};
    OPENHDK_FAIL_IF(11, !expectsError(partialMThdFields, SmfParseErrorCode::TruncatedHeader, 12U));

    constexpr std::array<std::uint8_t, 9> partialMThdFormat{
        'M','T','h','d', 0,0,0,6, 0};
    OPENHDK_FAIL_IF(20, !expectsError(partialMThdFormat, SmfParseErrorCode::TruncatedHeader, 9U));

    constexpr std::array<std::uint8_t, 11> partialMThdTrackCount{
        'M','T','h','d', 0,0,0,6, 0,0, 0};
    OPENHDK_FAIL_IF(21, !expectsError(partialMThdTrackCount, SmfParseErrorCode::TruncatedHeader, 11U));

    constexpr std::array<std::uint8_t, 13> partialMThdDivision{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0};
    OPENHDK_FAIL_IF(22, !expectsError(partialMThdDivision, SmfParseErrorCode::TruncatedHeader, 13U));

    constexpr std::array<std::uint8_t, 17> partialMTrkId{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96, 'M','T','r'};
    OPENHDK_FAIL_IF(12, !expectsError(partialMTrkId, SmfParseErrorCode::TruncatedTrackChunk, 17U));

    constexpr std::array<std::uint8_t, 20> partialMTrkLength{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96, 'M','T','r','k', 0,0};
    OPENHDK_FAIL_IF(13, !expectsError(partialMTrkLength, SmfParseErrorCode::TruncatedTrackChunk, 20U));

    constexpr std::array<std::uint8_t, 23> truncatedPayload{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,2, 0};
    OPENHDK_FAIL_IF(14, !expectsError(truncatedPayload, SmfParseErrorCode::TruncatedTrackChunk, 23U));

    constexpr std::array<std::uint8_t, 22> invalidMTrkId{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','x','k', 0,0,0,0};
    OPENHDK_FAIL_IF(15, !expectsError(invalidMTrkId, SmfParseErrorCode::InvalidTrackChunk, 16U));

    constexpr std::array<std::uint8_t, 14> format0TwoTracks{
        'M','T','h','d', 0,0,0,6, 0,0, 0,2, 0,96};
    OPENHDK_FAIL_IF(16, !expectsError(format0TwoTracks, SmfParseErrorCode::InvalidTrackCount, 10U));

    constexpr std::array<std::uint8_t, 14> format1ZeroTracks{
        'M','T','h','d', 0,0,0,6, 0,1, 0,0, 0,96};
    OPENHDK_FAIL_IF(17, !expectsError(format1ZeroTracks, SmfParseErrorCode::InvalidTrackCount, 10U));

    constexpr std::array<std::uint8_t, 27> trailingData{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,4, 0,0xff,0x2f,0, 0};
    OPENHDK_FAIL_IF(18, !expectsError(trailingData, SmfParseErrorCode::TrailingData, 26U));

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
    OPENHDK_FAIL_IF(24, !validEventsResult.succeeded() || !validEventsResult.events() || validEventsResult.error());
    const auto events = validEventsResult.events()->events();
    OPENHDK_FAIL_IF(25, events.size() != 9U || events[0].kind() != OpenHDK::SmfMidiEventKind::NoteOn
        || events[1].kind() != OpenHDK::SmfMidiEventKind::NoteOff || events[1].tick() != 128U
        || events[2].kind() != OpenHDK::SmfMidiEventKind::Controller || events[2].tick() != 128U
        || events[3].kind() != OpenHDK::SmfMidiEventKind::ProgramChange
        || events[4].kind() != OpenHDK::SmfMidiEventKind::PitchBend
        || events[5].kind() != OpenHDK::SmfMidiEventKind::Tempo || events[5].data().size() != 3U
        || events[6].kind() != OpenHDK::SmfMidiEventKind::Meta
        || events[7].kind() != OpenHDK::SmfMidiEventKind::SysEx
        || events[8].kind() != OpenHDK::SmfMidiEventKind::EndOfTrack);
    auto mutableEvents = validEvents;
    const auto ownedEventsResult = OpenHDK::SmfTrackEventDecoder::decode(mutableEvents);
    mutableEvents[3] = 0U;
    OPENHDK_FAIL_IF(33, !ownedEventsResult.events() || ownedEventsResult.events()->events()[0].data()[1] != 100U);

    constexpr std::array<std::uint8_t, 11> runningStatus{
        0, 0x90, 60, 64, 5, 61, 65, 0, 0xff, 0x2f, 0};
    const auto runningStatusResult = OpenHDK::SmfTrackEventDecoder::decode(runningStatus);
    OPENHDK_FAIL_IF(26, !runningStatusResult.succeeded() || !runningStatusResult.events()
        || runningStatusResult.events()->events().size() != 3U
        || runningStatusResult.events()->events()[1].tick() != 5U
        || runningStatusResult.events()->events()[1].data()[0] != 61U
        || runningStatusResult.events()->events()[2].kind() != OpenHDK::SmfMidiEventKind::EndOfTrack);

    constexpr std::array<std::uint8_t, 4> malformedVlq{0x81, 0x80, 0x80, 0x80};
    OPENHDK_FAIL_IF(27, !expectsTrackError(malformedVlq, SmfTrackDecodeErrorCode::MalformedVlq, 3U));

    constexpr std::array<std::uint8_t, 2> missingRunningStatus{0, 60};
    OPENHDK_FAIL_IF(28, !expectsTrackError(missingRunningStatus, SmfTrackDecodeErrorCode::MissingRunningStatus, 1U));

    constexpr std::array<std::uint8_t, 3> truncatedEventData{0, 0x90, 60};
    OPENHDK_FAIL_IF(29, !expectsTrackError(truncatedEventData, SmfTrackDecodeErrorCode::TruncatedEvent, 3U));

    constexpr std::array<std::uint8_t, 6> invalidTempoLength{0, 0xff, 0x51, 2, 0, 0};
    OPENHDK_FAIL_IF(30, !expectsTrackError(invalidTempoLength, SmfTrackDecodeErrorCode::InvalidMetaLength, 3U));

    constexpr std::array<std::uint8_t, 7> invalidZeroTempo{0, 0xff, 0x51, 3, 0, 0, 0};
    OPENHDK_FAIL_IF(88, !expectsTrackError(invalidZeroTempo, SmfTrackDecodeErrorCode::InvalidTempoValue, 4U));

    constexpr std::array<std::uint8_t, 5> truncatedMetaPayload{0, 0xff, 1, 2, 'x'};
    OPENHDK_FAIL_IF(31, !expectsTrackError(truncatedMetaPayload, SmfTrackDecodeErrorCode::TruncatedEvent, 5U));

    constexpr std::array<std::uint8_t, 4> truncatedSysExPayload{0, 0xf0, 2, 0x7d};
    OPENHDK_FAIL_IF(32, !expectsTrackError(truncatedSysExPayload, SmfTrackDecodeErrorCode::TruncatedEvent, 4U));

    constexpr std::array<std::uint8_t, 4> missingEndOfTrack{0, 0x90, 60, 64};
    OPENHDK_FAIL_IF(34, !expectsTrackError(missingEndOfTrack, SmfTrackDecodeErrorCode::MissingEndOfTrack, 4U));

    constexpr std::array<std::uint8_t, 8> trailingEventAfterEndOfTrack{
        0, 0xff, 0x2f, 0, 0, 0x90, 60, 64};
    OPENHDK_FAIL_IF(35, !expectsTrackError(trailingEventAfterEndOfTrack,
                           SmfTrackDecodeErrorCode::TrailingDataAfterEndOfTrack, 4U));

    constexpr std::array<std::uint8_t, 5> trailingDataAfterEndOfTrack{0, 0xff, 0x2f, 0, 0};
    OPENHDK_FAIL_IF(36, !expectsTrackError(trailingDataAfterEndOfTrack,
                           SmfTrackDecodeErrorCode::TrailingDataAfterEndOfTrack, 4U));

    constexpr std::array<std::uint8_t, 53> crossTrackTimeline{
        'M','T','h','d', 0,0,0,6, 0,1, 0,2, 0,96,
        'M','T','r','k', 0,0,0,12,
        0, 0x90, 60, 64, 0x60, 0x80, 60, 0, 0, 0xff, 0x2f, 0,
        'M','T','r','k', 0,0,0,11,
        0, 0xc0, 5, 0x30, 0xb0, 7, 100, 0x30, 0xff, 0x2f, 0};
    const auto crossTrackFile = OpenHDK::SmfParser::parse(crossTrackTimeline);
    OPENHDK_FAIL_IF(37, !crossTrackFile.file());
    const auto crossTrackResult = OpenHDK::SmfTimelineCompiler::compile(*crossTrackFile.file());
    OPENHDK_FAIL_IF(38, !crossTrackResult.succeeded() || !crossTrackResult.timeline() || crossTrackResult.error());
    const auto crossTrackEvents = crossTrackResult.timeline()->events();
    OPENHDK_FAIL_IF(39, crossTrackEvents.size() != 6U || crossTrackEvents[0].trackIndex() != 0U
        || crossTrackEvents[1].trackIndex() != 1U || crossTrackEvents[0].tick() != 0U
        || crossTrackEvents[1].tick() != 0U || crossTrackEvents[2].tick() != 48U
        || crossTrackEvents[2].timeMicroseconds() != 250000U
        || crossTrackEvents[3].tick() != 96U || crossTrackEvents[3].trackIndex() != 0U
        || crossTrackEvents[3].timeMicroseconds() != 500000U
        || crossTrackEvents[5].event().kind() != OpenHDK::SmfMidiEventKind::EndOfTrack);

    constexpr std::array<std::uint8_t, 48> tempoTimeline{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,100,
        'M','T','r','k', 0,0,0,26,
        0, 0xff, 0x51, 3, 7, 0xa1, 0x20,
        0x64, 0x90, 60, 64,
        0x64, 0xff, 0x51, 3, 3, 0xd0, 0x90,
        0x64, 0x80, 60, 0,
        0, 0xff, 0x2f, 0};
    const auto tempoFile = OpenHDK::SmfParser::parse(tempoTimeline);
    OPENHDK_FAIL_IF(40, !tempoFile.file());
    const auto tempoResult = OpenHDK::SmfTimelineCompiler::compile(*tempoFile.file());
    OPENHDK_FAIL_IF(41, !tempoResult.timeline() || tempoResult.timeline()->events().size() != 5U);
    const auto tempoEvents = tempoResult.timeline()->events();
    OPENHDK_FAIL_IF(42, tempoEvents[1].timeMicroseconds() != 500000U
        || tempoEvents[2].timeMicroseconds() != 1000000U
        || tempoEvents[3].timeMicroseconds() != 1250000U);

    constexpr std::array<std::uint8_t, 56> sameTickTempoTimeline{
        'M','T','h','d', 0,0,0,6, 0,1, 0,2, 0,100,
        'M','T','r','k', 0,0,0,11,
        0, 0xff, 0x51, 3, 7, 0xa1, 0x20, 0x64, 0xff, 0x2f, 0,
        'M','T','r','k', 0,0,0,15,
        0, 0xff, 0x51, 3, 3, 0xd0, 0x90,
        0x64, 0x90, 60, 64, 0, 0xff, 0x2f, 0};
    const auto sameTickTempoFile = OpenHDK::SmfParser::parse(sameTickTempoTimeline);
    OPENHDK_FAIL_IF(43, !sameTickTempoFile.file());
    const auto sameTickTempoResult = OpenHDK::SmfTimelineCompiler::compile(*sameTickTempoFile.file());
    OPENHDK_FAIL_IF(44, !sameTickTempoResult.timeline() || sameTickTempoResult.timeline()->events().size() != 5U);
    const auto sameTickTempoEvents = sameTickTempoResult.timeline()->events();
    OPENHDK_FAIL_IF(45, sameTickTempoEvents[0].trackIndex() != 0U || sameTickTempoEvents[1].trackIndex() != 1U
        || sameTickTempoEvents[3].event().kind() != OpenHDK::SmfMidiEventKind::NoteOn
        || sameTickTempoEvents[3].timeMicroseconds() != 250000U);

    constexpr std::array<std::uint8_t, 26> decoderFailureTimeline{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,4, 0, 0x90, 60, 64};
    const auto decoderFailureFile = OpenHDK::SmfParser::parse(decoderFailureTimeline);
    OPENHDK_FAIL_IF(46, !decoderFailureFile.file());
    const auto decoderFailureResult = OpenHDK::SmfTimelineCompiler::compile(*decoderFailureFile.file());
    OPENHDK_FAIL_IF(47, decoderFailureResult.succeeded() || !decoderFailureResult.error()
        || decoderFailureResult.error()->code() != SmfTimelineErrorCode::TrackDecodeFailed
        || decoderFailureResult.error()->trackIndex() != 0U
        || !decoderFailureResult.error()->trackDecodeError()
        || decoderFailureResult.error()->trackDecodeError()->code != SmfTrackDecodeErrorCode::MissingEndOfTrack
        || decoderFailureResult.error()->trackDecodeError()->offset != 4U);

    constexpr std::array<std::uint8_t, 24> smpteTimeline{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0xe7,40,
        'M','T','r','k', 0,0,0,2, 0, 0x90};
    const auto smpteFile = OpenHDK::SmfParser::parse(smpteTimeline);
    OPENHDK_FAIL_IF(48, !smpteFile.file());
    const auto smpteResult = OpenHDK::SmfTimelineCompiler::compile(*smpteFile.file());
    OPENHDK_FAIL_IF(49, smpteResult.succeeded() || !smpteResult.error()
        || smpteResult.error()->code() != SmfTimelineErrorCode::UnsupportedSmpteDivision);

    constexpr std::array<std::uint8_t, 34> fractionalTimeline{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,3,
        'M','T','r','k', 0,0,0,12,
        1, 0x90, 60, 64, 1, 0x80, 60, 0, 0, 0xff, 0x2f, 0};
    const auto fractionalFile = OpenHDK::SmfParser::parse(fractionalTimeline);
    OPENHDK_FAIL_IF(52, !fractionalFile.file());
    const auto fractionalResult = OpenHDK::SmfTimelineCompiler::compile(*fractionalFile.file());
    OPENHDK_FAIL_IF(53, !fractionalResult.timeline() || fractionalResult.timeline()->events().size() != 3U
        || fractionalResult.timeline()->events()[0].timeMicroseconds() != 166666U
        || fractionalResult.timeline()->events()[1].timeMicroseconds() != 333333U);

    std::vector<std::uint8_t> overflowTimeline{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,1,
        'M','T','r','k', 0,0,0x70,0x12,
        0, 0xff, 0x51, 3, 0xff, 0xff, 0xff};
    for (std::size_t index = 0U; index < 4097U; ++index) {
        overflowTimeline.insert(overflowTimeline.end(), {0xff, 0xff, 0xff, 0x7f, 0x90, 60, 64});
    }
    overflowTimeline.insert(overflowTimeline.end(), {0, 0xff, 0x2f, 0});
    const auto overflowFile = OpenHDK::SmfParser::parse(overflowTimeline);
    OPENHDK_FAIL_IF(50, !overflowFile.file());
    const auto overflowResult = OpenHDK::SmfTimelineCompiler::compile(*overflowFile.file());
    OPENHDK_FAIL_IF(51, overflowResult.succeeded() || !overflowResult.error()
        || overflowResult.error()->code() != SmfTimelineErrorCode::TimeOverflow);

    OpenHDK::PlaybackSession invalidSession;
    const auto invalidPrepare = invalidSession.prepare(*crossTrackResult.timeline(), 0U);
    OPENHDK_FAIL_IF(54, invalidPrepare.succeeded() || !invalidPrepare.error()
        || invalidPrepare.error()->code() != OpenHDK::PlaybackSessionErrorCode::InvalidSampleRate
        || invalidSession.state() != OpenHDK::PlaybackSessionState::Failed);
    OPENHDK_FAIL_IF(55, !invalidSession.stop().succeeded() || invalidSession.state() != OpenHDK::PlaybackSessionState::Idle);

    OpenHDK::PlaybackSession session;
    const auto idlePlay = session.play();
    OPENHDK_FAIL_IF(56, idlePlay.succeeded() || !idlePlay.error()
        || idlePlay.error()->code() != OpenHDK::PlaybackSessionErrorCode::IllegalTransition
        || session.state() != OpenHDK::PlaybackSessionState::Idle);
    OPENHDK_FAIL_IF(57, !session.prepare(*crossTrackResult.timeline(), 1000000U).succeeded()
        || session.state() != OpenHDK::PlaybackSessionState::Ready);
    OPENHDK_FAIL_IF(58, session.pause().succeeded());
    OPENHDK_FAIL_IF(59, !session.play().succeeded() || session.state() != OpenHDK::PlaybackSessionState::Playing);

    const auto zeroBlock = session.render(0U);
    OPENHDK_FAIL_IF(60, !zeroBlock.succeeded() || zeroBlock.blockStartMicroseconds() != 0U
        || zeroBlock.blockEndMicroseconds() != 0U || zeroBlock.events().size() != 2U
        || zeroBlock.events()[0].trackIndex() != 0U || zeroBlock.events()[1].trackIndex() != 1U
        || session.mediaTimeMicroseconds() != 0U);
    FakeMidiCommandSink sameTickSink;
    OpenHDK::SmfMidiEventDispatcher::dispatch(zeroBlock.events(), sameTickSink);
    OPENHDK_FAIL_IF(82, sameTickSink.count() != 2U || sameTickSink.command(0).kind != FakeMidiCommandKind::NoteOn
        || sameTickSink.command(0).channel != 0U
        || sameTickSink.command(1).kind != FakeMidiCommandKind::ProgramChange
        || sameTickSink.command(1).channel != 0U || sameTickSink.command(1).first != 5U);
    const auto prematureComplete = session.completeReleaseTail();
    OPENHDK_FAIL_IF(61, prematureComplete.succeeded() || !prematureComplete.error()
        || prematureComplete.error()->code()
            != OpenHDK::PlaybackSessionErrorCode::CompletionBeforeEndOfTimeline);

    OPENHDK_FAIL_IF(62, !session.pause().succeeded() || session.state() != OpenHDK::PlaybackSessionState::Paused);
    const auto pausedBlock = session.render(100U);
    OPENHDK_FAIL_IF(63, !pausedBlock.succeeded() || !pausedBlock.events().empty()
        || pausedBlock.blockStartMicroseconds() != 0U || pausedBlock.blockEndMicroseconds() != 0U
        || session.mediaTimeMicroseconds() != 0U);
    OPENHDK_FAIL_IF(64, !session.play().succeeded());
    const auto boundaryBlock = session.render(250000U);
    OPENHDK_FAIL_IF(65, !boundaryBlock.succeeded() || boundaryBlock.blockStartMicroseconds() != 0U
        || boundaryBlock.blockEndMicroseconds() != 250000U || boundaryBlock.events().size() != 1U
        || boundaryBlock.events()[0].timeMicroseconds() != 250000U);
    const auto finalBlock = session.render(250000U);
    OPENHDK_FAIL_IF(66, !finalBlock.succeeded() || finalBlock.events().size() != 3U
        || finalBlock.blockStartMicroseconds() != 250000U || finalBlock.blockEndMicroseconds() != 500000U
        || !session.endOfTimelineReached() || session.state() != OpenHDK::PlaybackSessionState::Playing);
    OPENHDK_FAIL_IF(67, !session.completeReleaseTail().succeeded()
        || session.state() != OpenHDK::PlaybackSessionState::Finished);
    OPENHDK_FAIL_IF(68, !session.stop().succeeded() || session.state() != OpenHDK::PlaybackSessionState::Idle
        || session.mediaTimeMicroseconds() != 0U);
    OPENHDK_FAIL_IF(69, session.stop().succeeded());

    OpenHDK::PlaybackSession splitOneBlock;
    OpenHDK::PlaybackSession splitTwoBlocks;
    OPENHDK_FAIL_IF(70, !splitOneBlock.prepare(*crossTrackResult.timeline(), 3U).succeeded()
        || !splitTwoBlocks.prepare(*crossTrackResult.timeline(), 3U).succeeded()
        || !splitOneBlock.play().succeeded() || !splitTwoBlocks.play().succeeded());
    OPENHDK_FAIL_IF(71, !splitOneBlock.render(2U).succeeded() || !splitTwoBlocks.render(1U).succeeded()
        || !splitTwoBlocks.render(1U).succeeded()
        || splitOneBlock.mediaTimeMicroseconds() != 666666U
        || splitTwoBlocks.mediaTimeMicroseconds() != 666666U);

    OpenHDK::PlaybackSession overflowSession;
    OPENHDK_FAIL_IF(72, !overflowSession.prepare(*crossTrackResult.timeline(), 1U).succeeded()
        || !overflowSession.play().succeeded());
    const auto clockOverflow = overflowSession.render(std::numeric_limits<std::uint64_t>::max());
    OPENHDK_FAIL_IF(73, clockOverflow.succeeded() || !clockOverflow.error()
        || clockOverflow.error()->code() != OpenHDK::PlaybackSessionErrorCode::ClockOverflow
        || overflowSession.state() != OpenHDK::PlaybackSessionState::Failed);

    constexpr std::array<std::uint8_t, 45> dispatchChannelEvents{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,23,
        0, 0x90, 60, 100,
        0, 0x80, 60, 12,
        0, 0xb1, 7, 101,
        0, 0xc2, 5,
        0, 0xe3, 0, 64,
        0, 0xff, 0x2f, 0};
    const auto dispatchChannelFile = OpenHDK::SmfParser::parse(dispatchChannelEvents);
    OPENHDK_FAIL_IF(74, !dispatchChannelFile.file());
    const auto dispatchChannelTimeline = OpenHDK::SmfTimelineCompiler::compile(*dispatchChannelFile.file());
    OPENHDK_FAIL_IF(75, !dispatchChannelTimeline.timeline());
    OpenHDK::PlaybackSession dispatchChannelSession;
    OPENHDK_FAIL_IF(76, !dispatchChannelSession.prepare(*dispatchChannelTimeline.timeline(), 1000000U).succeeded()
        || !dispatchChannelSession.play().succeeded());
    const auto dispatchChannelBlock = dispatchChannelSession.render(0U);
    FakeMidiCommandSink channelSink;
    OpenHDK::SmfMidiEventDispatcher::dispatch(dispatchChannelBlock.events(), channelSink);
    OPENHDK_FAIL_IF(77, !dispatchChannelBlock.succeeded() || channelSink.count() != 5U
        || channelSink.command(0).kind != FakeMidiCommandKind::NoteOn
        || channelSink.command(0).channel != 0U || channelSink.command(0).first != 60U
        || channelSink.command(0).second != 100U
        || channelSink.command(1).kind != FakeMidiCommandKind::NoteOff
        || channelSink.command(1).second != 12U
        || channelSink.command(2).kind != FakeMidiCommandKind::Controller
        || channelSink.command(2).channel != 1U || channelSink.command(2).first != 7U
        || channelSink.command(2).second != 101U
        || channelSink.command(3).kind != FakeMidiCommandKind::ProgramChange
        || channelSink.command(3).channel != 2U || channelSink.command(3).first != 5U
        || channelSink.command(4).kind != FakeMidiCommandKind::PitchBend
        || channelSink.command(4).channel != 3U || channelSink.command(4).second != 8192U);

    constexpr std::array<std::uint8_t, 30> noteOnZeroVelocity{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,8,
        0, 0x94, 61, 0, 0, 0xff, 0x2f, 0};
    const auto noteOnZeroFile = OpenHDK::SmfParser::parse(noteOnZeroVelocity);
    OPENHDK_FAIL_IF(78, !noteOnZeroFile.file());
    const auto noteOnZeroTimeline = OpenHDK::SmfTimelineCompiler::compile(*noteOnZeroFile.file());
    OPENHDK_FAIL_IF(79, !noteOnZeroTimeline.timeline());
    OpenHDK::PlaybackSession noteOnZeroSession;
    OPENHDK_FAIL_IF(80, !noteOnZeroSession.prepare(*noteOnZeroTimeline.timeline(), 1000000U).succeeded()
        || !noteOnZeroSession.play().succeeded());
    FakeMidiCommandSink zeroVelocitySink;
    OpenHDK::SmfMidiEventDispatcher::dispatch(noteOnZeroSession.render(0U).events(), zeroVelocitySink);
    OPENHDK_FAIL_IF(81, zeroVelocitySink.count() != 1U
        || zeroVelocitySink.command(0).kind != FakeMidiCommandKind::NoteOff
        || zeroVelocitySink.command(0).channel != 4U || zeroVelocitySink.command(0).first != 61U
        || zeroVelocitySink.command(0).second != 0U);

    constexpr std::array<std::uint8_t, 47> ignoredDispatchEvents{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,25,
        0, 0xff, 0x51, 3, 7, 0xa1, 0x20,
        0, 0xff, 1, 1, 'x',
        0, 0xf0, 2, 0x7d, 1,
        0, 0x90, 60, 1,
        0, 0xff, 0x2f, 0};
    const auto ignoredDispatchFile = OpenHDK::SmfParser::parse(ignoredDispatchEvents);
    OPENHDK_FAIL_IF(83, !ignoredDispatchFile.file());
    const auto ignoredDispatchTimeline = OpenHDK::SmfTimelineCompiler::compile(*ignoredDispatchFile.file());
    OPENHDK_FAIL_IF(84, !ignoredDispatchTimeline.timeline());
    OpenHDK::PlaybackSession ignoredDispatchSession;
    OPENHDK_FAIL_IF(85, !ignoredDispatchSession.prepare(*ignoredDispatchTimeline.timeline(), 1000000U).succeeded()
        || !ignoredDispatchSession.play().succeeded());
    FakeMidiCommandSink ignoredSink;
    OpenHDK::SmfMidiEventDispatcher::dispatch(ignoredDispatchSession.render(0U).events(), ignoredSink);
    OPENHDK_FAIL_IF(86, ignoredSink.count() != 1U || ignoredSink.command(0).kind != FakeMidiCommandKind::NoteOn
        || ignoredSink.command(0).first != 60U || ignoredSink.command(0).second != 1U);

    constexpr std::array<std::uint8_t, 41> pressureCompatibilityEvents{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,19,
        0, 0xa2, 60, 50,
        0, 0xd3, 40,
        0, 0x90, 61, 64,
        0, 0xb4, 7, 100,
        0, 0xff, 0x2f, 0};
    const auto pressureCompatibilityFile = OpenHDK::SmfParser::parse(pressureCompatibilityEvents);
    OPENHDK_FAIL_IF(89, !pressureCompatibilityFile.file());
    const auto pressureCompatibilityTimeline = OpenHDK::SmfTimelineCompiler::compile(
        *pressureCompatibilityFile.file());
    OPENHDK_FAIL_IF(90, !pressureCompatibilityTimeline.timeline()
        || pressureCompatibilityTimeline.timeline()->events().size() != 5U
        || pressureCompatibilityTimeline.timeline()->events()[0].event().kind()
            != OpenHDK::SmfMidiEventKind::PolyphonicKeyPressure
        || pressureCompatibilityTimeline.timeline()->events()[1].event().kind()
            != OpenHDK::SmfMidiEventKind::ChannelPressure);
    OpenHDK::PlaybackSession pressureCompatibilitySession;
    OPENHDK_FAIL_IF(91, !pressureCompatibilitySession.prepare(*pressureCompatibilityTimeline.timeline(), 1000000U).succeeded()
        || !pressureCompatibilitySession.play().succeeded());
    FakeMidiCommandSink pressureCompatibilitySink;
    OpenHDK::SmfMidiEventDispatcher::dispatch(
        pressureCompatibilitySession.render(0U).events(), pressureCompatibilitySink);
    OPENHDK_FAIL_IF(92, pressureCompatibilitySink.count() != 2U
        || pressureCompatibilitySink.command(0).kind != FakeMidiCommandKind::NoteOn
        || pressureCompatibilitySink.command(0).channel != 0U
        || pressureCompatibilitySink.command(0).first != 61U
        || pressureCompatibilitySink.command(1).kind != FakeMidiCommandKind::Controller
        || pressureCompatibilitySink.command(1).channel != 4U
        || pressureCompatibilitySink.command(1).first != 7U
        || pressureCompatibilitySink.command(1).second != 100U);

    constexpr std::array<std::uint8_t, 67> diagnosticEvents{
        'M','T','h','d', 0,0,0,6, 0,1, 0,2, 0,96,
        'M','T','r','k', 0,0,0,22,
        0, 0xc0, 40,
        0, 0x90, 60, 100,
        0, 0x90, 61, 0,
        0, 0xb0, 7, 80,
        0, 0xc0, 2,
        0, 0xff, 0x2f, 0,
        'M','T','r','k', 0,0,0,15,
        0, 0xc0, 40,
        0, 0xb0, 7, 64,
        0, 0x99, 36, 127,
        0, 0xff, 0x2f, 0};
    const auto diagnosticFile = OpenHDK::SmfParser::parse(diagnosticEvents);
    OPENHDK_FAIL_IF(96, !diagnosticFile.file());
    const auto diagnosticTimeline = OpenHDK::SmfTimelineCompiler::compile(*diagnosticFile.file());
    OPENHDK_FAIL_IF(97, !diagnosticTimeline.timeline());
    const auto diagnostics = OpenHDK::MidiChannelDiagnostics::aggregate(
        *diagnosticTimeline.timeline());
    OPENHDK_FAIL_IF(98, diagnostics[0].noteOnCount != 1U
        || !diagnostics[0].observedPrograms[2]
        || !diagnostics[0].observedPrograms[40]
        || diagnostics[0].observedPrograms[3]
        || !diagnostics[0].cc7.first || diagnostics[0].cc7.first->value != 80U
        || !diagnostics[0].cc7.final || diagnostics[0].cc7.final->value != 64U
        || diagnostics[0].cc7.beforeFirstNote.has_value()
        || diagnostics[0].cc7.minimum != 64U || diagnostics[0].cc7.maximum != 80U
        || diagnostics[0].cc7.changeCount != 1U
        || !diagnostics[0].effectiveCc7.final
        || diagnostics[0].effectiveCc7.final->value != 64U
        || diagnostics[9].noteOnCount != 1U
        || diagnostics[9].cc7.final.has_value()
        || diagnostics[15].noteOnCount != 0U
        || diagnostics[15].cc7.final.has_value());

    constexpr std::array<std::uint8_t, 62> controllerHistoryEvents{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,40,
        0, 0xb0, 7, 100,
        0, 0xb0, 39, 3,
        0, 0xb0, 11, 127,
        0, 0xb0, 43, 2,
        96, 0x90, 60, 100,
        96, 0xb0, 7, 0,
        0, 0xb0, 11, 64,
        0, 0xb0, 39, 4,
        0, 0xb0, 121, 0,
        0, 0xff, 0x2f, 0};
    const auto controllerHistoryFile = OpenHDK::SmfParser::parse(controllerHistoryEvents);
    OPENHDK_FAIL_IF(99, !controllerHistoryFile.file());
    const auto controllerHistoryTimeline = OpenHDK::SmfTimelineCompiler::compile(
        *controllerHistoryFile.file());
    OPENHDK_FAIL_IF(100, !controllerHistoryTimeline.timeline());
    const auto controllerHistory = OpenHDK::MidiChannelDiagnostics::aggregate(
        *controllerHistoryTimeline.timeline());
    const auto& history = controllerHistory[0];
    OPENHDK_FAIL_IF(101, history.noteOnCount != 1U
        || !history.cc7.first || history.cc7.first->value != 100U
        || history.cc7.first->tick != 0U || history.cc7.first->timeMicroseconds != 0U
        || !history.cc7.beforeFirstNote || history.cc7.beforeFirstNote->value != 100U
        || !history.cc7.final || history.cc7.final->value != 0U
        || history.cc7.final->tick != 192U || history.cc7.final->timeMicroseconds != 1000000U
        || history.cc7.minimum != 0U || history.cc7.maximum != 100U
        || history.cc7.changeCount != 1U || history.finalCc7FourteenBit != 4U
        || !history.effectiveCc7.first || history.effectiveCc7.first->value != 100U
        || !history.effectiveCc7.beforeFirstNote
        || history.effectiveCc7.beforeFirstNote->value != 100U
        || !history.effectiveCc7.final || history.effectiveCc7.final->value != 100U
        || history.effectiveCc7.final->tick != 192U
        || history.effectiveCc7.final->timeMicroseconds != 1000000U
        || history.effectiveCc7.minimum != 0U || history.effectiveCc7.maximum != 100U
        || history.effectiveCc7.changeCount != 2U
        || !history.cc39.final || history.cc39.final->value != 4U
        || history.cc39.changeCount != 1U
        || !history.cc11.beforeFirstNote || history.cc11.beforeFirstNote->value != 127U
        || !history.cc11.final || history.cc11.final->value != 64U
        || history.cc11.minimum != 64U || history.cc11.maximum != 127U
        || history.cc11.changeCount != 1U || history.finalCc11FourteenBit != 8194U
        || !history.cc43.final || history.cc43.final->value != 2U
        || history.cc43.changeCount != 0U || history.resetAllControllersCount != 1U
        || !history.finalCc121Reset || history.finalCc121Reset->tick != 192U
        || history.finalCc121Reset->timeMicroseconds != 1000000U);

    std::cout << "SMF parser fixtures passed\n";
    return 0;
}
