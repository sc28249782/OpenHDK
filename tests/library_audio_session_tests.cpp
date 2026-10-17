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
  return 0;
}