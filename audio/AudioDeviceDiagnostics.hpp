// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once
#include "audio/AudioBackend.hpp"
#include <string>
#include <vector>
namespace OpenHDK {
struct AudioOutputDeviceInfo { std::string name; bool isDefault{false}; };
// Does not open a stream. Device selection itself is deferred to 0.2.
std::vector<AudioOutputDeviceInfo> enumerateAudioOutputDevices(AudioBackendStatus& status);
} // namespace OpenHDK
