// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "audio/SmfTimelineCompiler.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace OpenHDK {

enum class PlaybackSessionState {
    Idle,
    Preparing,
    Ready,
    Playing,
    Paused,
    Stopping,
    Finished,
    Failed,
};

enum class PlaybackSessionErrorCode {
    IllegalTransition,
    InvalidSampleRate,
    ClockOverflow,
    CompletionBeforeEndOfTimeline,
};

class PlaybackSessionError {
public:
    [[nodiscard]] PlaybackSessionErrorCode code() const { return code_; }
    [[nodiscard]] PlaybackSessionState state() const { return state_; }
    [[nodiscard]] std::optional<PlaybackSessionState> requestedState() const {
        return requestedState_;
    }
    [[nodiscard]] constexpr std::string_view message() const {
        switch (code_) {
        case PlaybackSessionErrorCode::IllegalTransition: return "illegal playback session state transition";
        case PlaybackSessionErrorCode::InvalidSampleRate: return "sample rate must be non-zero";
        case PlaybackSessionErrorCode::ClockOverflow: return "rendered-frame media clock overflows";
        case PlaybackSessionErrorCode::CompletionBeforeEndOfTimeline:
            return "release tail cannot complete before the timeline ends";
        }
        return "unknown playback session error";
    }

private:
    friend class PlaybackSession;

    PlaybackSessionError(PlaybackSessionErrorCode code, PlaybackSessionState state,
                         std::optional<PlaybackSessionState> requestedState)
        : code_(code), state_(state), requestedState_(requestedState) {}

    PlaybackSessionErrorCode code_;
    PlaybackSessionState state_;
    std::optional<PlaybackSessionState> requestedState_;
};

class PlaybackSessionCommandResult {
public:
    [[nodiscard]] bool succeeded() const { return !error_.has_value(); }
    [[nodiscard]] const PlaybackSessionError* error() const {
        return error_ ? &*error_ : nullptr;
    }

private:
    friend class PlaybackSession;

    PlaybackSessionCommandResult() = default;
    explicit PlaybackSessionCommandResult(PlaybackSessionError error) : error_(std::move(error)) {}

    std::optional<PlaybackSessionError> error_;
};

class PlaybackSessionRenderResult {
public:
    [[nodiscard]] std::uint64_t blockStartMicroseconds() const { return blockStartMicroseconds_; }
    [[nodiscard]] std::uint64_t blockEndMicroseconds() const { return blockEndMicroseconds_; }
    [[nodiscard]] std::span<const SmfTimelineEvent> events() const { return events_; }
    [[nodiscard]] const PlaybackSessionError* error() const {
        return error_ ? &*error_ : nullptr;
    }
    [[nodiscard]] bool succeeded() const { return !error_.has_value(); }

private:
    friend class PlaybackSession;

    PlaybackSessionRenderResult(std::uint64_t blockStartMicroseconds,
                                std::uint64_t blockEndMicroseconds,
                                std::span<const SmfTimelineEvent> events)
        : blockStartMicroseconds_(blockStartMicroseconds), blockEndMicroseconds_(blockEndMicroseconds),
          events_(events) {}
    PlaybackSessionRenderResult(std::uint64_t timeMicroseconds, PlaybackSessionError error)
        : blockStartMicroseconds_(timeMicroseconds), blockEndMicroseconds_(timeMicroseconds),
          error_(std::move(error)) {}

    std::uint64_t blockStartMicroseconds_{};
    std::uint64_t blockEndMicroseconds_{};
    std::span<const SmfTimelineEvent> events_;
    std::optional<PlaybackSessionError> error_;
};

/// The session starts Idle. prepare() transitions Idle -> Preparing -> Ready;
/// play() permits Ready/Paused -> Playing; pause() permits Playing -> Paused;
/// completeReleaseTail() permits Playing/Paused -> Finished only after the
/// timeline has ended; stop() permits Ready/Playing/Paused/Finished/Failed ->
/// Stopping -> Idle. Preparing and Stopping are synchronous internal states.
///
/// Media time is derived solely from rendered frame counts and sample rate. The
/// frame-to-microsecond numerator remainder is retained between render blocks,
/// so the clock is invariant to block splitting. A Playing render block covers
/// [blockStart, blockEnd] and dispatches each remaining event at or before its
/// inclusive end exactly once. Paused blocks return no events and do not move
/// the clock. End-of-timeline does not itself transition to Finished.
class PlaybackSession {
public:
    [[nodiscard]] PlaybackSessionState state() const { return state_; }
    [[nodiscard]] std::uint64_t mediaTimeMicroseconds() const { return mediaTimeMicroseconds_; }
    [[nodiscard]] std::uint32_t sampleRate() const { return sampleRate_; }
    [[nodiscard]] bool endOfTimelineReached() const { return eventCursor_ == events_.size(); }

    [[nodiscard]] PlaybackSessionCommandResult prepare(const SmfTimeline& timeline,
                                                        std::uint32_t sampleRate) {
        if (state_ != PlaybackSessionState::Idle) return illegal(PlaybackSessionState::Preparing);
        state_ = PlaybackSessionState::Preparing;
        if (sampleRate == 0U) {
            state_ = PlaybackSessionState::Failed;
            return failure(PlaybackSessionErrorCode::InvalidSampleRate, PlaybackSessionState::Preparing,
                           std::nullopt);
        }
        const auto sourceEvents = timeline.events();
        events_.assign(sourceEvents.begin(), sourceEvents.end());
        sampleRate_ = sampleRate;
        mediaTimeMicroseconds_ = 0U;
        frameRemainder_ = 0U;
        eventCursor_ = 0U;
        state_ = PlaybackSessionState::Ready;
        return PlaybackSessionCommandResult();
    }

