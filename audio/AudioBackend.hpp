// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once
#include "audio/MidiChannelMix.hpp"
#include "audio/MediaClockPublication.hpp"
#include "audio/MidiRuntimeMixer.hpp"
#include "audio/MidiVelocityCurve.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace OpenHDK {
class SmfTimeline;
enum class AudioCapability : std::uint32_t { None = 0, MidiSynthesis = 1U << 0U, DeviceOutput = 1U << 1U, Mixing = 1U << 2U, TempoPitch = 1U << 3U, PluginHosting = 1U << 4U };
constexpr AudioCapability operator|(AudioCapability left, AudioCapability right) noexcept { return static_cast<AudioCapability>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right)); }
struct AudioBackendInfo { std::string_view id; std::string_view displayName; AudioCapability capabilities; };
enum class AudioBackendError { None, InvalidConfiguration, SoundFontNotFound, SoundFontLoadFailed, MidiFileNotFound, MidiPlaybackFailed, AudioDeviceUnavailable, AudioDeviceNotFound, RenderFailed, PlaybackObservationFailed };
struct AudioBackendStatus { AudioBackendError error{AudioBackendError::None}; std::string message{}; [[nodiscard]] explicit operator bool() const noexcept { return error == AudioBackendError::None; } };
struct AudioBackendPlaybackStart { std::uint64_t generation{}; };
struct AudioBackendConfig { std::filesystem::path soundFontPath; std::uint32_t sampleRate{44100}; bool enableDeviceOutput{true}; std::optional<std::uint32_t> outputDeviceIndex{}; float volume{1.0F}; bool muted{false}; MidiChannelGains channelGains{defaultMidiChannelGains()}; MidiVelocityCurve velocityCurve{MidiVelocityCurve::Linear}; };
[[nodiscard]] inline bool isNormalizedVolume(float value) noexcept {
  return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}
class AudioBackend {
public:
  virtual ~AudioBackend() = default;
  [[nodiscard]] virtual AudioBackendInfo info() const noexcept = 0;
  virtual bool initialize(const AudioBackendConfig&, AudioBackendStatus&) = 0;
  virtual bool playMidiFile(const std::filesystem::path&, AudioBackendStatus&) = 0;
  bool playCompiledTimeline(const SmfTimeline& timeline, AudioBackendStatus& status) {
    AudioBackendPlaybackStart ignored;
    return playCompiledTimeline(timeline, ignored, status);
  }
  virtual bool playCompiledTimeline(const SmfTimeline&, AudioBackendPlaybackStart&, AudioBackendStatus&) = 0;
  virtual bool stopPlayback(AudioBackendStatus&) = 0;
  virtual bool renderStereo(std::span<float>, AudioBackendStatus&) = 0;
  virtual bool setVolume(float volume, AudioBackendStatus&) = 0;
  virtual bool setMuted(bool muted, AudioBackendStatus&) = 0;
  virtual bool setRuntimeChannelGain(std::size_t channel, float gain, AudioBackendStatus&) = 0;
  virtual bool setRuntimeChannelMuted(std::size_t channel, bool muted, AudioBackendStatus&) = 0;
  virtual bool setRuntimeChannelSoloed(std::size_t channel, bool soloed, AudioBackendStatus&) = 0;
  virtual bool resetRuntimeMixer(AudioBackendStatus&) = 0;
  virtual bool saveRuntimeMixerPreset(std::string_view name, AudioBackendStatus&) = 0;
  virtual bool recallRuntimeMixerPreset(std::string_view name, AudioBackendStatus&) = 0;
  virtual bool deleteRuntimeMixerPreset(std::string_view name, AudioBackendStatus&) = 0;
  virtual bool listRuntimeMixerPresets(std::vector<std::string>& names, AudioBackendStatus&) = 0;
  [[nodiscard]] virtual float volume() const noexcept = 0;
  [[nodiscard]] virtual bool isMuted() const noexcept = 0;
  [[nodiscard]] virtual bool isPlaying() const noexcept = 0;
  [[nodiscard]] virtual bool hasActiveDevice() const noexcept = 0;
  [[nodiscard]] virtual MediaClockReadResult mediaClock() const noexcept = 0;
  virtual void shutdown() noexcept = 0;
};
} // namespace OpenHDK
