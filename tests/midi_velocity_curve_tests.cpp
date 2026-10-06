// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "audio/MidiVelocityCurve.hpp"
#include "audio/SmfMidiEventDispatcher.hpp"
#include "audio/SmfParser.hpp"
#include "tests/TestCheck.hpp"

#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

using OpenHDK::applyMidiVelocityCurve;
using OpenHDK::MidiVelocityCurve;

static_assert(applyMidiVelocityCurve(64U, MidiVelocityCurve::Soft) == 32U);
static_assert(applyMidiVelocityCurve(64U, MidiVelocityCurve::Hard) == 96U);
static_assert(noexcept(applyMidiVelocityCurve(64U, MidiVelocityCurve::Linear)));
static_assert(std::is_trivially_copyable_v<MidiVelocityCurve>);

namespace {
struct Command {
    char kind;
    std::uint8_t channel;
    std::uint8_t first;
    std::uint16_t second;
    bool operator==(const Command&) const = default;
};
class RecordingSink final : public OpenHDK::MidiCommandSink {
public:
    std::vector<Command> commands;
    void noteOn(std::uint8_t c, std::uint8_t n, std::uint8_t v) override { commands.push_back({'N', c, n, v}); }
    void noteOff(std::uint8_t c, std::uint8_t n, std::uint8_t v) override { commands.push_back({'O', c, n, v}); }
    void controller(std::uint8_t c, std::uint8_t n, std::uint8_t v) override { commands.push_back({'C', c, n, v}); }
    void programChange(std::uint8_t c, std::uint8_t p) override { commands.push_back({'P', c, p, 0U}); }
    void pitchBend(std::uint8_t c, std::uint16_t v) override { commands.push_back({'B', c, 0U, v}); }
};
}

