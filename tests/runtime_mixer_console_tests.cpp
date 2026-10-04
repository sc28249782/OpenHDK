// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "TestCheck.hpp"
#include "app/RuntimeMixerConsole.hpp"

#include <sstream>

namespace {
class ConsoleBackend final : public OpenHDK::AudioBackend {
public:
  [[nodiscard]] OpenHDK::AudioBackendInfo info() const noexcept override { return {}; }
  bool initialize(const OpenHDK::AudioBackendConfig&, OpenHDK::AudioBackendStatus&) override { return false; }
  bool playMidiFile(const std::filesystem::path&, OpenHDK::AudioBackendStatus&) override { return false; }
  bool playCompiledTimeline(const OpenHDK::SmfTimeline&, OpenHDK::AudioBackendStatus&) override { return false; }
  bool renderStereo(std::span<float>, OpenHDK::AudioBackendStatus&) override { return false; }
  bool setVolume(float, OpenHDK::AudioBackendStatus&) override { return false; }
  bool setMuted(bool, OpenHDK::AudioBackendStatus&) override { return false; }
  bool setRuntimeChannelGain(std::size_t channel, float gain, OpenHDK::AudioBackendStatus& status) override {
    lastChannel = channel; lastGain = gain; ++gainCalls; status = {}; return true;
  }
  bool setRuntimeChannelMuted(std::size_t channel, bool muted, OpenHDK::AudioBackendStatus& status) override {
    lastChannel = channel; lastEnabled = muted; ++muteCalls; status = {}; return true;
  }
  bool setRuntimeChannelSoloed(std::size_t channel, bool soloed, OpenHDK::AudioBackendStatus& status) override {
    lastChannel = channel; lastEnabled = soloed; ++soloCalls; status = {}; return true;
  }
  bool resetRuntimeMixer(OpenHDK::AudioBackendStatus& status) override { ++resetCalls; status = {}; return true; }
  [[nodiscard]] float volume() const noexcept override { return 1.0F; }
  [[nodiscard]] bool isMuted() const noexcept override { return false; }
  [[nodiscard]] bool isPlaying() const noexcept override { return false; }
  [[nodiscard]] bool hasActiveDevice() const noexcept override { return false; }
  void shutdown() noexcept override {}

  std::size_t lastChannel{};
  float lastGain{};
  bool lastEnabled{};
  int gainCalls{};
  int muteCalls{};
  int soloCalls{};
  int resetCalls{};
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
  return 0;
}