    [[nodiscard]] PlaybackSessionCommandResult play() {
        if (state_ != PlaybackSessionState::Ready && state_ != PlaybackSessionState::Paused) {
            return illegal(PlaybackSessionState::Playing);
        }
        state_ = PlaybackSessionState::Playing;
        return PlaybackSessionCommandResult();
    }

    [[nodiscard]] PlaybackSessionCommandResult pause() {
        if (state_ != PlaybackSessionState::Playing) return illegal(PlaybackSessionState::Paused);
        state_ = PlaybackSessionState::Paused;
        return PlaybackSessionCommandResult();
    }

    [[nodiscard]] PlaybackSessionCommandResult completeReleaseTail() {
        if (state_ != PlaybackSessionState::Playing && state_ != PlaybackSessionState::Paused) {
            return illegal(PlaybackSessionState::Finished);
        }
        if (!endOfTimelineReached()) {
            return failure(PlaybackSessionErrorCode::CompletionBeforeEndOfTimeline, state_,
                           PlaybackSessionState::Finished);
        }
        state_ = PlaybackSessionState::Finished;
        return PlaybackSessionCommandResult();
    }

    [[nodiscard]] PlaybackSessionCommandResult stop() {
        if (state_ != PlaybackSessionState::Ready && state_ != PlaybackSessionState::Playing
            && state_ != PlaybackSessionState::Paused && state_ != PlaybackSessionState::Finished
            && state_ != PlaybackSessionState::Failed) {
            return illegal(PlaybackSessionState::Stopping);
        }
        state_ = PlaybackSessionState::Stopping;
        events_.clear();
        sampleRate_ = 0U;
        mediaTimeMicroseconds_ = 0U;
        frameRemainder_ = 0U;
        eventCursor_ = 0U;
        state_ = PlaybackSessionState::Idle;
        return PlaybackSessionCommandResult();
    }

    /// This method performs no allocation. Its returned span remains valid
    /// until the next prepare() or stop() call on this session.
    [[nodiscard]] PlaybackSessionRenderResult render(std::uint64_t frames) {
        if (state_ == PlaybackSessionState::Paused) {
            return PlaybackSessionRenderResult(mediaTimeMicroseconds_, mediaTimeMicroseconds_, {});
        }
        if (state_ != PlaybackSessionState::Playing) {
            return PlaybackSessionRenderResult(
                mediaTimeMicroseconds_, PlaybackSessionError(PlaybackSessionErrorCode::IllegalTransition,
                                                              state_, PlaybackSessionState::Playing));
        }
        const auto clockAdvance = advanceClock(frames);
        if (!clockAdvance) {
            const auto error = PlaybackSessionError(PlaybackSessionErrorCode::ClockOverflow, state_,
                                                    std::nullopt);
            state_ = PlaybackSessionState::Failed;
            return PlaybackSessionRenderResult(mediaTimeMicroseconds_, error);
        }

        const auto blockStart = mediaTimeMicroseconds_;
        const auto blockEnd = clockAdvance->microseconds;
        auto endCursor = eventCursor_;
        while (endCursor < events_.size()
               && events_[endCursor].timeMicroseconds() <= blockEnd) {
            ++endCursor;
        }
        std::span<const SmfTimelineEvent> dueEvents;
        if (endCursor != eventCursor_) {
            dueEvents = std::span<const SmfTimelineEvent>(
                events_.data() + eventCursor_, endCursor - eventCursor_);
        }
        eventCursor_ = endCursor;
        mediaTimeMicroseconds_ = blockEnd;
        frameRemainder_ = clockAdvance->remainder;
        return PlaybackSessionRenderResult(blockStart, blockEnd, dueEvents);
    }

private:
    struct ClockAdvance {
        std::uint64_t microseconds;
        std::uint64_t remainder;
    };

    [[nodiscard]] std::optional<ClockAdvance> advanceClock(std::uint64_t frames) const {
        constexpr std::uint64_t microsecondsPerSecond = 1000000U;
        if (frames != 0U && microsecondsPerSecond > std::numeric_limits<std::uint64_t>::max() / frames) {
            return std::nullopt;
        }
        const auto numerator = frames * microsecondsPerSecond;
        if (numerator > std::numeric_limits<std::uint64_t>::max() - frameRemainder_) {
            return std::nullopt;
        }
        const auto combinedNumerator = numerator + frameRemainder_;
        const auto microsecondDelta = combinedNumerator / sampleRate_;
        if (mediaTimeMicroseconds_ > std::numeric_limits<std::uint64_t>::max() - microsecondDelta) {
            return std::nullopt;
        }
        return ClockAdvance{mediaTimeMicroseconds_ + microsecondDelta,
                            combinedNumerator % sampleRate_};
    }

    [[nodiscard]] PlaybackSessionCommandResult illegal(PlaybackSessionState requestedState) const {
        return failure(PlaybackSessionErrorCode::IllegalTransition, state_, requestedState);
    }

    [[nodiscard]] static PlaybackSessionCommandResult failure(
        PlaybackSessionErrorCode code, PlaybackSessionState state,
        std::optional<PlaybackSessionState> requestedState) {
        return PlaybackSessionCommandResult(PlaybackSessionError(code, state, requestedState));
    }

    PlaybackSessionState state_{PlaybackSessionState::Idle};
    std::vector<SmfTimelineEvent> events_;
    std::uint32_t sampleRate_{0U};
    std::uint64_t mediaTimeMicroseconds_{0U};
    std::uint64_t frameRemainder_{0U};
    std::size_t eventCursor_{0U};
};

} // namespace OpenHDK
