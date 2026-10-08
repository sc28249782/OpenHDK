// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "TestCheck.hpp"
#include "app/RuntimeMixerConsole.hpp"
#include "audio/MidiMixerPresets.hpp"

#include <sstream>

namespace {
class ConsoleBackend final : public OpenHDK::AudioBackend {
public:
  [[nodiscard]] OpenHDK::AudioBackendInfo info() const noexcept override { return {}; }
  bool initialize(const OpenHDK::AudioBackendConfig&, OpenHDK::AudioBackendStatus&) override { return false; }
  bool playMidiFile(const std::filesystem::path&, OpenHDK::AudioBackendStatus&) override { return false; }
  bool playCompiledTimeline(const OpenHDK::SmfTimeline&, OpenHDK::AudioBackendPlaybackStart&,
                            OpenHDK::AudioBackendStatus&) override { return false; }
  bool stopPlayback(OpenHDK::AudioBackendStatus&) override { return false; }
  bool renderStereo(std::span<float>, OpenHDK::AudioBackendStatus&) override { return false; }
  bool setVolume(float, OpenHDK::AudioBackendStatus&) override { return false; }
  bool setMuted(bool, OpenHDK::AudioBackendStatus&) override { return false; }
  bool setRuntimeChannelGain(std::size_t channel, float gain, OpenHDK::AudioBackendStatus& status) override {
    lastChannel = channel; lastGain = gain; ++gainCalls; status = {}; return mixer.setGain(channel, gain);
  }
  bool setRuntimeChannelMuted(std::size_t channel, bool muted, OpenHDK::AudioBackendStatus& status) override {
    lastChannel = channel; lastEnabled = muted; ++muteCalls; status = {}; return mixer.setMuted(channel, muted);
  }
  bool setRuntimeChannelSoloed(std::size_t channel, bool soloed, OpenHDK::AudioBackendStatus& status) override {
    lastChannel = channel; lastEnabled = soloed; ++soloCalls; status = {}; return mixer.setSoloed(channel, soloed);
  }
  bool resetRuntimeMixer(OpenHDK::AudioBackendStatus& status) override { ++resetCalls; mixer.reset(); status = {}; return true; }
  bool presetResult(OpenHDK::MidiMixerPresetResult result, OpenHDK::AudioBackendStatus& status) {
    status = {};
    if (result == OpenHDK::MidiMixerPresetResult::Success) return true;
    status.error = OpenHDK::AudioBackendError::InvalidConfiguration;
    status.message = "Invalid or missing preset.";
    return false;
  }
  bool saveRuntimeMixerPreset(std::string_view name, OpenHDK::AudioBackendStatus& status) override {
    ++presetCalls; return presetResult(presets.save(name, mixer), status);
  }
  bool recallRuntimeMixerPreset(std::string_view name, OpenHDK::AudioBackendStatus& status) override {
    ++presetCalls; return presetResult(presets.recall(name, mixer), status);
  }
  bool deleteRuntimeMixerPreset(std::string_view name, OpenHDK::AudioBackendStatus& status) override {
    ++presetCalls; return presetResult(presets.erase(name), status);
  }
  bool listRuntimeMixerPresets(std::vector<std::string>& names, OpenHDK::AudioBackendStatus& status) override {
    ++presetCalls; names = presets.names(); status = {}; return true;
  }
  [[nodiscard]] float volume() const noexcept override { return 1.0F; }
  [[nodiscard]] bool isMuted() const noexcept override { return false; }
  [[nodiscard]] bool isPlaying() const noexcept override { return false; }
  [[nodiscard]] bool hasActiveDevice() const noexcept override { return false; }
  [[nodiscard]] OpenHDK::MediaClockReadResult mediaClock() const noexcept override { return {}; }
  void shutdown() noexcept override {}

  std::size_t lastChannel{};
  float lastGain{};
  bool lastEnabled{};
  int gainCalls{};
  int muteCalls{};
  int soloCalls{};
  int resetCalls{};
  int presetCalls{};
  OpenHDK::MidiRuntimeMixer mixer;
  OpenHDK::MidiMixerPresets presets;
};

OpenHDK::RuntimeMixerConsoleResult run(std::string_view command, ConsoleBackend& backend,
                                       std::ostringstream& output, std::ostringstream& error) {
  OpenHDK::AudioBackendStatus status;
  return OpenHDK::runRuntimeMixerConsoleCommand(command, backend, status, output, error);
}
}

