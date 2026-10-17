// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once
#include "app/LibraryCliSession.hpp"
#include "app/PlaybackController.hpp"
#include <algorithm>
#include <cstdint>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace OpenHDK {

// Serialized audio-enabled library session (OHK-CLI-030 step 5, slice 2). One
// control path owns one guarded LibraryCliSession and one PlaybackController
// over one AudioBackend. Playback verbs ("play SONG-ID", "stop", "status") are
// handled here, outside the management grammar; every management line is
// dispatched through the session whose mutation guard is bound to the
// controller, so library mutations are rejected while playback is active or a
// backend stop is unconfirmed. Playback never touches the store; responses
// follow the existing JSON Lines conventions and never claim success when the
// backend refused or failed.
class LibraryAudioSession {
public:
  LibraryAudioSession(LibraryCliSession& session, PlaybackController& controller)
      : session_(session), controller_(controller), service_(session.service()) {}

  // One control-thread line. Playback verbs are handled locally; all other
  // lines go to the guarded management session.
  [[nodiscard]] LibraryCliResponse dispatch(std::string_view line) {
    if (line == "stop") return stopLine();
    if (line == "status") return statusLine();
    if (line.rfind("play ", 0) == 0) return playLine(line.substr(5));
    return session_.dispatch(line);
  }

  // Same delivery semantics as the management session loop. On exit, stop
  // playback before the caller closes the service. A failed stop retains the
  // prepared handle until the controller's ordered destruction/fallback.
  int run(std::istream& input, std::ostream& output) {
    int ordinary = 0;
    LibraryCliLineReader reader;
    const auto deliver = [&](LibraryCliResponse r) {
      if (!r.output.bytes) return 74;
      try {
        output.write(r.output.bytes->data(),
            static_cast<std::streamsize>(r.output.bytes->size()));
        output.flush();
      } catch (const std::ios_base::failure&) { return 74; }
      if (!output) return 74;
      if (r.exit) ordinary = r.exit;
      return r.terminal ? (r.exit ? r.exit : ordinary) : -1;
    };
    try {
      char byte{};
      while (input.get(byte)) {
        const auto status = reader.push(byte);
        if (status == LibraryCliLineReader::Status::More) continue;
        auto response = status == LibraryCliLineReader::Status::Rejected
            ? session_.grammarError(*reader.error()) : dispatch(reader.line());
        const auto code = deliver(std::move(response));
        reader.reset();
        if (code >= 0) return finish(code);
      }
      if (input.bad() || (!input.eof() && input.fail())) return finish(74);
      const auto status = reader.end();
      if (status != LibraryCliLineReader::Status::More) {
        auto response = status == LibraryCliLineReader::Status::Rejected
            ? session_.grammarError(*reader.error()) : dispatch(reader.line());
        const auto code = deliver(std::move(response));
        if (code >= 0) return finish(code);
      }
      return finish(ordinary);
    } catch (const std::ios_base::failure&) { return finish(74); }
      catch (const std::bad_alloc&) { return finish(74); }
  }

private:
  // Prepares through the service's serialized read-only preparation and starts
  // playback with the backend's acknowledged generation. A backend refusal or
  // failure is reported as an error; nothing claims success.
  [[nodiscard]] LibraryCliResponse playLine(std::string_view argument) {
    const auto id = parseSongId(argument);
    if (!id) return lineError("InvalidSongId");
    const auto snapshot = service_.snapshot();
    const auto song = std::find_if(snapshot->songs.begin(), snapshot->songs.end(),
        [&](const auto& s) { return s.id.diagnosticValue() == *id; });
    if (song == snapshot->songs.end()) return lineError("NotFound");
    const auto prepared = service_.prepare(song->id, {}, {},
        LibraryCliSession::kCallerBytes);
    if (prepared.error) return lineError("PrepareFailed");
    const auto started = controller_.start(prepared.prepared);
    if (started.error != PlaybackController::StartError::None)
      return lineError(startErrorName(started.error));
    LibraryCliOutputBatch batch;
    LibraryCliJsonRecord record;
    record.text("type", "playing");
    record.number("song", *id);
    record.number("generation", started.acknowledgedGeneration);
    batch.add(record);
    return complete(std::move(batch));
  }