int main() {
    struct Vector {
        std::uint8_t input;
        std::uint8_t linear;
        std::uint8_t soft;
        std::uint8_t hard;
    };
    constexpr std::array vectors{
        Vector{0U, 0U, 0U, 0U},
        Vector{1U, 1U, 1U, 2U},
        Vector{64U, 64U, 32U, 96U},
        Vector{127U, 127U, 127U, 127U},
    };
    for (const auto& vector : vectors) {
        OPENHDK_FAIL_IF(1, applyMidiVelocityCurve(vector.input, MidiVelocityCurve::Linear)
                              != vector.linear);
        OPENHDK_FAIL_IF(2, applyMidiVelocityCurve(vector.input, MidiVelocityCurve::Soft)
                              != vector.soft);
        OPENHDK_FAIL_IF(3, applyMidiVelocityCurve(vector.input, MidiVelocityCurve::Hard)
                              != vector.hard);
    }

    // An independent nearest-integer oracle searches all candidate outputs.
    // The denominator is odd, so no exact half-way tie can occur.
    const auto nearest = [](unsigned numerator) {
        unsigned best{};
        unsigned distance = numerator;
        for (unsigned candidate = 1U; candidate <= 127U; ++candidate) {
            const unsigned product = candidate * 127U;
            const unsigned difference = product > numerator ? product - numerator
                                                           : numerator - product;
            if (difference < distance) {
                best = candidate;
                distance = difference;
            }
        }
        return best;
    };
    unsigned previousSoft{};
    unsigned previousHard{};
    for (unsigned value = 1U; value <= 127U; ++value) {
        const auto velocity = static_cast<std::uint8_t>(value);
        const auto linear = applyMidiVelocityCurve(velocity, MidiVelocityCurve::Linear);
        const auto soft = applyMidiVelocityCurve(velocity, MidiVelocityCurve::Soft);
        const auto hard = applyMidiVelocityCurve(velocity, MidiVelocityCurve::Hard);
        OPENHDK_FAIL_IF(4, !linear || !soft || !hard);
        const unsigned roundedSoft = nearest(value * value);
        const unsigned inverse = 127U - value;
        OPENHDK_FAIL_IF(5, *linear != value);
        OPENHDK_FAIL_IF(6, *soft != (roundedSoft == 0U ? 1U : roundedSoft));
        OPENHDK_FAIL_IF(7, *hard != 127U - nearest(inverse * inverse));
        OPENHDK_FAIL_IF(8, *soft < 1U || *soft > value || *hard < value || *hard > 127U);
        OPENHDK_FAIL_IF(9, *soft < previousSoft || *hard < previousHard);
        previousSoft = *soft;
        previousHard = *hard;
    }

    constexpr std::array curves{MidiVelocityCurve::Linear, MidiVelocityCurve::Soft,
                                MidiVelocityCurve::Hard};
    for (const auto curve : curves) {
        for (unsigned value = 128U; value <= 255U; ++value) {
            OPENHDK_FAIL_IF(10, applyMidiVelocityCurve(static_cast<std::uint8_t>(value), curve));
        }
    }
    const auto invalidCurve = static_cast<MidiVelocityCurve>(999);
    OPENHDK_FAIL_IF(11, applyMidiVelocityCurve(64U, invalidCurve));
    OPENHDK_FAIL_IF(12, applyMidiVelocityCurve(0U, invalidCurve));
    OPENHDK_FAIL_IF(13, OpenHDK::parseMidiVelocityCurve("linear") != MidiVelocityCurve::Linear
                       || OpenHDK::parseMidiVelocityCurve("soft") != MidiVelocityCurve::Soft
                       || OpenHDK::parseMidiVelocityCurve("hard") != MidiVelocityCurve::Hard);
    OPENHDK_FAIL_IF(14, OpenHDK::parseMidiVelocityCurve("")
                       || OpenHDK::parseMidiVelocityCurve("Soft")
                       || OpenHDK::parseMidiVelocityCurve("soft "));

    const std::vector<std::uint8_t> track{
        0U,0x90U,60U,64U, 0U,0xb0U,7U,100U, 0U,0xb0U,11U,60U,
        1U,0x80U,60U,12U, 0U,0x90U,61U,0U, 0U,0xc0U,7U,
        0U,0xe0U,0U,64U, 0U,0xa0U,60U,70U, 0U,0xffU,0x2fU,0U};
    std::vector<std::uint8_t> bytes{
        'M','T','h','d',0,0,0,6,0,0,0,1,0,96,'M','T','r','k'};
    for (const unsigned shift : {24U, 16U, 8U, 0U}) {
        bytes.push_back(static_cast<std::uint8_t>(track.size() >> shift));
    }
    bytes.insert(bytes.end(), track.begin(), track.end());
    const auto originalBytes = bytes;
    const auto parsed = OpenHDK::SmfParser::parse(bytes);
    OPENHDK_FAIL_IF(15, !parsed.file());
    const auto compiled = OpenHDK::SmfTimelineCompiler::compile(*parsed.file());
    OPENHDK_FAIL_IF(16, !compiled.timeline());
    const auto events = compiled.timeline()->events();
    std::vector<std::vector<std::uint8_t>> sourceData;
    std::vector<std::uint64_t> sourceTimes;
    for (const auto& event : events) {
        sourceData.emplace_back(event.event().data().begin(), event.event().data().end());
        sourceTimes.push_back(event.timeMicroseconds());
    }
    for (const auto curve : curves) {
        RecordingSink sink;
        const auto expectedVelocity = curve == MidiVelocityCurve::Linear ? 64U
                                    : curve == MidiVelocityCurve::Soft ? 32U : 96U;
        const std::vector<Command> expected{
            {'N',0U,60U,static_cast<std::uint16_t>(expectedVelocity)},
            {'C',0U,7U,100U}, {'C',0U,11U,60U}, {'O',0U,60U,12U},
            {'O',0U,61U,0U}, {'P',0U,7U,0U}, {'B',0U,0U,8192U}};
        OPENHDK_FAIL_IF(17, !OpenHDK::SmfMidiEventDispatcher::dispatch(events, sink, curve)
                           || sink.commands != expected);
        std::size_t index{};
        for (const auto& event : events) {
            OPENHDK_FAIL_IF(18, std::vector<std::uint8_t>(event.event().data().begin(), event.event().data().end()) != sourceData[index]
                               || event.timeMicroseconds() != sourceTimes[index]);
            ++index;
        }
    }
    RecordingSink rejected;
    OPENHDK_FAIL_IF(19, OpenHDK::SmfMidiEventDispatcher::dispatch(events, rejected, invalidCurve)
                       || !rejected.commands.empty() || bytes != originalBytes);
    RecordingSink defaultSink;
    RecordingSink linearSink;
    OpenHDK::SmfMidiEventDispatcher::dispatch(events, defaultSink);
    OPENHDK_FAIL_IF(20, !OpenHDK::SmfMidiEventDispatcher::dispatch(events, linearSink, MidiVelocityCurve::Linear)
                       || defaultSink.commands != linearSink.commands);
    return 0;
}
