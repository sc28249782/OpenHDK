// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "audio/MidiChannelMix.hpp"
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
    struct ControllerSample {
        std::uint8_t value{};
        std::uint64_t tick{};
        std::uint64_t timeMicroseconds{};
    };

    struct ControllerSeries {
        std::optional<ControllerSample> first;
        std::optional<ControllerSample> beforeFirstNote;
        std::optional<ControllerSample> final;
        std::optional<std::uint8_t> minimum;
        std::optional<std::uint8_t> maximum;
        std::uint64_t changeCount{};
    };

    ControllerSeries cc7;
    ControllerSeries effectiveCc7;
    ControllerSeries cc39;
    ControllerSeries cc11;
    ControllerSeries cc43;
    std::optional<std::uint16_t> finalCc7FourteenBit;
    std::optional<std::uint16_t> finalCc11FourteenBit;
    std::uint64_t resetAllControllersCount{};
    std::optional<ControllerSample> finalCc121Reset;
};

using MidiChannelDiagnosticSummary =
    std::array<MidiChannelDiagnostic, kMidiDiagnosticChannelCount>;

class MidiChannelDiagnostics {
public:
    [[nodiscard]] static MidiChannelDiagnosticSummary aggregate(const SmfTimeline& timeline) {
        MidiChannelDiagnosticSummary summary{};
        std::array<std::optional<std::uint8_t>, kMidiDiagnosticChannelCount> cc7Msbs{};
        std::array<std::optional<std::uint8_t>, kMidiDiagnosticChannelCount> cc7Lsbs{};
        std::array<std::optional<std::uint8_t>, kMidiDiagnosticChannelCount> cc11Msbs{};
        std::array<std::optional<std::uint8_t>, kMidiDiagnosticChannelCount> cc11Lsbs{};
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
            case SmfMidiEventKind::Controller: {
                const MidiChannelDiagnostic::ControllerSample sample{
                    data[1], timelineEvent.tick(), timelineEvent.timeMicroseconds()};
                switch (data[0]) {
                case 7U:
                    record(diagnostic.cc7, sample, diagnostic.noteOnCount == 0U);
                    record(diagnostic.effectiveCc7, sample, diagnostic.noteOnCount == 0U);
                    cc7Msbs[*channel] = data[1];
                    updateFourteenBit(diagnostic.finalCc7FourteenBit, cc7Msbs[*channel],
                                     cc7Lsbs[*channel]);
                    break;
                case 39U:
                    record(diagnostic.cc39, sample, diagnostic.noteOnCount == 0U);
                    cc7Lsbs[*channel] = data[1];
                    updateFourteenBit(diagnostic.finalCc7FourteenBit, cc7Msbs[*channel],
                                     cc7Lsbs[*channel]);
                    break;
                case 11U:
                    record(diagnostic.cc11, sample, diagnostic.noteOnCount == 0U);
                    cc11Msbs[*channel] = data[1];
                    updateFourteenBit(diagnostic.finalCc11FourteenBit, cc11Msbs[*channel],
                                     cc11Lsbs[*channel]);
                    break;
                case 43U:
                    record(diagnostic.cc43, sample, diagnostic.noteOnCount == 0U);
                    cc11Lsbs[*channel] = data[1];
                    updateFourteenBit(diagnostic.finalCc11FourteenBit, cc11Msbs[*channel],
                                     cc11Lsbs[*channel]);
                    break;
                case 121U:
                    ++diagnostic.resetAllControllersCount;
                    diagnostic.finalCc121Reset = sample;
                    record(diagnostic.effectiveCc7,
                           MidiChannelDiagnostic::ControllerSample{
                               kMidiDefaultChannelVolume, timelineEvent.tick(),
                               timelineEvent.timeMicroseconds()},
                           diagnostic.noteOnCount == 0U);
                    break;
                default:
                    break;
                }
                break;
            }
            default:
                break;
            }
        }
        return summary;
    }

private:
    static void record(MidiChannelDiagnostic::ControllerSeries& series,
                       MidiChannelDiagnostic::ControllerSample sample,
                       bool beforeFirstNote) {
        if (!series.first) series.first = sample;
        if (beforeFirstNote) series.beforeFirstNote = sample;
        if (series.final && series.final->value != sample.value) ++series.changeCount;
        series.final = sample;
        if (!series.minimum || sample.value < *series.minimum) series.minimum = sample.value;
        if (!series.maximum || sample.value > *series.maximum) series.maximum = sample.value;
    }

    static void updateFourteenBit(std::optional<std::uint16_t>& target,
                                  const std::optional<std::uint8_t>& msb,
                                  const std::optional<std::uint8_t>& lsb) {
        if (!msb || !lsb) return;
        target = static_cast<std::uint16_t>((static_cast<std::uint16_t>(*msb) << 7U)
                                            | static_cast<std::uint16_t>(*lsb));
    }
};

} // namespace OpenHDK
