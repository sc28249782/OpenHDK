// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
namespace OpenHDK {
enum class AudioCapability : std::uint32_t { None = 0, MidiSynthesis = 1U << 0U, DeviceOutput = 1U << 1U, Mixing = 1U << 2U, TempoPitch = 1U << 3U, PluginHosting = 1U << 4U };
constexpr AudioCapability operator|(AudioCapability left, AudioCapability right) noexcept { return static_cast<AudioCapability>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right)); }
struct AudioBackendInfo { std::string_view id; std::string_view displayName; AudioCapability capabilities; };
enum class AudioBackendError { None, InvalidConfiguration, SoundFontNotFound, SoundFontLoadFailed, MidiFileNotFound, MidiPlaybackFailed, AudioDeviceUnavailable, AudioDeviceNotFound, RenderFailed };
struct AudioBackendStatus { AudioBackendError error{AudioBackendError::None}; std::string message{}; [[nodiscard]] explicit operator bool() const noexcept { return error == AudioBackendError::None; } };
struct AudioBackendConfig { std::filesystem::path soundFontPath; std::uint32_t sampleRate{44100}; bool enableDeviceOutput{true}; std::optional<std::uint32_t> outputDeviceIndex{}; };
class AudioBackend {
public:
  virtual ~AudioBackend() = default;
  [[nodiscard]] virtual AudioBackendInfo info() const noexcept = 0;
  virtual bool initialize(const AudioBackendConfig&, AudioBackendStatus&) = 0;
  virtual bool playMidiFile(const std::filesystem::path&, AudioBackendStatus&) = 0;
  virtual bool renderStereo(std::span<float>, AudioBackendStatus&) = 0;
  [[nodiscard]] virtual bool isPlaying() const noexcept = 0;
  [[nodiscard]] virtual bool hasActiveDevice() const noexcept = 0;
  virtual void shutdown() noexcept = 0;
};
} // namespace OpenHDK