  [[nodiscard]] LibraryCliResponse stopLine() {
    const auto stopped = controller_.stop();
    if (stopped.error != PlaybackController::StopError::None) {
      LibraryCliJsonRecord row;
      row.text("type", "error");
      row.text("error", "StopUnconfirmed");
      row.text("message", stopped.message);
      row.number("commit", static_cast<std::uint64_t>(LibraryCliCommitOutcome::Rejected));
      return {row.finish(), LibraryCliCommitOutcome::Rejected, 1, false};
    }
    LibraryCliOutputBatch batch;
    LibraryCliJsonRecord record;
    record.text("type", "stopped");
    batch.add(record);
    return complete(std::move(batch));
  }

  // Exactly one committed clock read per status request, on this thread.
  [[nodiscard]] LibraryCliResponse statusLine() {
    const auto poll = controller_.pollDisplay();
    LibraryCliOutputBatch batch;
    LibraryCliJsonRecord record;
    record.text("type", "status");
    record.boolean("active", controller_.playbackActive());
    record.text("poll", pollStatusName(poll.status));
    if (controller_.expectedGeneration())
      record.number("generation", *controller_.expectedGeneration());
    else record.null("generation");
    record.number("observed_cues", controller_.observed().cues().size());
    batch.add(record);
    return complete(std::move(batch));
  }

  [[nodiscard]] static std::optional<std::uint64_t> parseSongId(
      std::string_view text) noexcept {
    if (text.empty()) return std::nullopt;
    std::uint64_t value = 0U;
    for (const char c : text) {
      if (c < '0' || c > '9') return std::nullopt;
      const auto digit = static_cast<std::uint64_t>(c - '0');
      if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U)
        return std::nullopt;
      value = value * 10U + digit;
    }
    return value;
  }

  [[nodiscard]] static std::string_view startErrorName(
      PlaybackController::StartError error) noexcept {
    switch (error) {
    case PlaybackController::StartError::AlreadyActive: return "AlreadyPlaying";
    case PlaybackController::StartError::InvalidPrepared: return "InvalidSong";
    case PlaybackController::StartError::BackendRejected: return "BackendRejected";
    case PlaybackController::StartError::Failed: return "StartFailed";
    case PlaybackController::StartError::BindingFailed: return "BindingFailed";
    case PlaybackController::StartError::StopFailed: return "StopUnconfirmed";
    case PlaybackController::StartError::None: break;
    }
    return "StartFailed";
  }

  [[nodiscard]] static std::string_view pollStatusName(
      LyricClockPollStatus status) noexcept {
    switch (status) {
    case LyricClockPollStatus::Unbound: return "Unbound";
    case LyricClockPollStatus::Held: return "Held";
    case LyricClockPollStatus::Advanced: return "Advanced";
    case LyricClockPollStatus::Cleared: return "Cleared";
    case LyricClockPollStatus::Exhausted: return "Exhausted";
    case LyricClockPollStatus::InvalidSnapshot: return "InvalidSnapshot";
    case LyricClockPollStatus::BackwardPosition: return "BackwardPosition";
    }
    return "InvalidSnapshot";
  }

  // Structured reject for playback-verb problems; the store holds no commit.
  [[nodiscard]] LibraryCliResponse lineError(std::string_view error) {
    LibraryCliJsonRecord row;
    row.text("type", "error");
    row.text("error", error);
    row.number("commit",
        static_cast<std::uint64_t>(LibraryCliCommitOutcome::Rejected));
    return {row.finish(), LibraryCliCommitOutcome::Rejected, 1, false};
  }

  [[nodiscard]] LibraryCliResponse complete(LibraryCliOutputBatch&& batch) {
    auto output = batch.finish();
    if (output.error) return lineError("LimitExceeded");
    return {std::move(output), LibraryCliCommitOutcome::Unknown, 0, false};
  }

  int finish(int code) {
    if (closed_) return code;
    closed_ = true;
    try {
      const auto stopped = controller_.stop();
      if (stopped.error != PlaybackController::StopError::None && code == 0)
        return 1; // Never report a clean exit for an unconfirmed stop.
    } catch (const std::bad_alloc&) {
      return 74;
    }
    return code;
  }

  LibraryCliSession& session_;
  PlaybackController& controller_;
  DurableLibraryService& service_;
  bool closed_{false};
};

// Backend initialization for the audio-enabled entry: a failure is a clear
// failure (the caller exits with a message) and never degrades silently.
[[nodiscard]] inline bool initializeAudioBackend(
    const std::filesystem::path& soundFont, AudioBackend& backend,
    std::string& failure) {
  AudioBackendConfig configuration;
  configuration.soundFontPath = soundFont;
  AudioBackendStatus status;
  if (backend.initialize(configuration, status)) return true;
  failure = status.message;
  return false;
}

} // namespace OpenHDK