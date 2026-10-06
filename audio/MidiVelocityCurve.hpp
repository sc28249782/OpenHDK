// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include <cstdint>
#include <optional>

namespace OpenHDK {

enum class MidiVelocityCurve { Linear, Soft, Hard };

/// Maps one MIDI velocity without allocation or mutable state. Zero remains
/// zero; the dispatcher must still interpret a velocity-zero note-on as
/// note-off. Invalid velocity or curve values return no result.
[[nodiscard]] constexpr std::optional<std::uint8_t> applyMidiVelocityCurve(
    std::uint8_t velocity, MidiVelocityCurve curve) noexcept {
    if (velocity > 127U) return std::nullopt;

    const unsigned value = velocity;
    unsigned mapped{};
    switch (curve) {
    case MidiVelocityCurve::Linear:
        mapped = value;
        break;
    case MidiVelocityCurve::Soft:
        mapped = (value * value + 63U) / 127U;
        if (value != 0U && mapped == 0U) mapped = 1U;
        break;
    case MidiVelocityCurve::Hard: {
        const unsigned inverse = 127U - value;
        mapped = 127U - (inverse * inverse + 63U) / 127U;
        break;
    }
    default:
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(mapped);
}

} // namespace OpenHDK
