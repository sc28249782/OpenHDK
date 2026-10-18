// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
// Hardware-free integration tests for the audio-enabled library session loop
// (OHK-CLI-030 step 5, slice 2): a real DurableLibraryService on temporary
// Linux store directories, the guarded management session, the playback
// controller over the shared fake backend, and synthetic in-memory SMF/KAR
// fixtures. No player launches and no audio hardware is used.
#include "app/LibraryAudioSession.hpp"
#include "tests/PlaybackTestBackend.hpp"
#include "tests/TestCheck.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace fs = std::filesystem;
using namespace OpenHDK;
using Access = DurableLibraryServiceTestAccess;
using FakeBackend = OpenHDK::Test::PlaybackTestBackend;
namespace {
struct TempDir {
  fs::path path = fs::temp_directory_path() / ("openhdk-audio-session-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
      "-" + std::to_string(::getpid()));
  TempDir() { if (!fs::create_directory(path)) throw std::runtime_error("fixture directory failed"); }
  ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
};
using Bytes = std::vector<std::uint8_t>;
Bytes smf(const Bytes& track) {
  Bytes bytes{'M','T','h','d',0,0,0,6,0,0,0,1,0,3,'M','T','r','k'};
  for (auto shift : {24U,16U,8U,0U}) bytes.push_back(static_cast<std::uint8_t>(track.size() >> shift));
  bytes.insert(bytes.end(), track.begin(), track.end());
  return bytes;
}
void write(const fs::path& path, const Bytes& bytes) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!f) throw std::runtime_error("fixture write failed");
}
[[nodiscard]] bool has(const LibraryCliResponse& r, std::string_view text) {
  if (!r.output.bytes) return false;
  return std::string_view(*r.output.bytes).find(text) != std::string_view::npos;
}
// First song id from a songs listing; ids are JSON numbers after "id":.
[[nodiscard]] std::uint64_t firstSongId(const LibraryCliResponse& listing) {
  const std::string_view text(*listing.output.bytes);
  const auto key = text.find("\"id\":");
  if (key == std::string_view::npos) throw std::runtime_error("no song id in listing");
  std::size_t index = key + 5;
  std::uint64_t value = 0U;
  while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
    value = value * 10U + static_cast<std::uint64_t>(text[index] - '0');
    ++index;
  }
  return value;
}
struct StreamResult {
  int exit{};
  std::string output;
};
// A fresh synthetic store for each stdin/stdout case; no audio device or
// external media is opened.
StreamResult runStream(const fs::path& home, std::string_view name,
    const std::string& input, bool failOutput = false) {
  const auto store = home / std::string(name);
  fs::create_directory(store);
  auto opened = Access::createNative(std::make_unique<SongDiscovery>(),
      store.string(), "stream.ohkcat", {}, {}, 1024U);
  if (!opened.succeeded() || !opened.service)
    throw std::runtime_error("stream fixture failed");
  FakeBackend backend;
  PlaybackController* active = nullptr;
  LibraryCliSession session(std::move(opened.service),
      [&active] { return active != nullptr && active->libraryMutationAllowed(); });
  StreamResult result;
  {
    PlaybackController controller(backend);
    active = &controller;
    LibraryAudioSession audio(session, controller);
    std::istringstream in(input);
    std::ostringstream out;
    if (failOutput) out.setstate(std::ios::badbit);
    result.exit = audio.run(in, out);
    result.output = out.str();
  }
  static_cast<void>(session.service().close());
  return result;
}
} // namespace

// Session-level scripted clock backend proving the ONE-read-per-status
// guarantee: status diagnostics and observed_cues must come from the first
// committed read; the next status consumes the next scripted read.
class ScriptedSessionBackend final : public OpenHDK::Test::PlaybackTestBackend {
public:
  mutable std::uint64_t reads{0U};
  std::vector<MediaClockReadResult> script;
  [[nodiscard]] MediaClockReadResult mediaClock() const noexcept override {
    if (script.empty()) return {MediaClockReadStatus::Unstable, std::nullopt};
    const auto index = reads < script.size() ? reads : script.size() - 1U;
    ++reads;
    return script[static_cast<std::size_t>(index)];
  }
};

[[nodiscard]] MediaClockReadResult sessionClockRead(std::uint64_t generation,
    std::uint64_t microseconds, MediaClockPhase phase, std::uint64_t revision) {
  MediaClockPayload payload;
  payload.generation = generation;
  payload.mediaMicroseconds = microseconds;
  payload.source = MediaClockSource::CompiledTimeline;
  payload.phase = phase;
  payload.failure = MediaClockFailure::None;
  MediaClockSnapshot result;
  static_cast<MediaClockPayload&>(result) = payload;
  result.revision = revision;
  return MediaClockReadResult{MediaClockReadStatus::Snapshot, result};
}

