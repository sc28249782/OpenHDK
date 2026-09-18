// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "audio/SmfTrackEventDecoder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace OpenHDK {

enum class SmfTimelineErrorCode {
    TrackDecodeFailed,
    UnsupportedSmpteDivision,
    TimeOverflow,
};

class SmfTimelineError {
public:
    [[nodiscard]] SmfTimelineErrorCode code() const { return code_; }
    [[nodiscard]] std::optional<std::size_t> trackIndex() const { return trackIndex_; }
    [[nodiscard]] const SmfTrackDecodeError* trackDecodeError() const {
        return trackDecodeError_ ? &*trackDecodeError_ : nullptr;
    }
    [[nodiscard]] constexpr std::string_view message() const {
        switch (code_) {
        case SmfTimelineErrorCode::TrackDecodeFailed: return "SMF track event decoding failed";
        case SmfTimelineErrorCode::UnsupportedSmpteDivision:
            return "SMPTE division is not supported by the timeline compiler";
        case SmfTimelineErrorCode::TimeOverflow: return "timeline time conversion overflows";
        }
        return "unknown timeline compilation error";
    }

private:
    friend class SmfTimelineCompiler;

    SmfTimelineError(SmfTimelineErrorCode code, std::optional<std::size_t> trackIndex,
                     std::optional<SmfTrackDecodeError> trackDecodeError)
        : code_(code), trackIndex_(trackIndex), trackDecodeError_(std::move(trackDecodeError)) {}

    SmfTimelineErrorCode code_;
    std::optional<std::size_t> trackIndex_;
    std::optional<SmfTrackDecodeError> trackDecodeError_;
};

class SmfTimelineEvent {
public:
    [[nodiscard]] std::uint64_t tick() const { return tick_; }
    [[nodiscard]] std::uint64_t timeMicroseconds() const { return timeMicroseconds_; }
    [[nodiscard]] std::size_t trackIndex() const { return trackIndex_; }
    [[nodiscard]] std::size_t sourceIndex() const { return sourceIndex_; }
    [[nodiscard]] const SmfMidiEvent& event() const { return event_; }

private:
    friend class SmfTimelineCompiler;

    SmfTimelineEvent(std::uint64_t tick, std::uint64_t timeMicroseconds, std::size_t trackIndex,
                     std::size_t sourceIndex, SmfMidiEvent event)
        : tick_(tick), timeMicroseconds_(timeMicroseconds), trackIndex_(trackIndex),
          sourceIndex_(sourceIndex), event_(std::move(event)) {}

    std::uint64_t tick_;
    std::uint64_t timeMicroseconds_;
    std::size_t trackIndex_;
    std::size_t sourceIndex_;
    SmfMidiEvent event_;
};

/// Events are ordered by absolute tick, then file track index, then source
/// index. Tempo changes take effect after their own event at a tick; therefore
/// when several tempo events share a tick, the last event in this ordering sets
/// the tempo for later ticks. The compiler carries the numerator remainder of
/// each PPQN division across intervals and tempo changes, so an event timestamp
/// is floor of the total integrated musical time through its tick.
class SmfTimeline {
public:
    [[nodiscard]] std::span<const SmfTimelineEvent> events() const { return events_; }

private:
    friend class SmfTimelineCompiler;

    explicit SmfTimeline(std::vector<SmfTimelineEvent> events) : events_(std::move(events)) {}

    std::vector<SmfTimelineEvent> events_;
};

class SmfTimelineCompileResult {
public:
    [[nodiscard]] bool succeeded() const { return timeline_.has_value(); }
    [[nodiscard]] const SmfTimeline* timeline() const {
        return timeline_ ? &*timeline_ : nullptr;
    }
    [[nodiscard]] const SmfTimelineError* error() const {
        return error_ ? &*error_ : nullptr;
    }

private:
    friend class SmfTimelineCompiler;

    explicit SmfTimelineCompileResult(SmfTimeline timeline) : timeline_(std::move(timeline)) {}
    explicit SmfTimelineCompileResult(SmfTimelineError error) : error_(std::move(error)) {}

    std::optional<SmfTimeline> timeline_;
    std::optional<SmfTimelineError> error_;
};

