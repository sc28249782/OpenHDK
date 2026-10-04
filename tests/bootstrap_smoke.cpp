// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
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
  if (reader.readVariableLength() != 128U) return 10;
  constexpr std::array<std::uint8_t, 1> incompleteDelta{0x80U};
  OpenHDK::SmfByteReader incompleteReader(incompleteDelta);
  if (incompleteReader.readVariableLength().has_value()) return 11;
  constexpr std::array<std::uint8_t, 5> overlongDelta{0x81U, 0x80U, 0x80U, 0x80U, 0x00U};
  OpenHDK::SmfByteReader overlongReader(overlongDelta);
  if (overlongReader.readVariableLength().has_value()) return 12;
  constexpr std::array<std::uint8_t, 45> compiledMidi{
    'M','T','h','d',0,0,0,6,0,0,0,1,1,0xE0, 'M','T','r','k',0,0,0,0x17,
    0,0xFF,0x51,3,7,0xA1,0x20, 0,0xC0,0, 0,0x90,0x3C,0x64,
    0x83,0x60,0x80,0x3C,0, 0,0xFF,0x2F,0};
  const auto parsedMidi = OpenHDK::SmfParser::parse(compiledMidi);
  if (!parsedMidi.file()) return 16;
  const auto compiledTimeline = OpenHDK::SmfTimelineCompiler::compile(*parsedMidi.file());
  if (!compiledTimeline.timeline()) return 17;
  OpenHDK::FluidSynthBackend backend; OpenHDK::AudioBackendStatus status;
  if (backend.info().id != "fluidsynth-miniaudio") return 1;
  if (backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false,
                          .volume = std::numeric_limits<float>::quiet_NaN()}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration) return 13;
  if (backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false,
                          .volume = std::numeric_limits<float>::infinity()}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration) return 14;
  if (backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false,
                          .volume = -std::numeric_limits<float>::infinity()}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration) return 15;
  if (backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false, .volume = 1.1F}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration) return 7;
  auto invalidChannelGains = OpenHDK::defaultMidiChannelGains();
  invalidChannelGains[0] = std::numeric_limits<float>::infinity();
  if (backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false,
                          .channelGains = invalidChannelGains}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration) return 22;
  if (backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false}, status)
      || status.error != OpenHDK::AudioBackendError::SoundFontNotFound) return 2;
  const auto midi = writeMidi();
  if (!backend.initialize({.soundFontPath = OPENHDK_TEST_SOUNDFONT_PATH, .enableDeviceOutput = false,
                           .volume = 0.0F}, status) || backend.hasActiveDevice()) return 3;
  if (!backend.setVolume(1.0F, status) || backend.volume() != 1.0F
      || !backend.setVolume(0.0F, status) || backend.volume() != 0.0F
      || !backend.setVolume(0.5F, status) || backend.volume() != 0.5F
      || !backend.setMuted(true, status) || !backend.isMuted()) return 8;
  if (!backend.setMuted(false, status) || backend.isMuted() || backend.setVolume(1.1F, status)
      || backend.setVolume(std::numeric_limits<float>::quiet_NaN(), status)
      || backend.setVolume(std::numeric_limits<float>::infinity(), status)
      || backend.setVolume(-std::numeric_limits<float>::infinity(), status)) return 9;
  if (!backend.setRuntimeChannelGain(0U, 0.5F, status)
      || !backend.setRuntimeChannelMuted(0U, false, status)
      || !backend.setRuntimeChannelSoloed(0U, true, status)
      || !backend.setRuntimeChannelSoloed(0U, false, status)
      || backend.setRuntimeChannelGain(16U, 0.5F, status)
      || backend.setRuntimeChannelGain(0U, std::numeric_limits<float>::infinity(), status)
      || backend.setRuntimeChannelMuted(16U, true, status)
      || backend.setRuntimeChannelSoloed(16U, true, status)) return 23;
  if (!backend.playCompiledTimeline(*compiledTimeline.timeline(), status) || !backend.isPlaying()) return 18;
  if (backend.playMidiFile(midi, status) || status.error != OpenHDK::AudioBackendError::MidiPlaybackFailed) return 19;
  std::array<float, 256> compiledPcm{};
  if (!backend.setRuntimeChannelMuted(0U, true, status)) return 24;
  if (!backend.renderStereo(compiledPcm, status)) return 27;
  constexpr float runtimeMuteSilenceThreshold = 1.0e-6F;
  if (std::any_of(compiledPcm.begin(), compiledPcm.end(), [runtimeMuteSilenceThreshold](float sample) {
        return std::abs(sample) > runtimeMuteSilenceThreshold;
      })) return 28;
  if (!backend.setRuntimeChannelMuted(0U, false, status)) return 29;
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    if (!backend.renderStereo(compiledPcm, status)) return 20;
  }
  if (backend.isPlaying()) return 21;
  if (!backend.playCompiledTimeline(*compiledTimeline.timeline(), status) || !backend.isPlaying()) return 30;
  bool compiledHeard{};
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    if (!backend.renderStereo(compiledPcm, status)) return 20;
    compiledHeard = compiledHeard || std::any_of(compiledPcm.begin(), compiledPcm.end(), [](float x) { return x > 0.0001F || x < -0.0001F; });
  }
  if (backend.isPlaying() || !compiledHeard) return 21;
  if (!backend.resetRuntimeMixer(status)) return 26;
  if (!backend.playMidiFile(midi, status)) return 4;
  if (backend.setRuntimeChannelMuted(0U, true, status)
      || status.error != OpenHDK::AudioBackendError::MidiPlaybackFailed) return 25;
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  std::vector<float> legacyPcm(4096U);
  if (!backend.renderStereo(legacyPcm, status)) return 5;
  const bool heardLegacy = std::any_of(legacyPcm.begin(), legacyPcm.end(), [](float x) { return x > 0.0001F || x < -0.0001F; });
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    if (!backend.renderStereo(legacyPcm, status)) return 31;
  }
  if (!heardLegacy || backend.isPlaying()) return 32;
  if (!backend.playCompiledTimeline(*compiledTimeline.timeline(), status) || !backend.isPlaying()) return 33;
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    if (!backend.renderStereo(compiledPcm, status)) return 34;
  }
  const bool completedCompiledAfterLegacy = !backend.isPlaying();
  backend.shutdown(); std::filesystem::remove(midi); return completedCompiledAfterLegacy ? 0 : 35;
}
