// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once
// Hardware-free AudioBackend test double shared by the playback controller and
// audio-session tests. It implements the existing acknowledgement interface,
// records call counts, returns configurable acknowledgement generations, and
// exposes hooks so tests can observe controller ordering at the moment backend
// calls begin. Publication payloads are driven from the test side through the
// real MediaClockPublicationCell, like a backend callback would.
#include "audio/AudioBackend.hpp"
#include "audio/MediaClockPublication.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace OpenHDK::Test {

class PlaybackTestBackend final : public AudioBackend {
public:
  MediaClockPublicationCell cell;
  bool failNextStart{false};
  bool throwNextStart{false};
  bool failNextInitialize{false};
  bool forceUnstable{false};
  bool forceExhausted{false};
  bool failNextStop{false};
  bool throwNextStop{false};
  std::uint64_t nextGeneration{1U};
  std::size_t startCount{0U};
  std::size_t stopCount{0U};
  std::size_t shutdownCount{0U};
  bool playing{false};
  std::function<void()> startHook; // Runs at the top of a backend start.
  std::function<void()> stopHook;  // Runs at the top of a backend stop.
  [[nodiscard]] AudioBackendInfo info() const noexcept override {
    return {"fake", "Fake backend", AudioCapability::MidiSynthesis};
  }
  bool initialize(const AudioBackendConfig&, AudioBackendStatus& status) override {
    if (failNextInitialize) {
      failNextInitialize = false;
      status.error = AudioBackendError::InvalidConfiguration;
      status.message = "rejected initialize"; return false;
    }
    status = AudioBackendStatus{}; return true;
  }
  bool playMidiFile(const std::filesystem::path&, AudioBackendStatus& status) override {
    status.error = AudioBackendError::MidiPlaybackFailed;
    status.message = "not supported by the fake"; return false;
  }
  bool playCompiledTimeline(const SmfTimeline&, AudioBackendPlaybackStart& start,
                            AudioBackendStatus& status) override {
    ++startCount;
    if (startHook) startHook();
    if (throwNextStart) {
      throwNextStart = false;
      throw std::runtime_error("start explosion");
    }
    if (failNextStart) {
      failNextStart = false;
      status.error = AudioBackendError::MidiPlaybackFailed;
      status.message = "rejected start"; return false;
    }
    start.generation = nextGeneration++;
    lastStarted = start.generation;
    playing = true;
    status = AudioBackendStatus{}; return true;
  }
  bool stopPlayback(AudioBackendStatus& status) override {
    ++stopCount;
    if (stopHook) stopHook();
    if (throwNextStop) {
      throwNextStop = false;
      throw std::runtime_error("stop explosion");
    }
    if (failNextStop) {
      failNextStop = false;
      status.error = AudioBackendError::MidiPlaybackFailed;
      status.message = "rejected stop"; return false;
    }
    playing = false;
    MediaClockPayload stopped;
    stopped.generation = lastStarted;
    stopped.source = MediaClockSource::CompiledTimeline;
    stopped.phase = MediaClockPhase::Stopped;
    static_cast<void>(cell.publish(stopped));
    status = AudioBackendStatus{}; return true;
  }
  bool renderStereo(std::span<float>, AudioBackendStatus& status) override {
    status.error = AudioBackendError::RenderFailed;
    status.message = "not supported by the fake"; return false;
  }
  bool setVolume(float, AudioBackendStatus& status) override { status = AudioBackendStatus{}; return true; }
  bool setMuted(bool, AudioBackendStatus& status) override { status = AudioBackendStatus{}; return true; }
  bool setRuntimeChannelGain(std::size_t, float, AudioBackendStatus& status) override { status = AudioBackendStatus{}; return true; }
  bool setRuntimeChannelMuted(std::size_t, bool, AudioBackendStatus& status) override { status = AudioBackendStatus{}; return true; }
  bool setRuntimeChannelSoloed(std::size_t, bool, AudioBackendStatus& status) override { status = AudioBackendStatus{}; return true; }
  bool resetRuntimeMixer(AudioBackendStatus& status) override { status = AudioBackendStatus{}; return true; }
  bool saveRuntimeMixerPreset(std::string_view, AudioBackendStatus& status) override { status = AudioBackendStatus{}; return true; }
  bool recallRuntimeMixerPreset(std::string_view, AudioBackendStatus& status) override { status = AudioBackendStatus{}; return true; }
  bool deleteRuntimeMixerPreset(std::string_view, AudioBackendStatus& status) override { status = AudioBackendStatus{}; return true; }
  bool listRuntimeMixerPresets(std::vector<std::string>&, AudioBackendStatus& status) override { status = AudioBackendStatus{}; return true; }
  [[nodiscard]] float volume() const noexcept override { return 1.0F; }
  [[nodiscard]] bool isMuted() const noexcept override { return false; }
  [[nodiscard]] bool isPlaying() const noexcept override { return playing; }
  [[nodiscard]] bool hasActiveDevice() const noexcept override { return false; }
  [[nodiscard]] MediaClockReadResult mediaClock() const noexcept override {
    if (forceExhausted) return {MediaClockReadStatus::Exhausted, std::nullopt};
    if (forceUnstable) return {MediaClockReadStatus::Unstable, std::nullopt};
    return cell.tryRead();
  }
  void shutdown() noexcept override { ++shutdownCount; playing = false; }
  // Backend-callback emulation: publish one committed clock payload.
  bool publish(std::uint64_t generation, std::uint64_t microseconds,
               MediaClockPhase phase = MediaClockPhase::Playing) {
    MediaClockPayload payload;
    payload.generation = generation;
    payload.mediaMicroseconds = microseconds;
    payload.source = MediaClockSource::CompiledTimeline;
    payload.phase = phase;
    return cell.publish(payload) == MediaClockPublishStatus::Published;
  }
  std::uint64_t lastStarted{0U};
};

} // namespace OpenHDK::Test