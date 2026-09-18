// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include <cstdint>
#include <string_view>

namespace OpenHDK {

enum class AudioCapability : std::uint32_t {
    None = 0,
    MidiSynthesis = 1U << 0U,
    DeviceOutput = 1U << 1U,
    Mixing = 1U << 2U,
    TempoPitch = 1U << 3U,
    PluginHosting = 1U << 4U,
};

constexpr AudioCapability operator|(AudioCapability left, AudioCapability right) noexcept
{
    return static_cast<AudioCapability>(
        static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

struct AudioBackendInfo {
    std::string_view id;
    std::string_view displayName;
    AudioCapability capabilities;
};

class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    [[nodiscard]] virtual AudioBackendInfo info() const noexcept = 0;
    virtual bool initialize() = 0;
    virtual void shutdown() noexcept = 0;
};

} // namespace OpenHDK
