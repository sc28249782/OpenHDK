// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "audio/StereoPeakLimiter.hpp"
#include "tests/TestCheck.hpp"

#include <array>
#include <limits>

int main() {
    constexpr float tolerance = 1.0e-6F;
    const auto close = [](float value, float expected) {
        return std::abs(value - expected) <= tolerance;
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float largest = std::numeric_limits<float>::max();
    struct Vector { float left; float right; float expectedLeft; float expectedRight; };
    const std::array vectors{
        Vector{0.25F, -0.50F, 0.25F, -0.50F},
        Vector{1.0F, 0.5F, 0.98F, 0.49F},
        Vector{-2.0F, 1.0F, -0.98F, 0.49F},
        Vector{nan, 0.5F, 0.0F, 0.5F},
        Vector{inf, -inf, 0.0F, 0.0F},
        Vector{0.5F, nan, 0.5F, 0.0F},
        Vector{nan, 2.0F, 0.0F, 0.98F},
        Vector{largest, -largest / 2.0F, 0.98F, -0.49F},
        Vector{0.0F, 0.0F, 0.0F, 0.0F},
    };
    for (const auto& vector : vectors) {
        auto left = vector.left;
        auto right = vector.right;
        OpenHDK::limitStereoFrame(left, right);
        OPENHDK_FAIL_IF(1, !std::isfinite(left) || !std::isfinite(right));
        OPENHDK_FAIL_IF(2, !close(left, vector.expectedLeft) || !close(right, vector.expectedRight));
    }
    // Finite grid: peak ceiling, exact bypass, polarity, and linked balance.
    for (int l = -100; l <= 100; ++l) {
        for (int r = -100; r <= 100; ++r) {
            const float inputLeft = static_cast<float>(l) / 16.0F;
            const float inputRight = static_cast<float>(r) / 16.0F;
            auto left = inputLeft;
            auto right = inputRight;
            OpenHDK::limitStereoFrame(left, right);
            OPENHDK_FAIL_IF(3, !std::isfinite(left) || !std::isfinite(right)
                               || std::abs(left) > 0.98F + tolerance
                               || std::abs(right) > 0.98F + tolerance);
            if (std::max(std::abs(inputLeft), std::abs(inputRight)) <= 0.98F) {
                OPENHDK_FAIL_IF(4, left != inputLeft || right != inputRight);
            }
            OPENHDK_FAIL_IF(5, std::abs(static_cast<double>(left) * inputRight
                                        - static_cast<double>(right) * inputLeft) > 1.0e-6);
            OPENHDK_FAIL_IF(6, (left != 0.0F && std::signbit(left) != std::signbit(inputLeft))
                               || (right != 0.0F && std::signbit(right) != std::signbit(inputRight)));
        }
    }
    std::array<float, 10> whole{1.0F, 0.5F, -2.0F, 1.0F, nan, inf, 0.25F, -0.5F, largest, -largest};
    auto split = whole;
    OPENHDK_FAIL_IF(7, !OpenHDK::limitInterleavedStereo(whole));
    OPENHDK_FAIL_IF(8, !OpenHDK::limitInterleavedStereo(std::span{split}.first(2U))
                       || !OpenHDK::limitInterleavedStereo(std::span{split}.subspan(2U, 4U))
                       || !OpenHDK::limitInterleavedStereo(std::span{split}.subspan(6U))
                       || split != whole);
    std::array<float, 3> incomplete{2.0F, 1.0F, 3.0F};
    const auto unchanged = incomplete;
    OPENHDK_FAIL_IF(9, OpenHDK::limitInterleavedStereo(incomplete) || incomplete != unchanged);
    OPENHDK_FAIL_IF(10, !OpenHDK::limitInterleavedStereo({}));
    static_assert(noexcept(OpenHDK::limitStereoFrame(whole[0], whole[1])));
    static_assert(noexcept(OpenHDK::limitInterleavedStereo(whole)));
    return 0;
}
