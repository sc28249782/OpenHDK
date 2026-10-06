// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "audio/MidiVelocityCurve.hpp"
#include "tests/TestCheck.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

using OpenHDK::applyMidiVelocityCurve;
using OpenHDK::MidiVelocityCurve;

static_assert(applyMidiVelocityCurve(64U, MidiVelocityCurve::Soft) == 32U);
static_assert(applyMidiVelocityCurve(64U, MidiVelocityCurve::Hard) == 96U);
static_assert(noexcept(applyMidiVelocityCurve(64U, MidiVelocityCurve::Linear)));
static_assert(std::is_trivially_copyable_v<MidiVelocityCurve>);

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
    return 0;
}
