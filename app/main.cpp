// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors

#include "audio/AudioDeviceDiagnostics.hpp"
#include "audio/FluidSynthBackend.hpp"
#include "app/MidiDiagnosticsCli.hpp"
#include "app/RuntimeMixerConsole.hpp"
#include "audio/SmfParser.hpp"
#include "audio/SmfTimelineCompiler.hpp"

#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#endif

namespace {

constexpr int kRuntimeError = 1;
constexpr int kUsageError = 64;

struct CommandLineOptions {
    std::string midi;
    std::string soundFont;
    std::string diagnosticMidi;
    bool enableDeviceOutput{true};
    bool listDevices{};
    bool muted{};
    bool interactiveMixer{};
    bool hasPlaybackOption{};
    std::optional<std::uint32_t> deviceIndex;
    float volume{1.0F};
    OpenHDK::MidiChannelGains channelGains{OpenHDK::defaultMidiChannelGains()};
};

void usage() {
    std::cout
        << "Usage: OpenHDK --midi <file.mid> --soundfont <file.sf2> [--device <index>] "
           "[--volume <0-100>] [--channel-volume <1-16>:<0-100>] [--mute] "
           "[--interactive-mixer] [--no-device]\n"
           "       OpenHDK --midi-diagnostics <file.mid>\n"
           "       OpenHDK --list-devices\n";
}

bool indexOf(const std::string& text, std::uint32_t& index) {
    try {
        std::size_t parsed{};
        const auto value = std::stoul(text, &parsed);
        if (parsed != text.size() || value > UINT32_MAX) return false;
        index = static_cast<std::uint32_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool volumeOf(const std::string& text, float& volume) {
    try {
        std::size_t parsed{};
        const auto percent = std::stof(text, &parsed);
        if (parsed != text.size()) return false;

        const auto normalized = percent / 100.0F;
        if (!OpenHDK::isNormalizedVolume(normalized)) return false;
        volume = normalized;
        return true;
    } catch (...) {
        return false;
    }
}

bool channelVolumeOf(const std::string& text, OpenHDK::MidiChannelGains& gains) {
    const auto delimiter = text.find(':');
    if (delimiter == std::string::npos || delimiter == 0U || delimiter == text.size() - 1U
        || text.find(':', delimiter + 1U) != std::string::npos) {
        return false;
    }

    try {
        std::size_t parsed{};
        const auto channel = std::stoul(text.substr(0U, delimiter), &parsed);
        if (parsed != delimiter || channel == 0U || channel > OpenHDK::kMidiChannelCount) {
            return false;
        }

        float volume{};
        if (!volumeOf(text.substr(delimiter + 1U), volume)) return false;
        gains[channel - 1U] = volume;
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<int> parseCommandLine(int argc, char* argv[], CommandLineOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];

        if (argument == "--help" || argument == "-h") {
            usage();
            return 0;
        }
        if (argument == "--midi-diagnostics" && index + 1 < argc) {
            options.diagnosticMidi = argv[++index];
            continue;
        }
        if (argument == "--list-devices") {
            options.listDevices = true;
            continue;
        }
        if (argument == "--no-device") {
            options.enableDeviceOutput = false;
            options.hasPlaybackOption = true;
            continue;
        }
        if (argument == "--mute") {
            options.muted = true;
            options.hasPlaybackOption = true;
            continue;
        }
        if (argument == "--interactive-mixer") {
            options.interactiveMixer = true;
            options.hasPlaybackOption = true;
            continue;
        }
        if (argument == "--device" && index + 1 < argc) {
            std::uint32_t deviceIndex{};
            if (!indexOf(argv[++index], deviceIndex)) {
                std::cerr << "Device index must be a non-negative integer.\n";
                return kUsageError;
            }
            options.deviceIndex = deviceIndex;
            options.hasPlaybackOption = true;
            continue;
        }
        if (argument == "--volume" && index + 1 < argc) {
            if (!volumeOf(argv[++index], options.volume)) {
                std::cerr << "Volume must be a finite number from 0 to 100.\n";
                return kUsageError;
            }
            options.hasPlaybackOption = true;
            continue;
        }
        if (argument == "--channel-volume" && index + 1 < argc) {
            if (!channelVolumeOf(argv[++index], options.channelGains)) {
                std::cerr << "Channel volume must be <1-16>:<0-100>.\n";
                return kUsageError;
            }
            options.hasPlaybackOption = true;
            continue;
        }
        if ((argument == "--midi" || argument == "--soundfont") && index + 1 < argc) {
            (argument == "--midi" ? options.midi : options.soundFont) = argv[++index];
            options.hasPlaybackOption = true;
            continue;
        }

        std::cerr << "Unknown or incomplete argument: " << argument << "\n";
        usage();
        return kUsageError;
    }
    return std::nullopt;
}

int listAudioDevices(OpenHDK::AudioBackendStatus& status) {
    const auto devices = OpenHDK::enumerateAudioOutputDevices(status);
    if (!status) {
        std::cerr << status.message << '\n';
        return kRuntimeError;
    }

    std::cout << "Playback devices (" << devices.size() << "):\n";
    for (std::size_t index = 0U; index < devices.size(); ++index) {
        std::cout << (devices[index].isDefault ? "* " : "  ") << '[' << index << "] "
                  << devices[index].name << '\n';
    }
    return 0;
}

std::optional<int> validatePlaybackOptions(const CommandLineOptions& options) {
    if (!options.enableDeviceOutput && options.deviceIndex) {
        std::cerr << "--device cannot be used with --no-device.\n";
        return kUsageError;
    }
    if (!options.enableDeviceOutput && options.interactiveMixer) {
        std::cerr << "--interactive-mixer requires an audio output device.\n";
        return kUsageError;
    }
#ifndef _WIN32
    if (options.interactiveMixer) {
        std::cerr << "--interactive-mixer is currently supported on Windows console builds only.\n";
        return kUsageError;
    }
#endif
    if (options.midi.empty() || options.soundFont.empty()) {
        usage();
        return kUsageError;
    }
    return std::nullopt;
}

#ifdef _WIN32
void pollInteractiveMixer(std::string& command, OpenHDK::AudioBackend& backend,
                          OpenHDK::AudioBackendStatus& status, bool& keepPlaying) {
    while (_kbhit() != 0) {
        const auto character = _getch();
        if (character == 0 || character == 0xe0) {
            static_cast<void>(_getch());
            continue;
        }
        if (character == '\r' || character == '\n') {
            std::cout << '\n';
            if (OpenHDK::runRuntimeMixerConsoleCommand(command, backend, status, std::cout,
                                                        std::cerr)
                == OpenHDK::RuntimeMixerConsoleResult::Quit) {
                keepPlaying = false;
            }
            command.clear();
        } else if (character == '\b') {
            if (!command.empty()) {
                command.pop_back();
                std::cout << "\b \b";
            }
        } else if (std::isprint(static_cast<unsigned char>(character))) {
            command.push_back(static_cast<char>(character));
            std::cout << static_cast<char>(character);
        }
    }
}
#endif

int renderHeadless(OpenHDK::AudioBackend& backend, OpenHDK::AudioBackendStatus& status) {
    std::array<float, 2048> pcm{};
    while (backend.isPlaying()) {
        if (!backend.renderStereo(pcm, status)) {
            std::cerr << "Headless PCM render failed: " << status.message << '\n';
            return kRuntimeError;
        }
    }
    return 0;
}

void waitForPlayback(OpenHDK::AudioBackend& backend) {
    while (backend.isPlaying()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void runInteractiveMixer(OpenHDK::AudioBackend& backend,
                         [[maybe_unused]] OpenHDK::AudioBackendStatus& status) {
    std::cout << "Interactive mixer enabled. Type help for commands.\n";
    std::string command;
    bool keepPlaying = true;
    while (keepPlaying && backend.isPlaying()) {
#ifdef _WIN32
        pollInteractiveMixer(command, backend, status, keepPlaying);
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!keepPlaying) backend.shutdown();
}

int runPlayback(const CommandLineOptions& options, OpenHDK::AudioBackendStatus& status) {
    std::vector<std::uint8_t> midiBytes;
    if (!OpenHDK::readMidiBytes(options.midi, midiBytes)) {
        std::cerr << "MIDI file could not be read: " << options.midi << '\n';
        return kRuntimeError;
    }

    const auto parsedMidi = OpenHDK::SmfParser::parse(midiBytes);
    if (!parsedMidi.file()) {
        const auto* error = parsedMidi.error();
        std::cerr << "MIDI parse failed at byte " << error->offset << ": " << error->message()
                  << '\n';
        return kRuntimeError;
    }

    const auto timeline = OpenHDK::SmfTimelineCompiler::compile(*parsedMidi.file());
    if (!timeline.timeline()) {
        const auto* error = timeline.error();
        std::cerr << "MIDI timeline compilation failed: " << error->message() << '\n';
        return kRuntimeError;
    }

    OpenHDK::FluidSynthBackend backend;
    const OpenHDK::AudioBackendConfig configuration{
        .soundFontPath = options.soundFont,
        .enableDeviceOutput = options.enableDeviceOutput,
        .outputDeviceIndex = options.deviceIndex,
        .volume = options.volume,
        .muted = options.muted,
        .channelGains = options.channelGains,
    };
    if (!backend.initialize(configuration, status)) {
        std::cerr << "Audio initialization failed: " << status.message << '\n';
        return kRuntimeError;
    }
    if (!backend.playCompiledTimeline(*timeline.timeline(), status)) {
        std::cerr << "MIDI playback failed: " << status.message << '\n';
        return kRuntimeError;
    }
    if (!backend.hasActiveDevice()) return renderHeadless(backend, status);

    if (options.interactiveMixer) {
        runInteractiveMixer(backend, status);
    } else {
        waitForPlayback(backend);
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    CommandLineOptions options;
    if (const auto result = parseCommandLine(argc, argv, options)) return *result;

    if (!options.diagnosticMidi.empty()) {
        if (options.listDevices || options.hasPlaybackOption) {
            std::cerr
                << "--midi-diagnostics cannot be combined with playback or device options.\n";
            return kUsageError;
        }
        return OpenHDK::runMidiDiagnostics(options.diagnosticMidi, std::cout, std::cerr);
    }

    OpenHDK::AudioBackendStatus status;
    if (options.listDevices) return listAudioDevices(status);
    if (const auto result = validatePlaybackOptions(options)) return *result;
    return runPlayback(options, status);
}
