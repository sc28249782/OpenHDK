// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "TestCheck.hpp"
#include "audio/FluidSynthBackend.hpp"
#include "audio/SmfByteReader.hpp"
#include "audio/SmfParser.hpp"
#include "audio/SmfTimelineCompiler.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>
#include <vector>
namespace {
std::filesystem::path writeMidi() {
  constexpr std::array<unsigned char, 45> bytes{
    'M','T','h','d',0,0,0,6,0,0,0,1,1,0xE0, 'M','T','r','k',0,0,0,0x17,
    0,0xFF,0x51,3,7,0xA1,0x20, 0,0xC0,0, 0,0x90,0x3C,0x64,
    0x83,0x60,0x80,0x3C,0, 0,0xFF,0x2F,0};
  const auto path = std::filesystem::temp_directory_path() / "openhdk-poc-middle-c.mid";
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return path;
}
}
int main() {
  constexpr std::array<std::uint8_t, 2> delta{0x81U, 0x00U};
  OpenHDK::SmfByteReader reader(delta);
  OPENHDK_FAIL_IF(10, reader.readVariableLength() != 128U);
  constexpr std::array<std::uint8_t, 1> incompleteDelta{0x80U};
  OpenHDK::SmfByteReader incompleteReader(incompleteDelta);
  OPENHDK_FAIL_IF(11, incompleteReader.readVariableLength().has_value());
  constexpr std::array<std::uint8_t, 5> overlongDelta{0x81U, 0x80U, 0x80U, 0x80U, 0x00U};
  OpenHDK::SmfByteReader overlongReader(overlongDelta);
  OPENHDK_FAIL_IF(12, overlongReader.readVariableLength().has_value());
  constexpr std::array<std::uint8_t, 45> compiledMidi{
    'M','T','h','d',0,0,0,6,0,0,0,1,1,0xE0, 'M','T','r','k',0,0,0,0x17,
    0,0xFF,0x51,3,7,0xA1,0x20, 0,0xC0,0, 0,0x90,0x3C,0x64,
    0x83,0x60,0x80,0x3C,0, 0,0xFF,0x2F,0};
  const auto parsedMidi = OpenHDK::SmfParser::parse(compiledMidi);
  OPENHDK_FAIL_IF(16, !parsedMidi.file());
  const auto compiledTimeline = OpenHDK::SmfTimelineCompiler::compile(*parsedMidi.file());
  OPENHDK_FAIL_IF(17, !compiledTimeline.timeline());
  OpenHDK::FluidSynthBackend backend; OpenHDK::AudioBackendStatus status;
  OPENHDK_FAIL_IF(1, backend.info().id != "fluidsynth-miniaudio");
  OPENHDK_FAIL_IF(13, backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false,
                          .volume = std::numeric_limits<float>::quiet_NaN()}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration);
  OPENHDK_FAIL_IF(14, backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false,
                          .volume = std::numeric_limits<float>::infinity()}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration);
  OPENHDK_FAIL_IF(15, backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false,
                          .volume = -std::numeric_limits<float>::infinity()}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration);
  OPENHDK_FAIL_IF(7, backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false, .volume = 1.1F}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration);
  auto invalidChannelGains = OpenHDK::defaultMidiChannelGains();
  invalidChannelGains[0] = std::numeric_limits<float>::infinity();
  OPENHDK_FAIL_IF(22, backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false,
                          .channelGains = invalidChannelGains}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration);
  OPENHDK_FAIL_IF(2, backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false}, status)
      || status.error != OpenHDK::AudioBackendError::SoundFontNotFound);
  const auto midi = writeMidi();
  OPENHDK_FAIL_IF(3, !backend.initialize({.soundFontPath = OPENHDK_TEST_SOUNDFONT_PATH, .enableDeviceOutput = false,
                           .volume = 0.0F}, status) || backend.hasActiveDevice());
  OPENHDK_FAIL_IF(8, !backend.setVolume(1.0F, status) || backend.volume() != 1.0F
      || !backend.setVolume(0.0F, status) || backend.volume() != 0.0F
      || !backend.setVolume(0.5F, status) || backend.volume() != 0.5F
      || !backend.setMuted(true, status) || !backend.isMuted());
  OPENHDK_FAIL_IF(9, !backend.setMuted(false, status) || backend.isMuted() || backend.setVolume(1.1F, status)
      || backend.setVolume(std::numeric_limits<float>::quiet_NaN(), status)
      || backend.setVolume(std::numeric_limits<float>::infinity(), status)
      || backend.setVolume(-std::numeric_limits<float>::infinity(), status));
  OPENHDK_FAIL_IF(23, !backend.setRuntimeChannelGain(0U, 0.5F, status)
      || !backend.setRuntimeChannelMuted(0U, false, status)
      || !backend.setRuntimeChannelSoloed(0U, true, status)
      || !backend.setRuntimeChannelSoloed(0U, false, status)
      || backend.setRuntimeChannelGain(16U, 0.5F, status)
      || backend.setRuntimeChannelGain(0U, std::numeric_limits<float>::infinity(), status)
      || backend.setRuntimeChannelMuted(16U, true, status)
      || backend.setRuntimeChannelSoloed(16U, true, status));
  OPENHDK_FAIL_IF(18, !backend.playCompiledTimeline(*compiledTimeline.timeline(), status) || !backend.isPlaying());
  OPENHDK_FAIL_IF(19, backend.playMidiFile(midi, status) || status.error != OpenHDK::AudioBackendError::MidiPlaybackFailed);
  std::array<float, 256> compiledPcm{};
  OPENHDK_FAIL_IF(24, !backend.setRuntimeChannelMuted(0U, true, status));
  OPENHDK_FAIL_IF(27, !backend.renderStereo(compiledPcm, status));
  constexpr float runtimeMuteSilenceThreshold = 1.0e-6F;
  OPENHDK_FAIL_IF(28, std::any_of(compiledPcm.begin(), compiledPcm.end(), [runtimeMuteSilenceThreshold](float sample) {
        return std::abs(sample) > runtimeMuteSilenceThreshold;
      }));
  OPENHDK_FAIL_IF(29, !backend.setRuntimeChannelMuted(0U, false, status));
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(20, !backend.renderStereo(compiledPcm, status));
  }
  OPENHDK_FAIL_IF(21, backend.isPlaying());
  OPENHDK_FAIL_IF(30, !backend.playCompiledTimeline(*compiledTimeline.timeline(), status) || !backend.isPlaying());
  bool compiledHeard{};
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(20, !backend.renderStereo(compiledPcm, status));
    compiledHeard = compiledHeard || std::any_of(compiledPcm.begin(), compiledPcm.end(), [](float x) { return x > 0.0001F || x < -0.0001F; });
  }
  OPENHDK_FAIL_IF(21, backend.isPlaying() || !compiledHeard);
  OPENHDK_FAIL_IF(26, !backend.resetRuntimeMixer(status));
  OPENHDK_FAIL_IF(4, !backend.playMidiFile(midi, status));
  OPENHDK_FAIL_IF(25, backend.setRuntimeChannelMuted(0U, true, status)
      || status.error != OpenHDK::AudioBackendError::MidiPlaybackFailed);
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  std::vector<float> legacyPcm(4096U);
  OPENHDK_FAIL_IF(5, !backend.renderStereo(legacyPcm, status));
  const bool heardLegacy = std::any_of(legacyPcm.begin(), legacyPcm.end(), [](float x) { return x > 0.0001F || x < -0.0001F; });
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(31, !backend.renderStereo(legacyPcm, status));
  }
  OPENHDK_FAIL_IF(32, !heardLegacy || backend.isPlaying());
  OPENHDK_FAIL_IF(33, !backend.playCompiledTimeline(*compiledTimeline.timeline(), status) || !backend.isPlaying());
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(34, !backend.renderStereo(compiledPcm, status));
  }
  const bool completedCompiledAfterLegacy = !backend.isPlaying();
  backend.shutdown(); std::filesystem::remove(midi);
  OPENHDK_FAIL_IF(35, !completedCompiledAfterLegacy);
  return 0;
}
