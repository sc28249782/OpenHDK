// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace OpenHDK {

inline constexpr std::size_t kMidiChannelCount = 16U;
inline constexpr std::uint8_t kMidiChannelVolumeController = 7U;
inline constexpr std::uint8_t kMidiResetAllControllers = 121U;
inline constexpr std::uint8_t kMidiDefaultChannelVolume = 100U;

using MidiChannelGains = std::array<float, kMidiChannelCount>;

[[nodiscard]] inline constexpr MidiChannelGains defaultMidiChannelGains() noexcept {
  MidiChannelGains gains{};
  gains.fill(1.0F);
  return gains;
}

[[nodiscard]] inline bool isNormalizedMidiChannelGain(float value) noexcept {
  return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

[[nodiscard]] inline bool areNormalizedMidiChannelGains(const MidiChannelGains& gains) noexcept {
  for (const float gain : gains) {
    if (!isNormalizedMidiChannelGain(gain)) return false;
  }
  return true;
}

[[nodiscard]] inline std::uint8_t applyMidiChannelGain(std::uint8_t sourceVolume,
                                                        float gain) noexcept {
  const auto scaled = std::lround(static_cast<float>(sourceVolume) * gain);
  return static_cast<std::uint8_t>(std::clamp(scaled, 0L, 127L));
}

} // namespace OpenHDK
