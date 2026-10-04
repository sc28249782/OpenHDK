// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "TestCheck.hpp"
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
        OPENHDK_FAIL_IF(1, !output);
    }

    std::ostringstream standardOutput;
    std::ostringstream standardError;
    const auto exitCode = OpenHDK::runMidiDiagnostics(path, standardOutput, standardError);
    std::filesystem::remove(path, error);
    OPENHDK_FAIL_IF(2, exitCode != 0 || !standardError.str().empty());

    const auto diagnostics = standardOutput.str();
    const std::string expectedChannelOne =
        "Channel 1: note-ons=1; programs=3,41; "
        "cc7=first=80@0t/0us,pre-note=unavailable,final=64@0t/0us,range=64..80,changes=1; "
        "effective-cc7=first=80@0t/0us,pre-note=unavailable,final=64@0t/0us,range=64..80,changes=1; "
        "cc7-14bit=unavailable; cc39=unavailable; cc11=unavailable; "
        "cc11-14bit=unavailable; cc43=unavailable; cc121-resets=0\n";
    const std::string expectedChannelTen =
        "Channel 10: note-ons=1; programs=unavailable; cc7=unavailable; "
        "effective-cc7=unavailable; "
        "cc7-14bit=unavailable; cc39=unavailable; cc11=unavailable; "
        "cc11-14bit=unavailable; cc43=unavailable; cc121-resets=0\n";
    if (!diagnostics.starts_with("MIDI diagnostics: SMF format 1, tracks 2\n")
        || diagnostics.find(expectedChannelOne) == std::string::npos
        || diagnostics.find(expectedChannelTen) == std::string::npos
        || diagnostics.find("final-cc7") != std::string::npos) {
        std::cerr << "Unexpected MIDI diagnostics output:\n" << standardOutput.str();
        return OpenHDK::Test::reportFailure(
            "diagnostics output differs from the expected format or channel summary",
            __FILE__, __LINE__, 3);
    }
    return 0;
}
