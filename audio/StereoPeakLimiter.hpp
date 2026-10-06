// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include <algorithm>
#include <cmath>
#include <span>

namespace OpenHDK {

inline constexpr double kStereoPeakLimiterCeiling = 0.98;

// Stateless linked stereo limiting. Double intermediates avoid gain underflow
// for the largest finite float inputs. Neither path allocates or takes a lock.
inline void limitStereoFrame(float& left, float& right) noexcept {
    if (!std::isfinite(left)) left = 0.0F;
    if (!std::isfinite(right)) right = 0.0F;
    const auto peak = std::max(std::abs(static_cast<double>(left)),
                               std::abs(static_cast<double>(right)));
    if (peak <= kStereoPeakLimiterCeiling) return;
    const auto gain = kStereoPeakLimiterCeiling / peak;
    left = static_cast<float>(static_cast<double>(left) * gain);
    right = static_cast<float>(static_cast<double>(right) * gain);
}

// Reject an incomplete frame before changing any sample. Empty PCM succeeds.
[[nodiscard]] inline bool limitInterleavedStereo(std::span<float> samples) noexcept {
    if (samples.size() % 2U != 0U) return false;
    for (std::size_t offset = 0U; offset < samples.size(); offset += 2U) {
        limitStereoFrame(samples[offset], samples[offset + 1U]);
    }
    return true;
}

} // namespace OpenHDK