class SmfTimelineCompiler {
public:
    [[nodiscard]] static SmfTimelineCompileResult compile(const SmfFile& file) {
        if ((file.division() & 0x8000U) != 0U) {
            return failure(SmfTimelineErrorCode::UnsupportedSmpteDivision, std::nullopt, std::nullopt);
        }
        std::vector<UnscheduledEvent> unscheduled;
        const auto tracks = file.tracks();
        for (std::size_t trackIndex = 0U; trackIndex < tracks.size(); ++trackIndex) {
            const auto decoded = SmfTrackEventDecoder::decode(tracks[trackIndex]);
            if (!decoded.succeeded()) {
                return failure(SmfTimelineErrorCode::TrackDecodeFailed, trackIndex, *decoded.error());
            }
            const auto trackEvents = decoded.events()->events();
            for (std::size_t sourceIndex = 0U; sourceIndex < trackEvents.size(); ++sourceIndex) {
                unscheduled.push_back({trackEvents[sourceIndex].tick(), trackIndex, sourceIndex,
                                       trackEvents[sourceIndex]});
            }
        }
        std::sort(unscheduled.begin(), unscheduled.end(), [](const auto& left, const auto& right) {
            if (left.tick != right.tick) return left.tick < right.tick;
            if (left.trackIndex != right.trackIndex) return left.trackIndex < right.trackIndex;
            return left.sourceIndex < right.sourceIndex;
        });

        const auto ppqn = static_cast<std::uint64_t>(file.division());
        std::uint64_t tempo = 500000U;
        std::uint64_t previousTick = 0U;
        std::uint64_t timeMicroseconds = 0U;
        std::uint64_t fractionalRemainder = 0U;
        std::vector<SmfTimelineEvent> timelineEvents;
        timelineEvents.reserve(unscheduled.size());
        for (const auto& event : unscheduled) {
            const auto deltaTicks = event.tick - previousTick;
            const auto interval = convertInterval(deltaTicks, tempo, ppqn, fractionalRemainder);
            if (!interval || timeMicroseconds > std::numeric_limits<std::uint64_t>::max() - interval->microseconds) {
                return failure(SmfTimelineErrorCode::TimeOverflow, std::nullopt, std::nullopt);
            }
            timeMicroseconds += interval->microseconds;
            fractionalRemainder = interval->remainder;
            previousTick = event.tick;
            timelineEvents.push_back(SmfTimelineEvent(event.tick, timeMicroseconds, event.trackIndex,
                                                      event.sourceIndex, event.event));
            if (event.event.kind() == SmfMidiEventKind::Tempo) {
                tempo = tempoValue(event.event);
            }
        }
        return SmfTimelineCompileResult(SmfTimeline(std::move(timelineEvents)));
    }

private:
    struct UnscheduledEvent {
        std::uint64_t tick;
        std::size_t trackIndex;
        std::size_t sourceIndex;
        SmfMidiEvent event;
    };

    struct TimeInterval {
        std::uint64_t microseconds;
        std::uint64_t remainder;
    };

    [[nodiscard]] static std::optional<TimeInterval> convertInterval(
        std::uint64_t deltaTicks, std::uint64_t tempo, std::uint64_t ppqn,
        std::uint64_t carriedRemainder) {
        const auto wholeTicks = deltaTicks / ppqn;
        const auto remainderTicks = deltaTicks % ppqn;
        if (wholeTicks != 0U && tempo > std::numeric_limits<std::uint64_t>::max() / wholeTicks) {
            return std::nullopt;
        }
        const auto wholeMicroseconds = wholeTicks * tempo;
        if (remainderTicks != 0U
            && tempo > std::numeric_limits<std::uint64_t>::max() / remainderTicks) {
            return std::nullopt;
        }
        const auto fractionalNumerator = remainderTicks * tempo;
        if (fractionalNumerator > std::numeric_limits<std::uint64_t>::max() - carriedRemainder) {
            return std::nullopt;
        }
        const auto combinedNumerator = fractionalNumerator + carriedRemainder;
        const auto fractionalMicroseconds = combinedNumerator / ppqn;
        if (wholeMicroseconds > std::numeric_limits<std::uint64_t>::max() - fractionalMicroseconds) {
            return std::nullopt;
        }
        return TimeInterval{wholeMicroseconds + fractionalMicroseconds, combinedNumerator % ppqn};
    }

    [[nodiscard]] static std::uint64_t tempoValue(const SmfMidiEvent& event) {
        const auto data = event.data();
        return (static_cast<std::uint64_t>(data[0]) << 16U)
             | (static_cast<std::uint64_t>(data[1]) << 8U)
             | static_cast<std::uint64_t>(data[2]);
    }

    [[nodiscard]] static SmfTimelineCompileResult failure(
        SmfTimelineErrorCode code, std::optional<std::size_t> trackIndex,
        std::optional<SmfTrackDecodeError> trackDecodeError) {
        return SmfTimelineCompileResult(
            SmfTimelineError(code, trackIndex, std::move(trackDecodeError)));
    }
};

} // namespace OpenHDK
