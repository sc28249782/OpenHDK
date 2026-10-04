// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "audio/MidiChannelMix.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace OpenHDK {

inline constexpr std::uint32_t kMidiRuntimeGainScale = 1000000U;
inline constexpr std::uint32_t kMidiRuntimeChannelMask = (1U << kMidiChannelCount) - 1U;

struct MidiRuntimeMixerSnapshot {
    MidiChannelGains gains{defaultMidiChannelGains()};
    std::uint32_t mutedChannels{};
    std::uint32_t soloedChannels{};
    std::uint32_t revision{};

    [[nodiscard]] float outputGain(std::size_t channel) const noexcept {
        if (channel >= kMidiChannelCount || (mutedChannels & (1U << channel)) != 0U) return 0.0F;
        if (soloedChannels != 0U && (soloedChannels & (1U << channel)) == 0U) return 0.0F;
        return gains[channel];
    }
};

class MidiRuntimeMixer {
public:
    MidiRuntimeMixer() noexcept {
        for (auto& gain : gains_) gain.store(kMidiRuntimeGainScale, std::memory_order_relaxed);
    }

    [[nodiscard]] bool setGain(std::size_t channel, float gain) noexcept {
        if (channel >= kMidiChannelCount || !isNormalizedMidiChannelGain(gain)) return false;
        gains_[channel].store(encodeGain(gain), std::memory_order_release);
        publish();
        return true;
    }

    [[nodiscard]] bool setMuted(std::size_t channel, bool muted) noexcept {
        if (channel >= kMidiChannelCount) return false;
        updateMask(mutedChannels_, channel, muted);
        publish();
        return true;
    }

    [[nodiscard]] bool setSoloed(std::size_t channel, bool soloed) noexcept {
        if (channel >= kMidiChannelCount) return false;
        updateMask(soloedChannels_, channel, soloed);
        publish();
        return true;
    }

    void reset() noexcept {
        for (auto& gain : gains_) gain.store(kMidiRuntimeGainScale, std::memory_order_release);
        mutedChannels_.store(0U, std::memory_order_release);
        soloedChannels_.store(0U, std::memory_order_release);
        publish();
    }

    [[nodiscard]] MidiRuntimeMixerSnapshot snapshot() const noexcept {
        MidiRuntimeMixerSnapshot result{};
        result.revision = revision_.load(std::memory_order_acquire);
        for (std::size_t channel = 0U; channel < kMidiChannelCount; ++channel) {
            result.gains[channel] = decodeGain(gains_[channel].load(std::memory_order_acquire));
        }
        result.mutedChannels = mutedChannels_.load(std::memory_order_acquire) & kMidiRuntimeChannelMask;
        result.soloedChannels = soloedChannels_.load(std::memory_order_acquire) & kMidiRuntimeChannelMask;
        return result;
    }

    [[nodiscard]] std::uint32_t revision() const noexcept {
        return revision_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool isDefault() const noexcept {
        const auto current = snapshot();
        if (current.mutedChannels != 0U || current.soloedChannels != 0U) return false;
        for (const float gain : current.gains) {
            if (gain != 1.0F) return false;
        }
        return true;
    }

private:
    [[nodiscard]] static std::uint32_t encodeGain(float gain) noexcept {
        return static_cast<std::uint32_t>(std::lround(gain * static_cast<float>(kMidiRuntimeGainScale)));
    }

    [[nodiscard]] static float decodeGain(std::uint32_t gain) noexcept {
        return static_cast<float>(gain) / static_cast<float>(kMidiRuntimeGainScale);
    }

    static void updateMask(std::atomic<std::uint32_t>& target, std::size_t channel,
                           bool enabled) noexcept {
        const auto bit = 1U << channel;
        auto current = target.load(std::memory_order_relaxed);
        while (true) {
            const auto updated = enabled ? (current | bit) : (current & ~bit);
            if (target.compare_exchange_weak(current, updated, std::memory_order_release,
                                             std::memory_order_relaxed)) return;
        }
    }

    void publish() noexcept { revision_.fetch_add(1U, std::memory_order_release); }

    std::array<std::atomic<std::uint32_t>, kMidiChannelCount> gains_{};
    std::atomic<std::uint32_t> mutedChannels_{};
    std::atomic<std::uint32_t> soloedChannels_{};
    std::atomic<std::uint32_t> revision_{};
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

} // namespace OpenHDK
