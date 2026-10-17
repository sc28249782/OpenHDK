// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once
#include "audio/AudioBackend.hpp"
#include "library/SongPreparation.hpp"
#include "lyrics/LyricClockObserver.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace OpenHDK {

// Serialized control-path playback controller for ONE AudioBackend object and
// its LyricClockObserver (OHK-CLI-030 step 5, first slice). Every method runs
// on the serialized control thread only; nothing here participates in an audio
// callback, and the backend callback keeps performing no allocation,
// deallocation, locking, I/O or logging.
//
// Lifetime. The backend must outlive the controller. One controller drives one
// backend object and the acknowledged generations that backend produced; the
// bound observer rejects generation reuse across stops on the same backend.
// A terminal clock phase (Failed/Stopped/Unavailable/Exhausted) clears the
// observer but keeps the controller active: library mutations stay excluded
// until an explicit stop(). Exhausted publications require a replacement
// backend object (media clock contract); this slice never replaces backends.
//
// Start ordering. An active start is rejected before any observer or backend
// access and makes no backend call, preserving the current binding and
// playback. A start from idle first stops (clears) the observer, then requests
// the backend start. A backend refusal (false return) admits nothing and needs
// no cleanup. A backend acknowledgement whose generation is zero or
// non-monotonic, or a failed observer bind, stops the observer BEFORE the
// backend stop and reports failure. The retained PreparedSong handle is
// assigned to the controller only after a successful admission and bind, so a
// throwing backend start cannot leak an active state.
//
// Unconfirmed backend stop. Backend start and stop calls may throw or report
// false. Every backend stop attempt happens only after the observer stop. If a
// stop does not confirm, the controller keeps the prepared handle, keeps
// library mutations excluded, reports the failure with the backend message,
// and allows an explicit stop() retry. The prepared handle survives until a
// confirmed stop or shutdown. A controller destroyed in the unconfirmed state
// attempts the ordered stop once more and then calls backend.shutdown()
// (noexcept, interface contract) as final cleanup.
//
// pollDisplay() performs exactly one committed mediaClock() read on the
// control thread and forwards it to the observer; it never interpolates wall
// time and never reads session state. Phase handling (NoLyrics, Preparing,
// Playing, Paused, Finished, Failed, Stopped, Unavailable, Unstable,
// Exhausted) follows the existing LyricClockObserver contract.
//
// libraryMutationAllowed() is the exclusion boundary for later CLI wiring:
// library mutations must be rejected while playback is active or while a
// backend stop is unconfirmed. Retained PreparedSong ownership survives later
// catalog mutations regardless.
class PlaybackController {
public:
  enum class StartError {
    None,            // Playback started; the acknowledgement below is admitted.
    AlreadyActive,   // Current binding and playback preserved; no backend call.
    InvalidPrepared, // Null prepared handle; no backend access occurred.
    BackendRejected, // Backend refused the start (false); nothing was admitted
                     // and no binding is established; the controller is idle.
    Failed,          // A throwing backend start failed; the cleanup stop
                     // confirmed and the controller is idle with no binding.
    BindingFailed,   // Zero/non-monotonic acknowledgement or failed bind; the
                     // cleanup stop confirmed and the controller is idle.
    StopFailed,      // Start failed and the cleanup stop did not confirm; the
                     // controller keeps the prepared handle, stays blocked for
                     // mutations and waits for an explicit stop() retry.
  };
  struct StartOutcome {
    StartError error{StartError::None};
    std::uint64_t acknowledgedGeneration{0U};
  };
  enum class StopError {
    None,            // Idle no-op, or the backend stop was confirmed.
    StopUnconfirmed, // The backend stop returned false or threw; the controller
                     // keeps the prepared handle and stays blocked for
                     // mutations until a later stop() confirms.
  };
  struct StopOutcome {
    StopError error{StopError::None};
    std::string message{}; // Backend message or exception text when blocked.
  };

  explicit PlaybackController(AudioBackend& backend) noexcept : backend_(backend) {}
  ~PlaybackController();

  PlaybackController(const PlaybackController&) = delete;
  PlaybackController& operator=(const PlaybackController&) = delete;

