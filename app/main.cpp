// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "audio/FluidSynthBackend.hpp"
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
namespace { void usage() { std::cout << "Usage: OpenHDK --midi <file.mid> --soundfont <file.sf2> [--no-device]\n"; } }
int main(int argc, char* argv[]) {
  std::string midi, sf2; bool device = true;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") { usage(); return 0; }
    if (arg == "--no-device") { device = false; continue; }
    if ((arg == "--midi" || arg == "--soundfont") && i + 1 < argc) { (arg == "--midi" ? midi : sf2) = argv[++i]; continue; }
    std::cerr << "Unknown or incomplete argument: " << arg << "\n"; usage(); return 64;
  }
  if (midi.empty() || sf2.empty()) { usage(); return 64; }
  OpenHDK::FluidSynthBackend backend; OpenHDK::AudioBackendStatus status;
  if (!backend.initialize({.soundFontPath = sf2, .enableDeviceOutput = device}, status)) {
    std::cerr << "Audio initialization failed: " << status.message << "\nTip: use --no-device on a headless machine.\n"; return 1;
  }
  if (!backend.playMidiFile(midi, status)) { std::cerr << "MIDI playback failed: " << status.message << "\n"; return 1; }
  std::cout << "OpenHDK 0.1.0 PoC — " << backend.info().displayName << "\n";
  if (!backend.hasActiveDevice()) {
    std::vector<float> pcm(44100U * 2U);
    if (!backend.renderStereo(pcm, status)) { std::cerr << "Headless PCM render failed: " << status.message << "\n"; return 1; }
    std::cout << "Headless render completed (one second of stereo PCM).\n"; return 0;
  }
  std::cout << "Playing MIDI. Press Ctrl+C to stop.\n";
  while (backend.isPlaying()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
