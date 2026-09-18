// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors

#include "audio/AudioBackend.hpp"

int main()
{
    constexpr OpenHDK::AudioBackendInfo backend{
        .id = "bootstrap",
        .displayName = "Bootstrap backend",
        .capabilities = OpenHDK::AudioCapability::MidiSynthesis
            | OpenHDK::AudioCapability::DeviceOutput,
    };
    return backend.id.empty() || backend.displayName.empty() ? 1 : 0;
}