int main() {
  ConsoleBackend backend;
  std::ostringstream output;
  std::ostringstream error;
  OPENHDK_FAIL_IF(1, run("gain 10 50", backend, output, error) != OpenHDK::RuntimeMixerConsoleResult::Continue
      || backend.gainCalls != 1 || backend.lastChannel != 9U || backend.lastGain != 0.5F);
  OPENHDK_FAIL_IF(2, run("mute 10", backend, output, error) != OpenHDK::RuntimeMixerConsoleResult::Continue
      || backend.muteCalls != 1 || backend.lastChannel != 9U || !backend.lastEnabled);
  OPENHDK_FAIL_IF(3, run("unmute 10", backend, output, error) != OpenHDK::RuntimeMixerConsoleResult::Continue
      || backend.muteCalls != 2 || backend.lastEnabled);
  OPENHDK_FAIL_IF(4, run("solo 10", backend, output, error) != OpenHDK::RuntimeMixerConsoleResult::Continue
      || backend.soloCalls != 1 || !backend.lastEnabled);
  OPENHDK_FAIL_IF(5, run("unsolo 10", backend, output, error) != OpenHDK::RuntimeMixerConsoleResult::Continue
      || backend.soloCalls != 2 || backend.lastEnabled);
  OPENHDK_FAIL_IF(6, run("reset", backend, output, error) != OpenHDK::RuntimeMixerConsoleResult::Continue
      || backend.resetCalls != 1);
  OPENHDK_FAIL_IF(7, run("gain 17 50", backend, output, error) != OpenHDK::RuntimeMixerConsoleResult::Continue
      || backend.gainCalls != 1 || error.str().find("Channel must be") == std::string::npos);
  OPENHDK_FAIL_IF(8, run("gain 10 101", backend, output, error) != OpenHDK::RuntimeMixerConsoleResult::Continue
      || backend.gainCalls != 1 || error.str().find("Gain must be") == std::string::npos);
  OPENHDK_FAIL_IF(9, run("help", backend, output, error) != OpenHDK::RuntimeMixerConsoleResult::Continue
      || output.str().find("Mixer commands:") == std::string::npos);
  OPENHDK_FAIL_IF(10, run("quit", backend, output, error) != OpenHDK::RuntimeMixerConsoleResult::Quit);
  run("mute 10", backend, output, error);
  run("solo 10", backend, output, error);
  run("preset-save Drums", backend, output, error);
  run("reset", backend, output, error);
  run("gain 10 35", backend, output, error);
  const auto beforeRecall = backend.mixer.snapshot();
  run("preset-recall Drums", backend, output, error);
  const auto recalled = backend.mixer.snapshot();
  OPENHDK_FAIL_IF(11, recalled.mutedChannels != 0x200U || recalled.soloedChannels != 0x200U
                     || recalled.gains != beforeRecall.gains || recalled.revision != beforeRecall.revision + 1U);
  run("unmute 10", backend, output, error);
  run("preset-save Drums", backend, output, error);
  run("preset-recall Drums", backend, output, error);
  OPENHDK_FAIL_IF(12, backend.mixer.snapshot().mutedChannels != 0U);
  run("preset-save Zebra", backend, output, error);
  run("preset-save Alpha", backend, output, error);
  output.str("");
  run("preset-list", backend, output, error);
  OPENHDK_FAIL_IF(13, output.str() != "Mixer presets (3):\nAlpha\nDrums\nZebra\n");
  const auto calls = backend.presetCalls;
  for (const auto command : {"preset-save", "preset-recall Drums extra", "preset-delete", "preset-list extra"}) {
    run(command, backend, output, error);
  }
  OPENHDK_FAIL_IF(14, backend.presetCalls != calls);
  const auto beforeInvalid = backend.mixer.snapshot();
  run("preset-save 1bad", backend, output, error);
  run("preset-recall Missing", backend, output, error);
  run("preset-delete Missing", backend, output, error);
  OPENHDK_FAIL_IF(15, backend.mixer.revision() != beforeInvalid.revision
                     || error.str().find("Preset command failed:") == std::string::npos);
  run("preset-delete Drums", backend, output, error);
  run("preset-recall Drums", backend, output, error);
  OPENHDK_FAIL_IF(16, backend.presets.names() != std::vector<std::string>{"Alpha", "Zebra"}
                     || backend.mixer.revision() != beforeInvalid.revision);
  return 0;
}
