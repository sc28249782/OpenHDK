// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "app/MidiDiagnosticsCli.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main() {
    constexpr std::array<std::uint8_t, 60> fixture{
        'M','T','h','d', 0,0,0,6, 0,1, 0,2, 0,96,
        'M','T','r','k', 0,0,0,15,
        0, 0xc0, 2,
        0, 0x90, 60, 100,
        0, 0xb0, 7, 80,
        0, 0xff, 0x2f, 0,
        'M','T','r','k', 0,0,0,15,
        0, 0xc0, 40,
        0, 0xb0, 7, 64,
        0, 0x99, 36, 127,
        0, 0xff, 0x2f, 0};

    const auto path = std::filesystem::current_path() / "midi-diagnostics-cli-success.mid";
    std::error_code error;
    std::filesystem::remove(path, error);
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(fixture.data()),
                     static_cast<std::streamsize>(fixture.size()));
        if (!output) return 1;
    }

    std::ostringstream standardOutput;
    std::ostringstream standardError;
    const auto exitCode = OpenHDK::runMidiDiagnostics(path, standardOutput, standardError);
    std::filesystem::remove(path, error);
    if (exitCode != 0 || !standardError.str().empty()) return 2;

    const std::string expected =
        "MIDI diagnostics: SMF format 1, tracks 2\n"
        "Channel 1: note-ons=1; programs=3,41; final-cc7=64\n"
        "Channel 2: note-ons=0; programs=unavailable; final-cc7=unavailable\n"
        "Channel 3: note-ons=0; programs=unavailable; final-cc7=unavailable\n"
        "Channel 4: note-ons=0; programs=unavailable; final-cc7=unavailable\n"
        "Channel 5: note-ons=0; programs=unavailable; final-cc7=unavailable\n"
        "Channel 6: note-ons=0; programs=unavailable; final-cc7=unavailable\n"
        "Channel 7: note-ons=0; programs=unavailable; final-cc7=unavailable\n"
        "Channel 8: note-ons=0; programs=unavailable; final-cc7=unavailable\n"
        "Channel 9: note-ons=0; programs=unavailable; final-cc7=unavailable\n"
        "Channel 10: note-ons=1; programs=unavailable; final-cc7=unavailable\n"
        "Channel 11: note-ons=0; programs=unavailable; final-cc7=unavailable\n"
        "Channel 12: note-ons=0; programs=unavailable; final-cc7=unavailable\n"
        "Channel 13: note-ons=0; programs=unavailable; final-cc7=unavailable\n"
        "Channel 14: note-ons=0; programs=unavailable; final-cc7=unavailable\n"
        "Channel 15: note-ons=0; programs=unavailable; final-cc7=unavailable\n"
        "Channel 16: note-ons=0; programs=unavailable; final-cc7=unavailable\n";
    if (standardOutput.str() != expected) {
        std::cerr << "Unexpected MIDI diagnostics output:\n" << standardOutput.str();
        return 3;
    }
    return 0;
}
