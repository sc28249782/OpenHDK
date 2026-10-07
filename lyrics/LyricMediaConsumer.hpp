// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "lyrics/KarLyricExtractor.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace OpenHDK {

class LyricMediaConsumer;

// Retain the batch while using its spans. Ownership survives consumer reset,
// stop, replacement, or destruction. A retained historical batch is not the
// consumer's current display state after reset/stop.
class LyricCueBatch {
public:
  std::span<const KarLyricCue> cues() const noexcept {
    return timeline_ ? timeline_->cues().subspan(first_, count_) : std::span<const KarLyricCue>{};
  }
private:
  friend class LyricMediaConsumer;
  LyricCueBatch(std::shared_ptr<const KarLyricTimeline> timeline,
                std::size_t first, std::size_t count) noexcept
      : timeline_(std::move(timeline)), first_(first), count_(count) {}
  std::shared_ptr<const KarLyricTimeline> timeline_;
  std::size_t first_;
  std::size_t count_;
};

enum class LyricConsumerErrorCode { Unprepared, BackwardPosition };
struct LyricConsumerError {
  LyricConsumerErrorCode code;
  std::uint64_t requestedMicroseconds;
  std::optional<std::uint64_t> previousMicroseconds;
};

class LyricAdvanceResult {
public:
  bool succeeded() const noexcept { return !error_; }
  const std::optional<LyricConsumerError>& error() const noexcept { return error_; }
  const LyricCueBatch& batch() const noexcept { return batch_; }
private:
  friend class LyricMediaConsumer;
  explicit LyricAdvanceResult(LyricCueBatch batch) noexcept : batch_(std::move(batch)) {}
  LyricAdvanceResult(LyricCueBatch batch, LyricConsumerError error) noexcept
      : batch_(std::move(batch)), error_(error) {}
  LyricCueBatch batch_;
  std::optional<LyricConsumerError> error_;
};

// Pure, serialized control/observation-path consumer. Input is PlaybackSession
// media time, not elapsed wall time. Backend publication is a separate design;
// this class is not an audio callback handoff or a concurrent UI adapter.
// Current display state is the emitted immutable cue prefix, including each
// cue's whole ordered operation sequence. Screen rendering is left to adapters.
class LyricMediaConsumer {
public:
  bool prepare(std::shared_ptr<const KarLyricTimeline> timeline) noexcept {
    if (!timeline) return false; // Preserve an existing traversal on bad input.
    timeline_ = std::move(timeline);
    reset();
    return true;
  }
  void reset() noexcept {
    next_ = 0U;
    position_.reset();
  }
  void stop() noexcept {
    reset();
    timeline_.reset();
  }
  bool prepared() const noexcept { return static_cast<bool>(timeline_); }
  std::optional<std::uint64_t> mediaPositionMicroseconds() const noexcept { return position_; }
  LyricCueBatch observed() const noexcept { return LyricCueBatch(timeline_, 0U, next_); }

  LyricAdvanceResult advance(std::uint64_t mediaMicroseconds) noexcept {
    if (!timeline_) {
      return LyricAdvanceResult(LyricCueBatch(nullptr, 0U, 0U),
          {LyricConsumerErrorCode::Unprepared, mediaMicroseconds, position_});
    }
    if (position_ && mediaMicroseconds < *position_) {
      return LyricAdvanceResult(LyricCueBatch(timeline_, next_, 0U),
          {LyricConsumerErrorCode::BackwardPosition, mediaMicroseconds, position_});
    }
    const auto first = next_;
    const auto cues = timeline_->cues();
    while (next_ < cues.size() && cues[next_].position.timeMicroseconds <= mediaMicroseconds) ++next_;
    position_ = mediaMicroseconds;
    return LyricAdvanceResult(LyricCueBatch(timeline_, first, next_ - first));
  }
private:
  std::shared_ptr<const KarLyricTimeline> timeline_;
  std::size_t next_ = 0U;
  std::optional<std::uint64_t> position_;
};

} // namespace OpenHDK
