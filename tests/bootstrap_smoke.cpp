// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "TestCheck.hpp"
#include "audio/FluidSynthBackend.hpp"
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
bool isLimitedPcm(std::span<const float> samples) {
  constexpr float ceilingTolerance = 1.0e-6F;
  return std::all_of(samples.begin(), samples.end(), [](float sample) {
    return std::isfinite(sample) && std::abs(sample) <= 0.98F + ceilingTolerance;
  });
}
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
  OPENHDK_FAIL_IF(36, !isLimitedPcm(compiledPcm));
  constexpr float runtimeMuteSilenceThreshold = 1.0e-6F;
  OPENHDK_FAIL_IF(28, std::any_of(compiledPcm.begin(), compiledPcm.end(), [runtimeMuteSilenceThreshold](float sample) {
        return std::abs(sample) > runtimeMuteSilenceThreshold;
      }));
  OPENHDK_FAIL_IF(29, !backend.setRuntimeChannelMuted(0U, false, status));
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(20, !backend.renderStereo(compiledPcm, status));
    OPENHDK_FAIL_IF(36, !isLimitedPcm(compiledPcm));
  }
  OPENHDK_FAIL_IF(21, backend.isPlaying());
  OPENHDK_FAIL_IF(30, !backend.playCompiledTimeline(*compiledTimeline.timeline(), status) || !backend.isPlaying());
  bool compiledHeard{};
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(20, !backend.renderStereo(compiledPcm, status));
    compiledHeard = compiledHeard || std::any_of(compiledPcm.begin(), compiledPcm.end(), [](float x) { return x > 0.0001F || x < -0.0001F; });
    OPENHDK_FAIL_IF(36, !isLimitedPcm(compiledPcm));
  }
  OPENHDK_FAIL_IF(21, backend.isPlaying() || !compiledHeard);
  OPENHDK_FAIL_IF(26, !backend.resetRuntimeMixer(status));
  OPENHDK_FAIL_IF(4, !backend.playMidiFile(midi, status));
  OPENHDK_FAIL_IF(25, backend.setRuntimeChannelMuted(0U, true, status)
      || status.error != OpenHDK::AudioBackendError::MidiPlaybackFailed);
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  std::vector<float> legacyPcm(4096U);
  OPENHDK_FAIL_IF(5, !backend.renderStereo(legacyPcm, status));
  OPENHDK_FAIL_IF(37, !isLimitedPcm(legacyPcm));
  const bool heardLegacy = std::any_of(legacyPcm.begin(), legacyPcm.end(), [](float x) { return x > 0.0001F || x < -0.0001F; });
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(31, !backend.renderStereo(legacyPcm, status));
    OPENHDK_FAIL_IF(37, !isLimitedPcm(legacyPcm));
  }
  OPENHDK_FAIL_IF(32, !heardLegacy || backend.isPlaying());
  OPENHDK_FAIL_IF(33, !backend.playCompiledTimeline(*compiledTimeline.timeline(), status) || !backend.isPlaying());
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(34, !backend.renderStereo(compiledPcm, status));
    OPENHDK_FAIL_IF(36, !isLimitedPcm(compiledPcm));
  }
  const bool completedCompiledAfterLegacy = !backend.isPlaying();
  // A dense, maximum-velocity chord stresses the shared output boundary at
  // maximum supported master gain without changing the runtime SoundFont.
  std::vector<std::uint8_t> chordTrack{0U, 0xc0U, 0U, 0U, 0xb0U, 7U, 127U};
  for (std::uint8_t note = 36U; note < 84U; ++note) {
    chordTrack.insert(chordTrack.end(), {0U, 0x90U, note, 127U});
  }
  chordTrack.insert(chordTrack.end(), {0x83U, 0x60U, 0xb0U, 120U, 0U,
                                      0U, 0xffU, 0x2fU, 0U});
  std::vector<std::uint8_t> chordMidi{
    'M','T','h','d',0,0,0,6,0,0,0,1,1,0xe0,'M','T','r','k'};
  for (const unsigned shift : {24U, 16U, 8U, 0U}) {
    chordMidi.push_back(static_cast<std::uint8_t>(chordTrack.size() >> shift));
  }
  chordMidi.insert(chordMidi.end(), chordTrack.begin(), chordTrack.end());
  const auto chordParsed = OpenHDK::SmfParser::parse(chordMidi);
  OPENHDK_FAIL_IF(38, !chordParsed.file());
  const auto chordTimeline = OpenHDK::SmfTimelineCompiler::compile(*chordParsed.file());
  OPENHDK_FAIL_IF(39, !chordTimeline.timeline() || !backend.setVolume(1.0F, status)
                    || !backend.playCompiledTimeline(*chordTimeline.timeline(), status));
  bool chordHeard{};
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(40, !backend.renderStereo(compiledPcm, status)
                      || !isLimitedPcm(compiledPcm));
    chordHeard = chordHeard || std::any_of(compiledPcm.begin(), compiledPcm.end(), [](float sample) {
      return std::abs(sample) > 0.0001F;
    });
  }
  OPENHDK_FAIL_IF(41, backend.isPlaying() || !chordHeard);
  backend.shutdown(); std::filesystem::remove(midi);
  OPENHDK_FAIL_IF(35, !completedCompiledAfterLegacy);
  return 0;
}
