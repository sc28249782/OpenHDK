// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "audio/AudioDeviceDiagnostics.hpp"
#include <miniaudio.h>
namespace OpenHDK {
std::vector<AudioOutputDeviceInfo> enumerateAudioOutputDevices(AudioBackendStatus& status) {
  ma_context context{};
  const auto start = ma_context_init(nullptr, 0, nullptr, &context);
  if (start != MA_SUCCESS) {
    status = {.error = AudioBackendError::AudioDeviceUnavailable, .message = "Audio device enumeration could not start: " + std::string(ma_result_description(start))};
    return {};
  }
  ma_device_info* output{}; ma_uint32 count{};
  const auto result = ma_context_get_devices(&context, &output, &count, nullptr, nullptr);
  if (result != MA_SUCCESS) {
    ma_context_uninit(&context);
    status = {.error = AudioBackendError::AudioDeviceUnavailable, .message = "Audio device enumeration failed: " + std::string(ma_result_description(result))};
    return {};
  }
  std::vector<AudioOutputDeviceInfo> devices; devices.reserve(count);
  for (ma_uint32 i = 0; i < count; ++i)
    devices.push_back({.name = output[i].name, .isDefault = output[i].isDefault == MA_TRUE});
  ma_context_uninit(&context);
  status = {};
  return devices;
}
} // namespace OpenHDK
