// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "audio/SmfTimelineCompiler.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace OpenHDK {

inline constexpr std::size_t kMidiDiagnosticChannelCount = 16U;
inline constexpr std::size_t kMidiProgramCount = 128U;

struct MidiChannelDiagnostic {
    std::uint64_t noteOnCount{};
    std::array<bool, kMidiProgramCount> observedPrograms{};
    std::optional<std::uint8_t> finalCc7Volume;
};

using MidiChannelDiagnosticSummary =
    std::array<MidiChannelDiagnostic, kMidiDiagnosticChannelCount>;

class MidiChannelDiagnostics {
public:
    [[nodiscard]] static MidiChannelDiagnosticSummary aggregate(const SmfTimeline& timeline) {
        MidiChannelDiagnosticSummary summary{};
        for (const auto& timelineEvent : timeline.events()) {
            const auto& event = timelineEvent.event();
            const auto channel = event.channel();
            if (!channel || *channel >= summary.size()) continue;

            const auto data = event.data();
            auto& diagnostic = summary[*channel];
            switch (event.kind()) {
            case SmfMidiEventKind::NoteOn:
                if (data[1] != 0U) ++diagnostic.noteOnCount;
                break;
            case SmfMidiEventKind::ProgramChange:
                diagnostic.observedPrograms[data[0]] = true;
                break;
            case SmfMidiEventKind::Controller:
                if (data[0] == 7U) diagnostic.finalCc7Volume = data[1];
                break;
            default:
                break;
            }
        }
        return summary;
    }
};

} // namespace OpenHDK
