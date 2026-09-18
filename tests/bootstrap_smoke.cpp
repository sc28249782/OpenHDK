// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "audio/FluidSynthBackend.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
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
  OpenHDK::FluidSynthBackend backend; OpenHDK::AudioBackendStatus status;
  if (backend.info().id != "fluidsynth-miniaudio") return 1;
  if (backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false}, status)
      || status.error != OpenHDK::AudioBackendError::SoundFontNotFound) return 2;
  const auto midi = writeMidi();
  if (!backend.initialize({.soundFontPath = OPENHDK_TEST_SOUNDFONT_PATH, .enableDeviceOutput = false}, status) || backend.hasActiveDevice()) return 3;
  if (!backend.playMidiFile(midi, status)) return 4;
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  std::vector<float> pcm(4096U);
  if (!backend.renderStereo(pcm, status)) return 5;
  const bool heard = std::any_of(pcm.begin(), pcm.end(), [](float x) { return x > 0.0001F || x < -0.0001F; });
  backend.shutdown(); std::filesystem::remove(midi); return heard ? 0 : 6;
}
