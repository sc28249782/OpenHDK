// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors

#include <iostream>
#include "audio/AudioBackend.hpp"

int main()
{
    constexpr OpenHDK::AudioBackendInfo placeholder{
        .id = "unconfigured",
        .displayName = "No audio backend configured",
        .capabilities = OpenHDK::AudioCapability::None,
    };

    std::cout << "OpenHDK 0.1.0-dev bootstrap\n"
              << "Audio backend: " << placeholder.displayName << "\n";
    return 0;
}
