// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "audio/SmfTimelineCompiler.hpp"

#include <cstdint>
#include <span>

namespace OpenHDK {

/// A hardware-independent destination for supported SMF channel messages.
/// Pitch-bend values use the SMF 14-bit unsigned representation (0 through
/// 16383), with 8192 as the centre position.
class MidiCommandSink {
public:
    virtual ~MidiCommandSink() = default;

    virtual void noteOn(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity) = 0;
    virtual void noteOff(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity) = 0;
    virtual void controller(std::uint8_t channel, std::uint8_t controller,
                            std::uint8_t value) = 0;
    virtual void programChange(std::uint8_t channel, std::uint8_t program) = 0;
    virtual void pitchBend(std::uint8_t channel, std::uint16_t value) = 0;
};

/// Dispatches a pre-ordered timeline span without changing its order. This
/// boundary deliberately has no timing, playback state, allocation, or I/O.
class SmfMidiEventDispatcher {
public:
    static void dispatch(std::span<const SmfTimelineEvent> events, MidiCommandSink& sink) {
        for (const auto& timelineEvent : events) {
            const auto& event = timelineEvent.event();
            switch (event.kind()) {
            case SmfMidiEventKind::NoteOn: {
                const auto channel = *event.channel();
                const auto data = event.data();
                if (data[1] == 0U) {
                    sink.noteOff(channel, data[0], 0U);
                } else {
                    sink.noteOn(channel, data[0], data[1]);
                }
                break;
            }
            case SmfMidiEventKind::NoteOff: {
                const auto channel = *event.channel();
                const auto data = event.data();
                sink.noteOff(channel, data[0], data[1]);
                break;
            }
            case SmfMidiEventKind::Controller: {
                const auto channel = *event.channel();
                const auto data = event.data();
                sink.controller(channel, data[0], data[1]);
                break;
            }
            case SmfMidiEventKind::ProgramChange: {
                const auto channel = *event.channel();
                const auto data = event.data();
                sink.programChange(channel, data[0]);
                break;
            }
            case SmfMidiEventKind::PitchBend: {
                const auto channel = *event.channel();
                const auto data = event.data();
                sink.pitchBend(channel, static_cast<std::uint16_t>(data[0])
                                            | (static_cast<std::uint16_t>(data[1]) << 7U));
                break;
            }
            case SmfMidiEventKind::Tempo:
            case SmfMidiEventKind::EndOfTrack:
            case SmfMidiEventKind::Meta:
            case SmfMidiEventKind::SysEx:
                break;
            }
        }
    }
};

} // namespace OpenHDK
