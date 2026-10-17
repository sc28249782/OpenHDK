// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once
#include "audio/AudioBackend.hpp"
#include "library/SongPreparation.hpp"
#include "lyrics/LyricClockObserver.hpp"
#include <cstdint>
#include <memory>
#include <optional>

namespace OpenHDK {

// Serialized control-path playback controller for ONE AudioBackend object and
// its LyricClockObserver (OHK-CLI-030 step 5, first slice). Every method runs
// on the serialized control thread only; nothing here participates in an audio
// callback, and the backend callback keeps performing no allocation,
// deallocation, locking, I/O or logging.
//
// Start ordering. An active start is rejected before any observer or backend
// access, preserving the current binding and playback. A start from idle first
// stops (clears) the observer, then requests the backend start. A rejected
// backend start establishes no binding. A backend acknowledgement whose
// generation is zero or non-monotonic, or a failed observer bind, stops the
// observer BEFORE the backend stop and reports failure, so no active state
// leaks from the controller.
//
// Stop ordering. The observer stop happens BEFORE the backend stop; the
// retained PreparedSong outlives the backend stop and is released afterwards.
// A controller destroyed while active performs the same ordered stop.
//
// pollDisplay() performs exactly one committed mediaClock() read on the
// control thread and forwards it to the observer; it never interpolates wall
// time and never reads session state. Phase handling (NoLyrics, Preparing,
// Playing, Paused, Finished, Failed, Stopped, Unavailable, Unstable,
// Exhausted) follows the existing LyricClockObserver contract.
//
// libraryMutationAllowed() is the exclusion boundary for later CLI wiring:
// library mutations must be rejected while playback is active. Retained
// PreparedSong ownership survives later catalog mutations regardless.
class PlaybackController {
public:
  enum class StartError {
    None,          // Playback started; the acknowledgement below is admitted.
    AlreadyActive, // Current binding and playback preserved untouched.
    InvalidPrepared, // Null prepared handle; no backend access occurred.
    BackendRejected, // Backend refused the start; no binding is established.
    BindingFailed,   // Zero/non-monotonic acknowledgement or failed bind;
                     // observer stopped before the backend stop.
  };
  struct StartOutcome {
    StartError error{StartError::None};
    std::uint64_t acknowledgedGeneration{0U};
  };

  explicit PlaybackController(AudioBackend& backend) noexcept : backend_(backend) {}
  ~PlaybackController();

  PlaybackController(const PlaybackController&) = delete;
  PlaybackController& operator=(const PlaybackController&) = delete;

  // Retains the owning PreparedSong for the whole active lifetime.
  [[nodiscard]] StartOutcome start(std::shared_ptr<const PreparedSong> prepared);
  void stop();

  // One committed clock read per call, on the serialized control thread.
  [[nodiscard]] LyricClockPollResult pollDisplay() noexcept;

  // Mutation/play exclusion query for later CLI integration.
  [[nodiscard]] bool playbackActive() const noexcept { return prepared_ != nullptr; }
  [[nodiscard]] bool libraryMutationAllowed() const noexcept { return !playbackActive(); }

  [[nodiscard]] std::optional<std::uint64_t> expectedGeneration() const noexcept {
    return observer_.expectedGeneration();
  }
  [[nodiscard]] LyricCueBatch observed() const noexcept { return observer_.observed(); }
  // Retained prepared identity: the catalog revision of the owned snapshot.
  [[nodiscard]] std::optional<std::uint64_t> preparedCatalogRevision() const noexcept {
    if (!prepared_) return std::nullopt;
    return prepared_->catalogSnapshot()->revision;
  }

private:
  AudioBackend& backend_;
  LyricClockObserver observer_;
  std::shared_ptr<const PreparedSong> prepared_;
};

inline PlaybackController::~PlaybackController() { stop(); }

inline PlaybackController::StartOutcome PlaybackController::start(
    std::shared_ptr<const PreparedSong> prepared) {
  // 1. The active check happens first: a rejected start must not disturb the
  // current observer binding or the running playback.
  if (prepared_) return {StartError::AlreadyActive, 0U};
  if (!prepared) return {StartError::InvalidPrepared, 0U};
  // 2. Idle start: clear previous observer state BEFORE the backend start.
  observer_.stop();
  prepared_ = std::move(prepared);
  AudioBackendStatus status;
  AudioBackendPlaybackStart acknowledged;
  if (!backend_.playCompiledTimeline(prepared_->midi(), acknowledged, status)) {
    prepared_.reset(); // The backend never admitted this start; no binding.
    return {StartError::BackendRejected, 0U};
  }
  // 3. Bind only with the backend's own acknowledged start. A zero or
  // non-monotonic generation fails the bind; on any bind failure the observer
  // is stopped BEFORE the backend stop and the prepared handle is released.
  if (acknowledged.generation == 0U || !observer_.bind(prepared_->lyrics(), acknowledged)) {
    observer_.stop();
    AudioBackendStatus stopStatus;
    static_cast<void>(backend_.stopPlayback(stopStatus));
    prepared_.reset();
    return {StartError::BindingFailed, acknowledged.generation};
  }
  return {StartError::None, acknowledged.generation};
}

inline void PlaybackController::stop() {
  if (!prepared_) return; // Idle: do not stop backend playback we did not start.
  observer_.stop();       // Observer stop BEFORE the backend stop.
  AudioBackendStatus status;
  static_cast<void>(backend_.stopPlayback(status));
  prepared_.reset(); // Prepared lifetime outlives the backend stop.
}

inline LyricClockPollResult PlaybackController::pollDisplay() noexcept {
  // Exactly one committed read; the observer owns all phase decisions.
  return observer_.poll(backend_.mediaClock());
}

} // namespace OpenHDK