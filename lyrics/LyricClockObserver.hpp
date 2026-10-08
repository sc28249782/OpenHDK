// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "audio/AudioBackend.hpp"
#include "lyrics/LyricMediaConsumer.hpp"

namespace OpenHDK {

enum class LyricClockPollStatus {
  Unbound, Held, Advanced, Cleared, Exhausted, InvalidSnapshot, BackwardPosition
};
struct LyricClockPollResult {
  LyricClockPollStatus status;
  std::optional<LyricCueBatch> batch;
};

// Serialized control-path adapter for ONE backend object's lifetime. bind()
// takes only a successful start acknowledgement for that same backend. It does
// not prove SongId/source identity. The controller must stop this observer
// BEFORE requesting backend stop, replacement, or shutdown. No callback calls.
class LyricClockObserver {
public:
  [[nodiscard]] bool bind(std::shared_ptr<const KarLyricTimeline> timeline,
                          const AudioBackendPlaybackStart& acknowledged) noexcept {
    if (!timeline || acknowledged.generation == 0U
        || acknowledged.generation <= lastAcknowledged_) return false;
    if (!consumer_.prepare(std::move(timeline))) return false;
    expectedGeneration_ = acknowledged.generation;
    lastAcknowledged_ = acknowledged.generation;
    return true;
  }
  void stop() noexcept {
    consumer_.stop();
    expectedGeneration_.reset();
  }
  [[nodiscard]] std::optional<std::uint64_t> expectedGeneration() const noexcept {
    return expectedGeneration_;
  }
  [[nodiscard]] LyricCueBatch observed() const noexcept { return consumer_.observed(); }
  [[nodiscard]] std::optional<std::uint64_t> mediaPositionMicroseconds() const noexcept {
    return consumer_.mediaPositionMicroseconds();
  }
  // Consume one read result without retry, interpolation, or reading session
  // state. Unstable is ignored even if a malformed caller supplies a payload.
  [[nodiscard]] LyricClockPollResult poll(const MediaClockReadResult& read) noexcept {
    if (read.status == MediaClockReadStatus::Unstable)
      return {LyricClockPollStatus::Held, std::nullopt};
    if (read.status == MediaClockReadStatus::Exhausted)
      return clear(LyricClockPollStatus::Exhausted);
    if (read.status != MediaClockReadStatus::Snapshot || !read.snapshot)
      return clear(LyricClockPollStatus::InvalidSnapshot);
    if (!expectedGeneration_) return {LyricClockPollStatus::Unbound, std::nullopt};
    const auto& snapshot = *read.snapshot;
    if (snapshot.source != MediaClockSource::CompiledTimeline
        || snapshot.generation != *expectedGeneration_)
      return clear(LyricClockPollStatus::Cleared);
    if (snapshot.revision == 0U || (snapshot.revision & 1U) != 0U)
      return clear(LyricClockPollStatus::InvalidSnapshot);
    switch (snapshot.phase) {
    case MediaClockPhase::Preparing:
      return {LyricClockPollStatus::Held, std::nullopt};
    case MediaClockPhase::Playing:
    case MediaClockPhase::Paused:
    case MediaClockPhase::Finished: {
      if (snapshot.failure != MediaClockFailure::None)
        return clear(LyricClockPollStatus::InvalidSnapshot);
      auto result = consumer_.advance(snapshot.mediaMicroseconds);
      if (!result.succeeded()) return clear(LyricClockPollStatus::BackwardPosition);
      return {LyricClockPollStatus::Advanced, result.batch()};
    }
    case MediaClockPhase::Failed:
    case MediaClockPhase::Stopped:
    case MediaClockPhase::Unavailable:
      return clear(LyricClockPollStatus::Cleared);
    }
    return clear(LyricClockPollStatus::InvalidSnapshot);
  }
private:
  LyricClockPollResult clear(LyricClockPollStatus status) noexcept {
    stop();
    return {status, std::nullopt};
  }
  LyricMediaConsumer consumer_;
  std::optional<std::uint64_t> expectedGeneration_;
  std::uint64_t lastAcknowledged_{};
};

} // namespace OpenHDK