  // Retains the owning PreparedSong for the whole active lifetime.
  [[nodiscard]] StartOutcome start(std::shared_ptr<const PreparedSong> prepared);
  // Attempts the ordered stop; inspect the outcome before assuming idleness.
  [[nodiscard]] StopOutcome stop();

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
  // Shared cleanup for a failed start after the backend attempt: the observer
  // is already stopped. Confirms the backend stop or enters the unconfirmed
  // blocked state. noexcept: the backend call is guarded internally.
  [[nodiscard]] StartOutcome finishFailedStart(
      std::shared_ptr<const PreparedSong>&& prepared,
      std::uint64_t acknowledgedGeneration, StartError failedError) noexcept;

  AudioBackend& backend_;
  LyricClockObserver observer_;
  std::shared_ptr<const PreparedSong> prepared_;
  bool stopUnconfirmed_{false};
};

inline PlaybackController::~PlaybackController() {
  try {
    stop(); // Ordered stop attempt; failures are reported, not thrown.
  } catch (...) {
    // Guard the outcome message allocation during destruction only.
  }
  if (prepared_) {
    // Backend stop never confirmed: final noexcept cleanup per the interface.
    backend_.shutdown();
  }
}

inline PlaybackController::StartOutcome PlaybackController::start(
    std::shared_ptr<const PreparedSong> prepared) {
  // 1. The active check happens first: a rejected start must not disturb the
  // current observer binding or the running playback, and makes no backend
  // call.
  if (prepared_) return {StartError::AlreadyActive, 0U};
  if (!prepared) return {StartError::InvalidPrepared, 0U};
  // 2. Idle start: clear previous observer state BEFORE the backend start.
  observer_.stop();
  AudioBackendStatus status;
  AudioBackendPlaybackStart acknowledged;
  try {
    if (!backend_.playCompiledTimeline(prepared->midi(), acknowledged, status)) {
      // Definitive refusal: the backend never admitted this start.
      return {StartError::BackendRejected, 0U};
    }
  } catch (...) {
    // Unknown outcome after an attempt; the observer is already cleared.
    return finishFailedStart(std::move(prepared), 0U, StartError::Failed);
  }
  // 3. Bind only with the backend's own acknowledged start. A zero or
  // non-monotonic generation fails the bind; on any bind failure the observer
  // is stopped BEFORE the backend stop.
  if (acknowledged.generation == 0U || !observer_.bind(prepared->lyrics(), acknowledged)) {
    observer_.stop();
    return finishFailedStart(std::move(prepared), acknowledged.generation,
                             StartError::BindingFailed);
  }
  // 4. Assign the retained handle only after admission and bind succeeded, so
  // a throwing backend start cannot leak an active controller state.
  prepared_ = std::move(prepared);
  return {StartError::None, acknowledged.generation};
}

inline PlaybackController::StartOutcome PlaybackController::finishFailedStart(
    std::shared_ptr<const PreparedSong>&& prepared,
    std::uint64_t acknowledgedGeneration, StartError failedError) noexcept {
  AudioBackendStatus stopStatus;
  bool confirmed = false;
  try {
    confirmed = backend_.stopPlayback(stopStatus);
  } catch (...) {
    confirmed = false;
  }
  if (confirmed) {
    prepared_.reset(); // The local handle releases with this call.
    return {failedError, acknowledgedGeneration};
  }
  // Backend stop unconfirmed: keep the prepared handle, block mutations and
  // wait for an explicit stop() retry.
  prepared_ = std::move(prepared);
  stopUnconfirmed_ = true;
  return {StartError::StopFailed, acknowledgedGeneration};
}

inline PlaybackController::StopOutcome PlaybackController::stop() {
  if (!prepared_) return {StopError::None, {}}; // Idle: do not stop playback we did not start.
  observer_.stop(); // Observer stop BEFORE the backend stop, on every attempt.
  AudioBackendStatus status;
  bool confirmed = false;
  try {
    confirmed = backend_.stopPlayback(status);
  } catch (const std::exception& e) {
    stopUnconfirmed_ = true;
    return {StopError::StopUnconfirmed, e.what()};
  } catch (...) {
    stopUnconfirmed_ = true;
    return {StopError::StopUnconfirmed, "unknown backend stop failure"};
  }
  if (!confirmed) {
    stopUnconfirmed_ = true;
    return {StopError::StopUnconfirmed, status.message};
  }
  stopUnconfirmed_ = false;
  prepared_.reset(); // Prepared lifetime outlives the confirmed backend stop.
  return {StopError::None, {}};
}

inline LyricClockPollResult PlaybackController::pollDisplay() noexcept {
  // Exactly one committed read; the observer owns all phase decisions.
  return observer_.poll(backend_.mediaClock());
}

} // namespace OpenHDK