// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "audio/MidiRuntimeMixer.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace OpenHDK {

enum class MidiMixerPresetResult { Success, InvalidName, NotFound };

[[nodiscard]] constexpr bool isMidiMixerPresetName(std::string_view name) noexcept {
    const auto letter = [](char value) {
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
    };
    if (name.empty() || name.size() > 32U || !letter(name.front())) return false;
    for (const char value : name) {
        if (!letter(value) && !(value >= '0' && value <= '9')
            && value != '-' && value != '_') return false;
    }
    return true;
}

// Control-thread-only registry. It can allocate and MUST NOT run in a callback.
// Its payload contains flags only; it has no access to source MIDI state.
class MidiMixerPresets {
public:
    [[nodiscard]] MidiMixerPresetResult save(std::string_view name,
                                             const MidiRuntimeMixer& mixer) {
        if (!isMidiMixerPresetName(name)) return MidiMixerPresetResult::InvalidName;
        const auto snapshot = mixer.snapshot();
        presets_.insert_or_assign(std::string{name}, Flags{snapshot.mutedChannels,
                                                          snapshot.soloedChannels});
        return MidiMixerPresetResult::Success;
    }

    [[nodiscard]] MidiMixerPresetResult recall(std::string_view name,
                                               MidiRuntimeMixer& mixer) const noexcept {
        if (!isMidiMixerPresetName(name)) return MidiMixerPresetResult::InvalidName;
        const auto found = presets_.find(name);
        if (found == presets_.end()) return MidiMixerPresetResult::NotFound;
        static_cast<void>(mixer.setChannelFlags(found->second.muted, found->second.soloed));
        return MidiMixerPresetResult::Success;
    }

    [[nodiscard]] MidiMixerPresetResult erase(std::string_view name) {
        if (!isMidiMixerPresetName(name)) return MidiMixerPresetResult::InvalidName;
        const auto found = presets_.find(name);
        if (found == presets_.end()) return MidiMixerPresetResult::NotFound;
        presets_.erase(found);
        return MidiMixerPresetResult::Success;
    }

    // ASCII lexicographic order; names are case-sensitive.
    [[nodiscard]] std::vector<std::string> names() const {
        std::vector<std::string> result;
        result.reserve(presets_.size());
        for (const auto& [name, flags] : presets_) {
            static_cast<void>(flags);
            result.push_back(name);
        }
        return result;
    }

private:
    struct Flags { std::uint32_t muted; std::uint32_t soloed; };
    std::map<std::string, Flags, std::less<>> presets_;
};

} // namespace OpenHDK
