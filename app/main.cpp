// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "audio/AudioDeviceDiagnostics.hpp"
#include "audio/FluidSynthBackend.hpp"
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
namespace {
void usage() {
  std::cout << "Usage: OpenHDK --midi <file.mid> --soundfont <file.sf2> [--no-device]\n"
            << "       OpenHDK --list-devices\n";
}
}
int main(int argc, char* argv[]) {
  std::string midi, soundFont; bool deviceOutput = true, listDevices = false;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--help" || argument == "-h") { usage(); return 0; }
    if (argument == "--list-devices") { listDevices = true; continue; }
    if (argument == "--no-device") { deviceOutput = false; continue; }
    if ((argument == "--midi" || argument == "--soundfont") && i + 1 < argc) { (argument == "--midi" ? midi : soundFont) = argv[++i]; continue; }
    std::cerr << "Unknown or incomplete argument: " << argument << "\n"; usage(); return 64;
  }
  OpenHDK::AudioBackendStatus status;
  if (listDevices) {
    const auto devices = OpenHDK::enumerateAudioOutputDevices(status);
    if (!status) { std::cerr << status.message << "\n"; return 1; }
    std::cout << "Playback devices (" << devices.size() << "):\n";
    for (const auto& device : devices) std::cout << (device.isDefault ? "* " : "  ") << device.name << "\n";
    return 0;
  }
  if (midi.empty() || soundFont.empty()) { usage(); return 64; }
  OpenHDK::FluidSynthBackend backend;
  if (!backend.initialize({.soundFontPath = soundFont, .enableDeviceOutput = deviceOutput}, status)) {
    std::cerr << "Audio initialization failed: " << status.message << "\nTip: use --list-devices or --no-device on a headless machine.\n"; return 1;
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
