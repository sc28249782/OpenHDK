// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "audio/AudioBackend.hpp"

#include <cmath>
#include <cstddef>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

namespace OpenHDK {

enum class RuntimeMixerConsoleResult { Continue, Quit };

inline RuntimeMixerConsoleResult runRuntimeMixerConsoleCommand(
    std::string_view command, AudioBackend& backend, AudioBackendStatus& status,
    std::ostream& output, std::ostream& error) {
    std::istringstream input{std::string(command)};
    std::string operation;
    std::string extra;
    input >> operation;
    if (operation.empty()) return RuntimeMixerConsoleResult::Continue;
    if (operation == "quit" || operation == "exit") return RuntimeMixerConsoleResult::Quit;
    if (operation == "help") {
        output << "Mixer commands: gain <1-16> <0-100>, mute <1-16>, unmute <1-16>, "
                  "solo <1-16>, unsolo <1-16>, preset-save <name>, preset-recall <name>, "
                  "preset-delete <name>, preset-list, reset, help, quit\n";
        return RuntimeMixerConsoleResult::Continue;
    }
    if (operation == "reset") {
        input >> extra;
        if (!extra.empty()) {
            error << "reset does not take arguments. Type help for mixer commands.\n";
            return RuntimeMixerConsoleResult::Continue;
        }
        if (!backend.resetRuntimeMixer(status)) error << "Mixer reset failed: " << status.message << '\n';
        else output << "Mixer reset.\n";
        return RuntimeMixerConsoleResult::Continue;
    }

    if (operation == "preset-list") {
        input >> extra;
        if (!extra.empty()) {
            error << "preset-list does not take arguments.\n";
            return RuntimeMixerConsoleResult::Continue;
        }
        std::vector<std::string> names;
        if (!backend.listRuntimeMixerPresets(names, status)) {
            error << "Preset list failed: " << status.message << '\n';
        } else {
            output << "Mixer presets (" << names.size() << "):\n";
            for (const auto& name : names) output << name << '\n';
        }
        return RuntimeMixerConsoleResult::Continue;
    }
    if (operation == "preset-save" || operation == "preset-recall" || operation == "preset-delete") {
        std::string name;
        input >> name >> extra;
        if (name.empty() || !extra.empty()) {
            error << operation << " takes one preset name.\n";
            return RuntimeMixerConsoleResult::Continue;
        }
        const bool succeeded = operation == "preset-save" ? backend.saveRuntimeMixerPreset(name, status)
            : operation == "preset-recall" ? backend.recallRuntimeMixerPreset(name, status)
                                          : backend.deleteRuntimeMixerPreset(name, status);
        if (!succeeded) error << "Preset command failed: " << status.message << '\n';
        else output << "Preset command applied.\n";
        return RuntimeMixerConsoleResult::Continue;
    }

    std::string channelText;
    input >> channelText;
    std::size_t parsed{};
    unsigned long channel{};
    try { channel = std::stoul(channelText, &parsed); } catch (...) { parsed = 0U; }
    if (channelText.empty() || parsed != channelText.size() || channel == 0U
        || channel > kMidiChannelCount) {
        error << "Channel must be from 1 to 16. Type help for mixer commands.\n";
        return RuntimeMixerConsoleResult::Continue;
    }
    const auto channelIndex = static_cast<std::size_t>(channel - 1U);
    bool succeeded{};
    if (operation == "gain") {
        std::string percentText;
        input >> percentText >> extra;
        std::size_t percentParsed{};
        float percent{};
        try { percent = std::stof(percentText, &percentParsed); } catch (...) { percentParsed = 0U; }
        if (percentText.empty() || percentParsed != percentText.size() || !extra.empty()
            || !std::isfinite(percent) || percent < 0.0F || percent > 100.0F) {
            error << "Gain must be a finite number from 0 to 100.\n";
            return RuntimeMixerConsoleResult::Continue;
        }
        succeeded = backend.setRuntimeChannelGain(channelIndex, percent / 100.0F, status);
    } else if (operation == "mute" || operation == "unmute" || operation == "solo" || operation == "unsolo") {
        input >> extra;
        if (!extra.empty()) {
            error << operation << " takes one channel argument.\n";
            return RuntimeMixerConsoleResult::Continue;
        }
        const bool enabled = operation == "mute" || operation == "solo";
        succeeded = operation == "mute" || operation == "unmute"
            ? backend.setRuntimeChannelMuted(channelIndex, enabled, status)
            : backend.setRuntimeChannelSoloed(channelIndex, enabled, status);
    } else {
        error << "Unknown mixer command: " << operation << ". Type help for mixer commands.\n";
        return RuntimeMixerConsoleResult::Continue;
    }
    if (!succeeded) error << "Mixer command failed: " << status.message << '\n';
    else output << "Mixer command applied.\n";
    return RuntimeMixerConsoleResult::Continue;
}

} // namespace OpenHDK