// Backend that can return an Unstable read WITH a supplied payload, to prove
// the status layer nulls every payload field unless the read is a Snapshot.
class UnstablePayloadBackend final : public OpenHDK::Test::PlaybackTestBackend {
public:
  bool unstablePayload{false};
  [[nodiscard]] MediaClockReadResult mediaClock() const noexcept override {
    if (unstablePayload) {
      MediaClockPayload payload;
      payload.generation = 1U;
      payload.mediaMicroseconds = 4000U;
      payload.source = MediaClockSource::CompiledTimeline;
      payload.phase = MediaClockPhase::Playing;
      payload.failure = MediaClockFailure::None;
      MediaClockSnapshot snapshot;
      static_cast<MediaClockPayload&>(snapshot) = payload;
      snapshot.revision = 99U;
      return {MediaClockReadStatus::Unstable, snapshot};
    }
    return OpenHDK::Test::PlaybackTestBackend::mediaClock();
  }
};
int main() {
  TempDir home;
  const fs::path store = home.path / "store", songs = home.path / "songs";
  fs::create_directory(store);
  fs::create_directory(songs);
  write(songs / "song.kar",
      smf({0,0xff,5,1,'A',1,0xff,5,1,'B',0,0xff,0x2f,0}));
  auto opened = Access::createNative(std::make_unique<SongDiscovery>(),
      store.string(), "primary.ohkcat", {}, {}, 1024U);
  OPENHDK_FAIL_IF(1, !opened.succeeded() || !opened.service);
  FakeBackend backend;
  PlaybackController controller(backend);
  LibraryCliSession session(std::move(opened.service),
      [&controller] { return controller.libraryMutationAllowed(); });
  LibraryAudioSession audio(session, controller);
  // Root registration and scan flow through the guarded session normally.
  const auto registered = audio.dispatch("register \"" + songs.generic_string() + "\"");
  OPENHDK_FAIL_IF(2, registered.exit != 0 || !has(registered, "\"type\":\"commit\""));
  const auto scanned = audio.dispatch("scan 1");
  OPENHDK_FAIL_IF(3, scanned.exit != 0 || !has(scanned, "\"candidates\":1"));
  const auto listing = audio.dispatch("songs 1 0 100");
  OPENHDK_FAIL_IF(4, listing.exit != 0 || !has(listing, "\"type\":\"song\""));
  const auto songId = firstSongId(listing);
  // Idle status: no playback, the clock was never published.
  const auto idle = audio.dispatch("status");
  OPENHDK_FAIL_IF(5, idle.exit == 0 && has(idle, "\"active\":true"));
  OPENHDK_FAIL_IF(6, !has(idle, "\"type\":\"status\"") || !has(idle, "\"poll\":\"Unbound\"")
      || !has(idle, "\"active\":false"));
  // Playback starts with the backend's acknowledged generation.
  const auto playing = audio.dispatch("play " + std::to_string(songId));
  OPENHDK_FAIL_IF(7, playing.exit != 0 || !has(playing, "\"type\":\"playing\"")
      || !has(playing, "\"generation\":1") || !controller.playbackActive()
      || controller.libraryMutationAllowed());
  // A mutating management verb is rejected with a structured PlaybackActive
  // error and the store is untouched.
  const auto catalogRevision = session.service().snapshot()->revision;
  const auto rejected = audio.dispatch("overrides " + std::to_string(songId) + " T -");
  OPENHDK_FAIL_IF(8, rejected.exit != 1 || !has(rejected, "\"error\":\"PlaybackActive\"")
      || session.service().snapshot()->revision != catalogRevision);
  // A second start is rejected while the first binding is preserved.
  const auto again = audio.dispatch("play " + std::to_string(songId));
  OPENHDK_FAIL_IF(9, again.exit != 1 || !has(again, "\"error\":\"AlreadyPlaying\"")
      || controller.expectedGeneration() != 1U);
  // Queries remain available during playback.
  OPENHDK_FAIL_IF(10, audio.dispatch("songs 1 0 100").exit != 0);
  // A confirmed stop releases the guard and mutations are admitted again.
  const auto stopped = audio.dispatch("stop");
  OPENHDK_FAIL_IF(11, stopped.exit != 0 || !has(stopped, "\"type\":\"stopped\"")
      || controller.playbackActive() || !controller.libraryMutationAllowed());
  const auto admitted = audio.dispatch("overrides " + std::to_string(songId) + " T -");
  OPENHDK_FAIL_IF(12, admitted.exit != 0 || !has(admitted, "\"type\":\"commit\""));
  // Stopping while idle is a no-op success and touches nothing.
  const auto confirmedStopCount = backend.stopCount;
  const auto idleStop = audio.dispatch("stop");
  OPENHDK_FAIL_IF(13, idleStop.exit != 0 || !has(idleStop, "\"type\":\"stopped\"")
      || backend.stopCount != confirmedStopCount);
  // Invalid play arguments and unknown songs are structured rejects.
  const auto invalid = audio.dispatch("play abc");
  OPENHDK_FAIL_IF(14, invalid.exit != 1 || !has(invalid, "\"error\":\"InvalidSongId\""));
  const auto missing = audio.dispatch("play 999");
  OPENHDK_FAIL_IF(15, missing.exit != 1 || !has(missing, "\"error\":\"NotFound\""));
  // A backend start refusal never reports success and keeps mutations allowed.
  backend.failNextStart = true;
  const auto refused = audio.dispatch("play " + std::to_string(songId));
  OPENHDK_FAIL_IF(17, refused.exit != 1 || !has(refused, "\"error\":\"BackendRejected\"")
      || !controller.libraryMutationAllowed());
  OPENHDK_FAIL_IF(18, audio.dispatch("overrides " + std::to_string(songId) + " T -").exit != 0);
  const auto restarted = audio.dispatch("play " + std::to_string(songId));
  OPENHDK_FAIL_IF(19, restarted.exit != 0 || !has(restarted, "\"type\":\"playing\"")
      || !has(restarted, "\"generation\":2") || !controller.playbackActive());
  // An unconfirmed stop keeps the guard closed until a retry confirms.
  backend.failNextStop = true;
  const auto blocked = audio.dispatch("stop");
  OPENHDK_FAIL_IF(20, blocked.exit != 1 || !has(blocked, "\"error\":\"StopUnconfirmed\"")
      || !has(blocked, "\"message\":\"rejected stop\"") || !controller.playbackActive()
      || controller.libraryMutationAllowed());
  OPENHDK_FAIL_IF(21, audio.dispatch("overrides " + std::to_string(songId) + " T -").exit != 1);
  const auto retry = audio.dispatch("stop");
  OPENHDK_FAIL_IF(22, retry.exit != 0 || !has(retry, "\"type\":\"stopped\"")
      || !controller.libraryMutationAllowed());
  OPENHDK_FAIL_IF(23, audio.dispatch("overrides " + std::to_string(songId) + " T -").exit != 0);
  // The prepared handle is released by the confirmed ordered stop path.
  OPENHDK_FAIL_IF(24, controller.playbackActive());
  // Backend initialization failure is a clear failure with a message.
  FakeBackend failed;
  failed.failNextInitialize = true;
  std::string initFailure;
  OPENHDK_FAIL_IF(25, initializeAudioBackend("unused.soundfont", failed, initFailure)
      || initFailure.empty());
  std::string okFailure;
  OPENHDK_FAIL_IF(26, !initializeAudioBackend("unused.soundfont", backend, okFailure)
      || !okFailure.empty());
  // Quit while active performs the ordered stop while the service is ready.
  const auto finalStart = audio.dispatch("play " + std::to_string(songId));
  bool serviceReadyAtStop = false;
  backend.stopHook = [&] {
    serviceReadyAtStop = session.service().state() == DurableServiceState::Ready;
  };
  std::istringstream quitInput("quit\nstatus\n");
  std::ostringstream quitOutput;
  const auto quitExit = audio.run(quitInput, quitOutput);
  OPENHDK_FAIL_IF(27, finalStart.exit != 0 || quitExit != 0
      || !serviceReadyAtStop || controller.playbackActive()
      || quitOutput.str().find("\"type\":\"status\"") != std::string::npos
      || quitOutput.str().find("\"type\":\"quit\"") == std::string::npos);
  static_cast<void>(session.service().close());

  const auto finalLine = runStream(home.path, "final-line", "status");
  OPENHDK_FAIL_IF(28, finalLine.exit != 0
      || finalLine.output.find("\"type\":\"status\"") == std::string::npos
      || finalLine.output.empty() || finalLine.output.back() != '\n');
  const auto oversized = runStream(home.path, "oversized",
      std::string(kLibraryCliLineBytes + 1U, 'x') + "\nquit\n");
  OPENHDK_FAIL_IF(29, oversized.exit != 1
      || oversized.output.find("\"family\":\"grammar\"") == std::string::npos
      || oversized.output.find("\"type\":\"quit\"") == std::string::npos);
  const auto terminal = runStream(home.path, "terminal", "quit\nstatus\n");
  OPENHDK_FAIL_IF(30, terminal.exit != 0
      || terminal.output.find("\"type\":\"quit\"") == std::string::npos
      || terminal.output.find("\"type\":\"status\"") != std::string::npos);
  const auto outputLoss = runStream(home.path, "output-loss", "status\n", true);
  OPENHDK_FAIL_IF(31, outputLoss.exit != 74);
  // 32-47: additive flat clock diagnostics in status. The same committed read
  // drives lyric observation and the diagnostics; legacy fields unchanged.
  {
    const fs::path store2 = home.path / "clockdiag";
    fs::create_directory(store2);
    auto opened2 = Access::createNative(std::make_unique<SongDiscovery>(),
        store2.string(), "clockdiag.ohkcat", {}, {}, 1024U);
    OPENHDK_FAIL_IF(32, !opened2.succeeded() || !opened2.service);
    FakeBackend backend2;
    PlaybackController controller2(backend2);
    LibraryCliSession session2(std::move(opened2.service),
        [&controller2] { return controller2.libraryMutationAllowed(); });
    LibraryAudioSession audio2(session2, controller2);
    OPENHDK_FAIL_IF(33, audio2.dispatch("register \"" + songs.generic_string()
        + "\"").exit != 0);
    OPENHDK_FAIL_IF(34, audio2.dispatch("scan 1").exit != 0);
    const auto listing2 = audio2.dispatch("songs 1 0 100");
    OPENHDK_FAIL_IF(35, listing2.exit != 0);
    const auto songId2 = firstSongId(listing2);
    // Idle: the fresh cell yields a revision-0 snapshot; reported raw.
    const auto idle2 = audio2.dispatch("status");
    OPENHDK_FAIL_IF(36, !has(idle2, "\"clock_read\":\"Snapshot\"")
        || !has(idle2, "\"clock_phase\":\"Unavailable\"")
        || has(idle2, "\"clock_media_us\":null"));
    OPENHDK_FAIL_IF(37, !backend2.publish(1U, 123456U, MediaClockPhase::Playing));
    OPENHDK_FAIL_IF(38, audio2.dispatch("play " + std::to_string(songId2)).exit
        != 0);
    const auto stPlaying = audio2.dispatch("status");
    OPENHDK_FAIL_IF(39, !has(stPlaying, "\"clock_read\":\"Snapshot\"")
        || !has(stPlaying, "\"clock_source\":\"CompiledTimeline\"")
        || !has(stPlaying, "\"clock_phase\":\"Playing\"")
        || !has(stPlaying, "\"clock_generation\":1")
        || !has(stPlaying, "\"clock_media_us\":123456")
        || has(stPlaying, "\"clock_revision\":null")
        || !has(stPlaying, "\"poll\":\"Advanced\"")
        || !has(stPlaying, "\"active\":true"));
    // Paused and Finished are directly distinguishable even though the
    // observer stays Advanced and the controller stays active.
    OPENHDK_FAIL_IF(40, !backend2.publish(1U, 234567U, MediaClockPhase::Paused));
    const auto stPaused = audio2.dispatch("status");
    OPENHDK_FAIL_IF(41, !has(stPaused, "\"clock_phase\":\"Paused\"")
        || !has(stPaused, "\"clock_media_us\":234567")
        || !has(stPaused, "\"poll\":\"Advanced\"")
        || !has(stPaused, "\"active\":true"));
    OPENHDK_FAIL_IF(42, !backend2.publish(1U, 345678U,
        MediaClockPhase::Finished));
    const auto stFinished = audio2.dispatch("status");
    OPENHDK_FAIL_IF(43, !has(stFinished, "\"clock_phase\":\"Finished\"")
        || !has(stFinished, "\"poll\":\"Advanced\"")
        || !has(stFinished, "\"active\":true"));
    backend2.forceUnstable = true;
    const auto stUnstable = audio2.dispatch("status");
    OPENHDK_FAIL_IF(44, !has(stUnstable, "\"clock_read\":\"Unstable\"")
        || !has(stUnstable, "\"clock_source\":null")
        || !has(stUnstable, "\"clock_phase\":null")
        || !has(stUnstable, "\"clock_failure\":null")
        || !has(stUnstable, "\"clock_generation\":null")
        || !has(stUnstable, "\"clock_revision\":null")
        || !has(stUnstable, "\"clock_media_us\":null")
        || !has(stUnstable, "\"poll\":\"Held\""));
    backend2.forceUnstable = false;
    backend2.forceExhausted = true;
    const auto stExhausted = audio2.dispatch("status");
    OPENHDK_FAIL_IF(45, !has(stExhausted, "\"clock_read\":\"Exhausted\"")
        || !has(stExhausted, "\"clock_media_us\":null")
        || !has(stExhausted, "\"poll\":\"Exhausted\"")
        || !has(stExhausted, "\"active\":true"));
    backend2.forceExhausted = false;
    const auto stopped2 = audio2.dispatch("stop");
    OPENHDK_FAIL_IF(46, stopped2.exit != 0
        || !has(stopped2, "\"type\":\"stopped\""));
    const auto stAfterStop = audio2.dispatch("status");
    OPENHDK_FAIL_IF(47, !has(stAfterStop, "\"active\":false")
        || !has(stAfterStop, "\"poll\":\"Unbound\"")
        || !has(stAfterStop, "\"generation\":null"));
  }
  // 48-53: an Unstable read that carries a supplied payload must still emit
  // every payload field null from the status layer.
  {
    const fs::path store3 = home.path / "clockdiag2";
    fs::create_directory(store3);
    auto opened3 = Access::createNative(std::make_unique<SongDiscovery>(),
        store3.string(), "clockdiag2.ohkcat", {}, {}, 1024U);
    OPENHDK_FAIL_IF(48, !opened3.succeeded() || !opened3.service);
    UnstablePayloadBackend backend3;
    PlaybackController controller3(backend3);
    LibraryCliSession session3(std::move(opened3.service),
        [&controller3] { return controller3.libraryMutationAllowed(); });
    LibraryAudioSession audio3(session3, controller3);
    OPENHDK_FAIL_IF(49, audio3.dispatch("register \"" + songs.generic_string()
        + "\"").exit != 0 || audio3.dispatch("scan 1").exit != 0);
    const auto listing3 = audio3.dispatch("songs 1 0 100");
    OPENHDK_FAIL_IF(50, listing3.exit != 0);
    const auto songId3 = firstSongId(listing3);
    OPENHDK_FAIL_IF(51, !backend3.publish(1U, 1000U, MediaClockPhase::Playing));
    OPENHDK_FAIL_IF(52, audio3.dispatch("play " + std::to_string(songId3)).exit
        != 0);
    backend3.unstablePayload = true;
    const auto stUnstablePayload = audio3.dispatch("status");
    OPENHDK_FAIL_IF(53, !has(stUnstablePayload, "\"clock_read\":\"Unstable\"")
        || !has(stUnstablePayload, "\"clock_source\":null")
        || !has(stUnstablePayload, "\"clock_phase\":null")
        || !has(stUnstablePayload, "\"clock_failure\":null")
        || !has(stUnstablePayload, "\"clock_generation\":null")
        || !has(stUnstablePayload, "\"clock_revision\":null")
        || !has(stUnstablePayload, "\"clock_media_us\":null")
        || !has(stUnstablePayload, "\"poll\":\"Held\"")
        || !has(stUnstablePayload, "\"active\":true"));
  }
  // 54-63: one committed read per status request. Status diagnostics and
  // observed_cues come from the FIRST read; each later status consumes the
  // next scripted read. Fixture song.kar holds cues at 0/166666 microseconds
  // (indices 0,1); raw clock_generation is reported separately from the
  // observer's expected generation.
  {
    const fs::path store4 = home.path / "clockdiag3";
    fs::create_directory(store4);
    auto opened4 = Access::createNative(std::make_unique<SongDiscovery>(),
        store4.string(), "clockdiag3.ohkcat", {}, {}, 1024U);
    OPENHDK_FAIL_IF(54, !opened4.succeeded() || !opened4.service);
    ScriptedSessionBackend backend4;
    backend4.script.push_back(sessionClockRead(1U, 0U, MediaClockPhase::Playing,
        2U));
    backend4.script.push_back(sessionClockRead(1U, 166666U,
        MediaClockPhase::Paused, 4U));
    PlaybackController controller4(backend4);
    LibraryCliSession session4(std::move(opened4.service),
        [&controller4] { return controller4.libraryMutationAllowed(); });
    LibraryAudioSession audio4(session4, controller4);
    OPENHDK_FAIL_IF(55, audio4.dispatch("register \"" + songs.generic_string()
        + "\"").exit != 0 || audio4.dispatch("scan 1").exit != 0);
    const auto listing4 = audio4.dispatch("songs 1 0 100");
    OPENHDK_FAIL_IF(56, listing4.exit != 0);
    const auto songId4 = firstSongId(listing4);
    OPENHDK_FAIL_IF(57, audio4.dispatch("play " + std::to_string(songId4)).exit
        != 0);
    const auto stFirst = audio4.dispatch("status");
    OPENHDK_FAIL_IF(58, !has(stFirst, "\"clock_read\":\"Snapshot\"")
        || !has(stFirst, "\"clock_phase\":\"Playing\"")
        || !has(stFirst, "\"clock_generation\":1")
        || !has(stFirst, "\"clock_revision\":2")
        || !has(stFirst, "\"clock_media_us\":0")
        || !has(stFirst, "\"observed_cues\":1")
        || !has(stFirst, "\"poll\":\"Advanced\"")
        || backend4.reads != 1U);
    const auto stSecond = audio4.dispatch("status");
    OPENHDK_FAIL_IF(59, !has(stSecond, "\"clock_phase\":\"Paused\"")
        || !has(stSecond, "\"clock_media_us\":166666")
        || !has(stSecond, "\"clock_revision\":4")
        || !has(stSecond, "\"observed_cues\":2")
        || !has(stSecond, "\"poll\":\"Advanced\"")
        || backend4.reads != 2U);
    // Raw generation passes through even when it mismatches the observer's
    // expected generation: diagnostics stay raw, observer clears.
    backend4.script.push_back(sessionClockRead(2U, 200000U,
        MediaClockPhase::Finished, 6U));
    const auto stMismatch = audio4.dispatch("status");
    OPENHDK_FAIL_IF(60, !has(stMismatch, "\"clock_generation\":2")
        || !has(stMismatch, "\"clock_phase\":\"Finished\"")
        || !has(stMismatch, "\"poll\":\"Cleared\"")
        || has(stMismatch, "\"clock_generation\":null")
        || backend4.reads != 3U);
    // Snapshot WITHOUT a snapshot: read status still Snapshot, observer
    // InvalidSnapshot, every payload field null.
    backend4.script.push_back(
        MediaClockReadResult{MediaClockReadStatus::Snapshot, std::nullopt});
    const auto stNullPayload = audio4.dispatch("status");
    OPENHDK_FAIL_IF(61, !has(stNullPayload, "\"clock_read\":\"Snapshot\"")
        || !has(stNullPayload, "\"clock_source\":null")
        || !has(stNullPayload, "\"clock_phase\":null")
        || !has(stNullPayload, "\"clock_failure\":null")
        || !has(stNullPayload, "\"clock_generation\":null")
        || !has(stNullPayload, "\"clock_revision\":null")
        || !has(stNullPayload, "\"clock_media_us\":null")
        || !has(stNullPayload, "\"poll\":\"InvalidSnapshot\"")
        || backend4.reads != 4U);
    // Exhausted WITH a supplied malformed payload: still all six null.
    MediaClockPayload payload5;
    payload5.generation = 1U;
    payload5.mediaMicroseconds = 4000U;
    payload5.source = MediaClockSource::CompiledTimeline;
    payload5.phase = MediaClockPhase::Playing;
    payload5.failure = MediaClockFailure::None;
    MediaClockSnapshot snapshot5;
    static_cast<MediaClockPayload&>(snapshot5) = payload5;
    snapshot5.revision = 99U;
    backend4.script.push_back(
        MediaClockReadResult{MediaClockReadStatus::Exhausted, snapshot5});
    const auto stExhaustedPayload = audio4.dispatch("status");
    OPENHDK_FAIL_IF(62, !has(stExhaustedPayload, "\"clock_read\":\"Exhausted\"")
        || !has(stExhaustedPayload, "\"clock_source\":null")
        || !has(stExhaustedPayload, "\"clock_phase\":null")
        || !has(stExhaustedPayload, "\"clock_failure\":null")
        || !has(stExhaustedPayload, "\"clock_generation\":null")
        || !has(stExhaustedPayload, "\"clock_revision\":null")
        || !has(stExhaustedPayload, "\"clock_media_us\":null")
        || !has(stExhaustedPayload, "\"poll\":\"Exhausted\"")
        || backend4.reads != 5U);
    // Out-of-contract enum value falls back to Unknown in the name field.
    MediaClockPayload payload6;
    payload6.generation = 1U;
    payload6.mediaMicroseconds = 5000U;
    payload6.source = static_cast<MediaClockSource>(99U);
    payload6.phase = MediaClockPhase::Playing;
    payload6.failure = MediaClockFailure::None;
    MediaClockSnapshot snapshot6;
    static_cast<MediaClockPayload&>(snapshot6) = payload6;
    snapshot6.revision = 98U;
    backend4.script.push_back(
        MediaClockReadResult{MediaClockReadStatus::Snapshot, snapshot6});
    const auto stUnknown = audio4.dispatch("status");
    OPENHDK_FAIL_IF(63, !has(stUnknown, "\"clock_source\":\"Unknown\"")
        || !has(stUnknown, "\"clock_phase\":\"Playing\"")
        || backend4.reads != 6U);
    // Stop contract unchanged after all cleared/exhausted reads.
    const auto stopped4 = audio4.dispatch("stop");
    OPENHDK_FAIL_IF(64, stopped4.exit != 0
        || !has(stopped4, "\"type\":\"stopped\""));
  }
  // Pure helper: FF 05 literal text with CRLF -> preview "A\nB", false.
  {
    KarLyricCue cue;
    cue.decoded = "A\r\nB";
    cue.operations = {
        {LyricDisplayKind::Text, 0U, 1U},
        {LyricDisplayKind::LineBreak, 1U, 0U},
        {LyricDisplayKind::Text, 3U, 1U}};
    const auto r = lyricObservationText({&cue, 1});
    OPENHDK_FAIL_IF(65, r.failed || r.truncated || r.text != "A\nB");
  }
  // Named FF 01 leading markers: '/' line break, '\' paragraph break; the
  // marker byte itself is never emitted. Action-only cues still emit breaks.
  {
    KarLyricCue cue;
    cue.decoded = "/\\X";
    cue.operations = {
        {LyricDisplayKind::LineBreak, 0U, 0U},
        {LyricDisplayKind::ParagraphBreak, 1U, 0U},
        {LyricDisplayKind::Text, 2U, 1U}};
    const auto r = lyricObservationText({&cue, 1});
    OPENHDK_FAIL_IF(66, r.failed || r.truncated || r.text != "\n\n\nX");
  }
  {
    KarLyricCue cue;
    cue.decoded = "/\\";
    cue.operations = {
        {LyricDisplayKind::LineBreak, 0U, 0U},
        {LyricDisplayKind::ParagraphBreak, 1U, 0U}};
    const auto r = lyricObservationText({&cue, 1});
    OPENHDK_FAIL_IF(67, r.failed || r.truncated || r.text != "\n\n\n");
  }
  // Tab/quote/backslash bytes pass through the helper byte-exactly.
  {
    KarLyricCue cue;
    cue.decoded = std::string("a\tb\"c\\d");
    cue.operations = {{LyricDisplayKind::Text, 0U, cue.decoded.size()}};
    const auto r = lyricObservationText({&cue, 1});
    OPENHDK_FAIL_IF(68, r.failed || r.truncated
        || r.text != std::string("a\tb\"c\\d"));
  }
  // Thai scalars (3 UTF-8 bytes each): exact fit stays untruncated, and
  // clipping always backs off to a whole scalar boundary.
  {
    KarLyricCue cue;
    cue.decoded = "กก"; // 6 bytes
    cue.operations = {{LyricDisplayKind::Text, 0U, 6U}};
    const auto exact = lyricObservationText({&cue, 1},
        {6U, 1024U, 4096U});
    OPENHDK_FAIL_IF(69, exact.failed || exact.truncated || exact.text != "กก");
    const auto clipped = lyricObservationText({&cue, 1},
        {5U, 1024U, 4096U});
    OPENHDK_FAIL_IF(70, clipped.failed || !clipped.truncated
        || clipped.text != "ก");
  }
  // Operation and cue caps stop traversal immediately and mark truncated.
  {
    KarLyricCue a, b, c;
    a.decoded = "x"; b.decoded = "y"; c.decoded = "z";
    a.operations = {{LyricDisplayKind::Text, 0U, 1U}};
    b.operations = {{LyricDisplayKind::Text, 0U, 1U},
        {LyricDisplayKind::LineBreak, 1U, 0U}};
    c.operations = {{LyricDisplayKind::Text, 0U, 1U}};
    const KarLyricCue cues[] = {a, b, c};
    const auto r = lyricObservationText(cues, {4096U, 2U});
    OPENHDK_FAIL_IF(71, r.failed || !r.truncated || r.text != "xy\n");
    const auto r2 = lyricObservationText(cues, {5U, 1024U, 2U});
    OPENHDK_FAIL_IF(72, r2.failed || !r2.truncated || r2.text != "xy");
  }
  // Defensive bounds: out-of-range text ops and unknown kinds fail with no
  // text and no guessed bytes. Limit hard caps: raising above the contract
  // maximum, or zeroing a limit, fails out of band with no scanning at all;
  // a lowered but positive limit is honored (exact lower fit stays clean).
  {
    KarLyricCue cue;
    cue.decoded = "A";
    cue.operations = {{LyricDisplayKind::Text, 2U, 2U}};
    const auto r = lyricObservationText({&cue, 1});
    OPENHDK_FAIL_IF(73, !r.failed || !r.text.empty());
    KarLyricCue bad;
    bad.decoded = "A";
    bad.operations = {{static_cast<LyricDisplayKind>(77U), 0U, 1U}};
    const auto r2 = lyricObservationText({&bad, 1});
    OPENHDK_FAIL_IF(74, !r2.failed || !r2.text.empty());
    const auto aboveMax = lyricObservationText({&cue, 1},
        {kLyricObservationMaxOutputBytes + 1U, 1024U, 4096U});
    OPENHDK_FAIL_IF(101, !aboveMax.failed || !aboveMax.text.empty()
        || aboveMax.truncated);
    const auto zeroed = lyricObservationText({&cue, 1},
        {1U, 1024U, 0U});
    OPENHDK_FAIL_IF(102, !zeroed.failed || !zeroed.text.empty()
        || zeroed.truncated);
    // A lowered positive output cap that exactly fits REAL valid content
    // stays clean (correct oracle: a good cue, not the invalid one above).
    KarLyricCue good;
    good.decoded = "AB";
    good.operations = {{LyricDisplayKind::Text, 0U, 2U}};
    const KarLyricCue one[] = {good};
    const auto lowered = lyricObservationText(one,
        {2U, 1024U, 4096U});
    OPENHDK_FAIL_IF(103, lowered.failed || lowered.truncated
        || lowered.text != "AB");
  }
  // Assembled-preview UTF-8 validation: an in-bounds operation that STARTS
  // inside a multi-byte scalar can only ever fail; it must not return a
  // partial "success" with a split scalar.
  {
    KarLyricCue cue;
    cue.decoded = "\xe0\xb8\x81"; // Thai ก (3 bytes)
    cue.operations = {{LyricDisplayKind::Text, 1U, 2U}};
    const auto r = lyricObservationText({&cue, 1});
    OPENHDK_FAIL_IF(104, !r.failed || !r.text.empty());
  }
  // Multi-byte scalar truncation with explicit byte literals.
  // U+00A2 (CENT SIGN, \xc2\xa2) twice = 4 bytes; cap 3 keeps one cent and
  // reports truncated.
  {
    KarLyricCue cue;
    cue.decoded = std::string("\xc2\xa2\xc2\xa2");
    cue.operations = {{LyricDisplayKind::Text, 0U, 4U}};
    const auto r = lyricObservationText({&cue, 1}, {3U, 1024U, 4096U});
    OPENHDK_FAIL_IF(105, r.failed || !r.truncated
        || r.text != std::string("\xc2\xa2"));
  }
  // U+1F600 twice (\xf0\x9f\x98\x80) = 8 bytes; cap 7 keeps one emoji
  // (the incomplete second 4-byte scalar is dropped whole); exact cap 8
  // keeps both untruncated.
  {
    KarLyricCue cue;
    cue.decoded = std::string("\xf0\x9f\x98\x80\xf0\x9f\x98\x80");
    cue.operations = {{LyricDisplayKind::Text, 0U, 8U}};
    const auto clipped = lyricObservationText({&cue, 1},
        {7U, 1024U, 4096U});
    OPENHDK_FAIL_IF(106, clipped.failed || !clipped.truncated
        || clipped.text != std::string("\xf0\x9f\x98\x80"));
    const auto exact = lyricObservationText({&cue, 1},
        {8U, 1024U, 4096U});
    OPENHDK_FAIL_IF(107, exact.failed || exact.truncated
        || exact.text != std::string("\xf0\x9f\x98\x80\xf0\x9f\x98\x80"));
  }
  // ASCII: exact fit (4 bytes) stays clean; cap 3 clips to "abc" truncated.
  {
    KarLyricCue cue;
    cue.decoded = "abcd";
    cue.operations = {{LyricDisplayKind::Text, 0U, 4U}};
    const auto exact = lyricObservationText({&cue, 1}, {4U, 1024U, 4096U});
    OPENHDK_FAIL_IF(108, exact.failed || exact.truncated || exact.text != "abcd");
    const auto clipped = lyricObservationText({&cue, 1}, {3U, 1024U, 4096U});
    OPENHDK_FAIL_IF(109, clipped.failed || !clipped.truncated
        || clipped.text != "abc");
  }
  // Session-level: lyrics verb on the audio entry. ONE committed read per
  // lyrics request, same-clock prefix text, repeat query, Held/Finished
  // retention, and legacy status output unchanged. LF is escaped as \u000a
  // by the shared JSON writer, matching every other control character.
  {
    const fs::path store5 = home.path / "lyricsprobe";
    fs::create_directory(store5);
    const fs::path songs5 = home.path / "lyricsprobe-songs";
    fs::create_directory(songs5);
    write(songs5 / "lyricAB.kar",
        smf({0, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20,
             0, 0xff, 0x05, 4, 'A', '\r', '\n', 'B',
             3, 0xff, 0x05, 1, 'C',
             0, 0xff, 0x2f, 0}));
    auto opened5 = Access::createNative(std::make_unique<SongDiscovery>(),
        store5.string(), "lyrics.ohkcat", {}, {}, 1024U);
    OPENHDK_FAIL_IF(75, !opened5.succeeded() || !opened5.service);
    ScriptedSessionBackend backend5;
    backend5.script.push_back(sessionClockRead(1U, 0U,
        MediaClockPhase::Playing, 2U));
    backend5.script.push_back(sessionClockRead(1U, 1000000U,
        MediaClockPhase::Playing, 4U));
    PlaybackController controller5(backend5);
    LibraryCliSession session5(std::move(opened5.service),
        [&controller5] { return controller5.libraryMutationAllowed(); });
    LibraryAudioSession audio5(session5, controller5);
    OPENHDK_FAIL_IF(76, audio5.dispatch("register \"" +
        songs5.generic_string() + "\"").exit != 0
        || audio5.dispatch("scan 1").exit != 0);
    const auto listing5 = audio5.dispatch("songs 1 0 100");
    OPENHDK_FAIL_IF(77, listing5.exit != 0);
    const auto songId5 = firstSongId(listing5);
    OPENHDK_FAIL_IF(78, audio5.dispatch("play "
        + std::to_string(songId5)).exit != 0);
    const auto first = audio5.dispatch("lyrics");
    OPENHDK_FAIL_IF(79, first.exit != 0 || backend5.reads != 1U);
    OPENHDK_FAIL_IF(80, !has(first, "\"type\":\"lyrics\"")
        || !has(first, "\"active\":true")
        || !has(first, "\"poll\":\"Advanced\"")
        || !has(first, "\"generation\":1")
        || !has(first, "\"observed_cues\":1")
        || !has(first, "\"clock_read\":\"Snapshot\"")
        || !has(first, "\"clock_phase\":\"Playing\"")
        || !has(first, "\"clock_media_us\":0")
        || !has(first, "\"text\":\"A\\u000aB\"")
        || !has(first, "\"text_truncated\":false"));
    // Legacy status output is unchanged: still no text field, and each
    // status call continues to consume exactly one committed read.
    const auto legacy = audio5.dispatch("status");
    OPENHDK_FAIL_IF(81, !has(legacy, "\"type\":\"status\"")
        || !has(legacy, "\"observed_cues\":2")
        || has(legacy, "\"text\":")
        || backend5.reads != 2U);
    const auto second = audio5.dispatch("lyrics");
    OPENHDK_FAIL_IF(82, !has(second, "\"observed_cues\":2")
        || !has(second, "\"text\":\"A\\u000aBC\"")
        || !has(second, "\"text_truncated\":false")
        || backend5.reads != 3U);
    backend5.script.push_back(sessionClockRead(1U, 1000000U,
        MediaClockPhase::Playing, 4U));
    // Reading position unchanged -> same prefix, same text (never stale or
    // duplicated); every lyrics call still consumes exactly one read.
    const auto repeat = audio5.dispatch("lyrics");
    OPENHDK_FAIL_IF(83, !has(repeat, "\"text\":\"A\\u000aBC\"")
        || !has(repeat, "\"observed_cues\":2")
        || backend5.reads != 4U);
    // Held retains the prefix and the text for BOTH Held sources: an
    // Unstable read and a Preparing snapshot.
    backend5.script.push_back(
        MediaClockReadResult{MediaClockReadStatus::Unstable, std::nullopt});
    const auto held = audio5.dispatch("lyrics");
    OPENHDK_FAIL_IF(84, !has(held, "\"poll\":\"Held\"")
        || !has(held, "\"text\":\"A\\u000aBC\"")
        || !has(held, "\"observed_cues\":2")
        || backend5.reads != 5U);
    backend5.script.push_back(sessionClockRead(1U, 700000U,
        MediaClockPhase::Preparing, 6U));
    const auto heldPreparing = audio5.dispatch("lyrics");
    OPENHDK_FAIL_IF(85, !has(heldPreparing, "\"poll\":\"Held\"")
        || !has(heldPreparing, "\"clock_phase\":\"Preparing\"")
        || !has(heldPreparing, "\"text\":\"A\\u000aBC\"")
        || !has(heldPreparing, "\"observed_cues\":2")
        || backend5.reads != 6U);
    // Unstable WITH a supplied payload stays Held, all payload fields null.
    MediaClockPayload payload7;
    payload7.generation = 1U;
    payload7.mediaMicroseconds = 900000U;
    payload7.source = MediaClockSource::CompiledTimeline;
    payload7.phase = MediaClockPhase::Playing;
    payload7.failure = MediaClockFailure::None;
    MediaClockSnapshot snapshot7;
    static_cast<MediaClockPayload&>(snapshot7) = payload7;
    snapshot7.revision = 98U;
    backend5.script.push_back(
        MediaClockReadResult{MediaClockReadStatus::Unstable, snapshot7});
    const auto heldPayload = audio5.dispatch("lyrics");
    OPENHDK_FAIL_IF(86, !has(heldPayload, "\"poll\":\"Held\"")
        || !has(heldPayload, "\"clock_media_us\":null")
        || !has(heldPayload, "\"text\":\"A\\u000aBC\"")
        || backend5.reads != 7U);
    // A real Finished sample retains the prefix and its text.
    backend5.script.push_back(sessionClockRead(1U, 2000000U,
        MediaClockPhase::Finished, 6U));
    const auto finished = audio5.dispatch("lyrics");
    OPENHDK_FAIL_IF(87, !has(finished, "\"clock_phase\":\"Finished\"")
        || !has(finished, "\"poll\":\"Advanced\"")
        || !has(finished, "\"text\":\"A\\u000aBC\"")
        || !has(finished, "\"observed_cues\":2")
        || backend5.reads != 8U);
    // Generation mismatch on a later snapshot (observer still bound): cleared
    // prefix, empty text, with the raw clock generation reported in diagnostics.
    backend5.script.push_back(sessionClockRead(99U, 3000000U,
        MediaClockPhase::Playing, 8U));
    const auto mismatch = audio5.dispatch("lyrics");
    OPENHDK_FAIL_IF(88, !has(mismatch, "\"poll\":\"Cleared\"")
        || !has(mismatch, "\"clock_generation\":99")
        || !has(mismatch, "\"observed_cues\":0")
        || !has(mismatch, "\"text\":\"\""));
    // Exhausted read surfaces Exhausted (read-status checks precede the
    // unbound check) and clears the current prefix: empty text.
    backend5.script.push_back(
        MediaClockReadResult{MediaClockReadStatus::Exhausted, std::nullopt});
    const auto exhausted = audio5.dispatch("lyrics");
    OPENHDK_FAIL_IF(89, !has(exhausted, "\"poll\":\"Exhausted\"")
        || !has(exhausted, "\"observed_cues\":0")
        || !has(exhausted, "\"text\":\"\"")
        || backend5.reads != 10U);
    // Stop while active (a cleared display does not stop the controller):
    // idle Unbound with an empty current prefix.
    const auto stopped5 = audio5.dispatch("stop");
    OPENHDK_FAIL_IF(90, stopped5.exit != 0);
    backend5.script.push_back(sessionClockRead(1U, 3000000U,
        MediaClockPhase::Finished, 10U));
    const auto idle = audio5.dispatch("lyrics");
    OPENHDK_FAIL_IF(91, !has(idle, "\"active\":false")
        || !has(idle, "\"poll\":\"Unbound\"")
        || !has(idle, "\"generation\":null")
        || !has(idle, "\"observed_cues\":0")
        || !has(idle, "\"text\":\"\"")
        || !has(idle, "\"text_truncated\":false"));
  }
  // Composer escape path: DEL (0x7f) and C1 (U+009f) render as escaped JSON.
  {
    const fs::path store7 = home.path / "lyricsesc";
    fs::create_directory(store7);
    const fs::path songs7 = home.path / "lyricsesc-songs";
    fs::create_directory(songs7);
    write(songs7 / "esc.mid",
        smf({0, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20,
             0, 0xff, 0x05, 5, 'x', 0x7f, 0xc2, 0x9f, 'y',
             0, 0xff, 0x2f, 0}));
    auto opened7 = Access::createNative(std::make_unique<SongDiscovery>(),
        store7.string(), "esc.ohkcat", {}, {}, 1024U);
    OPENHDK_FAIL_IF(92, !opened7.succeeded() || !opened7.service);
    ScriptedSessionBackend backend7;
    backend7.script.push_back(sessionClockRead(1U, 0U,
        MediaClockPhase::Playing, 2U));
    PlaybackController controller7(backend7);
    LibraryCliSession session7(std::move(opened7.service),
        [&controller7] { return controller7.libraryMutationAllowed(); });
    LibraryAudioSession audio7(session7, controller7);
    OPENHDK_FAIL_IF(93, audio7.dispatch("register \"" +
        songs7.generic_string() + "\"").exit != 0
        || audio7.dispatch("scan 1").exit != 0);
    const auto listing7 = audio7.dispatch("songs 1 0 100");
    OPENHDK_FAIL_IF(94, listing7.exit != 0);
    const auto songId7 = firstSongId(listing7);
    OPENHDK_FAIL_IF(95, audio7.dispatch("play "
        + std::to_string(songId7)).exit != 0);
    const auto esc = audio7.dispatch("lyrics");
    OPENHDK_FAIL_IF(96, !has(esc, "\"text\":\"x\\u007f\\u009fy\"")
        || !has(esc, "\"text_truncated\":false"));
  }
  // NoLyrics: empty text with false truncation, disambiguated by the
  // existing observed_cues/poll fields.
  {
    const fs::path store6 = home.path / "nolyrics";
    fs::create_directory(store6);
    const fs::path songs6 = home.path / "nolyrics-songs";
    fs::create_directory(songs6);
    write(songs6 / "plain.mid",
        smf({0, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20,
             0, 0x90, 0x3c, 0x64,
             3, 0x80, 0x3c, 0x00,
             0, 0xff, 0x2f, 0}));
    auto opened6 = Access::createNative(std::make_unique<SongDiscovery>(),
        store6.string(), "nolyr.ohkcat", {}, {}, 1024U);
    OPENHDK_FAIL_IF(97, !opened6.succeeded() || !opened6.service);
    FakeBackend backend6;
    PlaybackController controller6(backend6);
    LibraryCliSession session6(std::move(opened6.service),
        [&controller6] { return controller6.libraryMutationAllowed(); });
    LibraryAudioSession audio6(session6, controller6);
    OPENHDK_FAIL_IF(98, audio6.dispatch("register \"" +
        songs6.generic_string() + "\"").exit != 0
        || audio6.dispatch("scan 1").exit != 0);
    const auto listing6 = audio6.dispatch("songs 1 0 100");
    const auto songId6 = firstSongId(listing6);
    OPENHDK_FAIL_IF(99, audio6.dispatch("play "
        + std::to_string(songId6)).exit != 0);
    const auto noLyrics = audio6.dispatch("lyrics");
    OPENHDK_FAIL_IF(100, !has(noLyrics, "\"type\":\"lyrics\"")
        || !has(noLyrics, "\"observed_cues\":0")
        || !has(noLyrics, "\"text\":\"\"")
        || !has(noLyrics, "\"text_truncated\":false"));
  }
  return 0;
}