// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "audio/MidiChannelDiagnostics.hpp"
#include "audio/SmfParser.hpp"
#include "audio/SmfTimelineCompiler.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <ostream>
#include <vector>

namespace OpenHDK {

[[nodiscard]] inline bool readMidiBytes(const std::filesystem::path& path,
                                        std::vector<std::uint8_t>& bytes) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) return false;

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;

    const auto length = input.tellg();
    if (length < 0 || static_cast<std::uintmax_t>(length)
            > std::numeric_limits<std::size_t>::max()
        || static_cast<std::uintmax_t>(length)
            > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return false;
    }

    bytes.resize(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(input) || bytes.empty();
}

inline void printMidiDiagnostics(const SmfFile& file, const SmfTimeline& timeline,
                                 std::ostream& output) {
    const auto summary = MidiChannelDiagnostics::aggregate(timeline);
    const auto printSample = [&output](const MidiChannelDiagnostic::ControllerSample& sample) {
        output << static_cast<unsigned>(sample.value) << "@" << sample.tick << "t/"
               << sample.timeMicroseconds << "us";
    };
    const auto printSeries = [&output, &printSample](
                                 const char* name,
                                 const MidiChannelDiagnostic::ControllerSeries& series,
                                 std::uint64_t noteOnCount) {
        output << name << '=';
        if (!series.final) {
            output << "unavailable";
            return;
        }
        output << "first=";
        printSample(*series.first);
        output << ",pre-note=";
        if (noteOnCount != 0U && series.beforeFirstNote) {
            printSample(*series.beforeFirstNote);
        } else {
            output << "unavailable";
        }
        output << ",final=";
        printSample(*series.final);
        output << ",range=" << static_cast<unsigned>(*series.minimum) << ".."
               << static_cast<unsigned>(*series.maximum) << ",changes=" << series.changeCount;
    };
    output << "MIDI diagnostics: SMF format " << file.format() << ", tracks "
           << file.tracks().size() << '\n';
    for (std::size_t channel = 0U; channel < summary.size(); ++channel) {
        const auto& diagnostic = summary[channel];
        output << "Channel " << (channel + 1U) << ": note-ons=" << diagnostic.noteOnCount
               << "; programs=";
        bool anyProgram = false;
        for (std::size_t program = 0U; program < diagnostic.observedPrograms.size(); ++program) {
            if (!diagnostic.observedPrograms[program]) continue;
            if (anyProgram) output << ',';
            output << (program + 1U);
            anyProgram = true;
        }
        if (!anyProgram) output << "unavailable";
        output << "; ";
        printSeries("cc7", diagnostic.cc7, diagnostic.noteOnCount);
        output << "; ";
        printSeries("effective-cc7", diagnostic.effectiveCc7, diagnostic.noteOnCount);
        output << "; cc7-14bit=";
        if (diagnostic.finalCc7FourteenBit) {
            output << *diagnostic.finalCc7FourteenBit;
        } else {
            output << "unavailable";
        }
        output << "; ";
        printSeries("cc39", diagnostic.cc39, diagnostic.noteOnCount);
        output << "; ";
        printSeries("cc11", diagnostic.cc11, diagnostic.noteOnCount);
        output << "; cc11-14bit=";
        if (diagnostic.finalCc11FourteenBit) {
            output << *diagnostic.finalCc11FourteenBit;
        } else {
            output << "unavailable";
        }
        output << "; ";
        printSeries("cc43", diagnostic.cc43, diagnostic.noteOnCount);
        output << "; cc121-resets=" << diagnostic.resetAllControllersCount;
        if (diagnostic.finalCc121Reset) {
            output << ",last=" << diagnostic.finalCc121Reset->tick << "t/"
                   << diagnostic.finalCc121Reset->timeMicroseconds << "us";
        }
        output << '\n';
    }
}

[[nodiscard]] inline int runMidiDiagnostics(const std::filesystem::path& path,
                                             std::ostream& output, std::ostream& error) {
    std::vector<std::uint8_t> bytes;
    if (!readMidiBytes(path, bytes)) {
        error << "MIDI file could not be read: " << path.string() << '\n';
        return 1;
    }

    const auto parsed = SmfParser::parse(bytes);
    if (!parsed.file()) {
        const auto* parseError = parsed.error();
        error << "MIDI parse failed";
        if (parseError != nullptr) {
            error << " at byte " << parseError->offset << ": " << parseError->message();
        }
        error << '\n';
        return 1;
    }

    const auto compiled = SmfTimelineCompiler::compile(*parsed.file());
    if (!compiled.timeline()) {
        const auto* timelineError = compiled.error();
        error << "MIDI timeline compilation failed";
        if (timelineError != nullptr) error << ": " << timelineError->message();
        error << '\n';
        return 1;
    }

    printMidiDiagnostics(*parsed.file(), *compiled.timeline(), output);
    return 0;
}

} // namespace OpenHDK
