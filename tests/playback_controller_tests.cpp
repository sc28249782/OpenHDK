// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
// Hardware-free PlaybackController tests (OHK-CLI-030 step 5, first slice).
// Synthetic in-memory SMF/KAR fixtures only; the FakeBackend implements the
// existing AudioBackend acknowledgement interface and drives the real
// MediaClockPublicationCell from the test side, like a backend callback would.
#include "app/PlaybackController.hpp"
#include "tests/PlaybackTestBackend.hpp"
#include "audio/SmfTimelineCompiler.hpp"
#include "library/SongDiscovery.hpp"
#include "tests/TestCheck.hpp"
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>
namespace fs = std::filesystem;
using namespace OpenHDK;
using FakeBackend = OpenHDK::Test::PlaybackTestBackend;
namespace {
struct TemporaryRoot {
  fs::path path = fs::temp_directory_path() / ("openhdk-playback-controller-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryRoot() { if (!fs::create_directory(path)) throw std::runtime_error("fixture directory failed"); }
  ~TemporaryRoot() { std::error_code ec; fs::remove_all(path, ec); }
};
using Bytes = std::vector<std::uint8_t>;
Bytes smf(const Bytes& track) {
  Bytes bytes{'M','T','h','d',0,0,0,6,0,0,0,1,0,3,'M','T','r','k'};
  for (auto shift : {24U,16U,8U,0U}) bytes.push_back(static_cast<std::uint8_t>(track.size() >> shift));
  bytes.insert(bytes.end(), track.begin(), track.end());
  return bytes;
}
// Four lyric cues at 0/166666/166666/333333 microseconds, then end-of-track.
Bytes midiWithLyrics() {
  const Bytes track{0,0xff,5,1,'A',1,0xff,5,1,'B',0,0xff,5,1,'C',1,0xff,5,1,'D',0,0xff,0x2f,0};
  return smf(track);
}
Bytes midiNoLyrics() { return smf({0,0xff,0x2f,0}); }
void write(const fs::path& path, const Bytes& bytes) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!f) throw std::runtime_error("fixture write failed");
}
std::shared_ptr<const PreparedSong> makePrepared(
    SongDiscovery& library, const std::shared_ptr<const CatalogSnapshot>& snapshot,
    std::string_view locatorPart) {
  for (const auto& song : snapshot->songs)
    if (song.locator().find(locatorPart) != std::string::npos) {
      const auto result = library.prepare(snapshot, song.id);
      if (!result.succeeded()) throw std::runtime_error("synthetic preparation failed");
      return result.prepared;
    }
  throw std::runtime_error("synthetic song not found");
}
std::vector<std::size_t> indices(const LyricCueBatch& batch) {
  std::vector<std::size_t> result;
  for (const auto& cue : batch.cues()) result.push_back(cue.position.sourceIndex);
  return result;
}
static_assert(!std::is_copy_constructible_v<PlaybackController>);
static_assert(noexcept(std::declval<PlaybackController>().pollDisplay()));
} // namespace
int main() {
  TemporaryRoot temp;
  write(temp.path / "song.kar", midiWithLyrics());
  write(temp.path / "instrumental.kar", midiNoLyrics());
  SongDiscovery library;
  const auto registered = library.registerRoot(temp.path);
  OPENHDK_FAIL_IF(1, !registered.root || !library.scan(*registered.root).succeeded());
  const auto snapshot = library.snapshot();
  const auto lyrics = makePrepared(library, snapshot, "song.kar");
  const auto noLyrics = makePrepared(library, snapshot, "instrumental.kar");
  const auto revision = snapshot->revision;
  // 1. Idle semantics: no playback, mutations allowed, no binding.
  FakeBackend backend1;
  PlaybackController controller1(backend1);
  OPENHDK_FAIL_IF(2, controller1.playbackActive() || !controller1.libraryMutationAllowed()
      || controller1.expectedGeneration() || controller1.preparedCatalogRevision());
  OPENHDK_FAIL_IF(3, !backend1.publish(5U, 0U)
      || controller1.pollDisplay().status != LyricClockPollStatus::Unbound);
  // 2. Start from idle: acknowledgement binding, identity retention, guard.
  OPENHDK_FAIL_IF(4, controller1.start(nullptr).error != PlaybackController::StartError::InvalidPrepared
      || controller1.playbackActive());
  const auto started = controller1.start(lyrics);
  OPENHDK_FAIL_IF(5, started.error != PlaybackController::StartError::None
      || started.acknowledgedGeneration != 1U || !controller1.playbackActive()
      || controller1.libraryMutationAllowed() || controller1.expectedGeneration() != 1U
      || controller1.preparedCatalogRevision() != revision
      || backend1.startCount != 1U || backend1.stopCount != 0U || !backend1.playing);
  // 3. Committed-clock progression on the control thread (one read per poll).
  OPENHDK_FAIL_IF(6, !backend1.publish(1U, 0U));
  const auto firstCue = controller1.pollDisplay();
  OPENHDK_FAIL_IF(7, !firstCue.batch || indices(*firstCue.batch) != std::vector<std::size_t>{0});
  OPENHDK_FAIL_IF(8, !backend1.publish(1U, 166666U));
  const auto secondCue = controller1.pollDisplay();
  OPENHDK_FAIL_IF(9, !secondCue.batch
      || indices(*secondCue.batch) != std::vector<std::size_t>{1, 2}
      || controller1.observed().cues().size() != 3);
  // Preparing holds without clearing; unstable reads are ignored.
  OPENHDK_FAIL_IF(10, !backend1.publish(1U, 166666U, MediaClockPhase::Preparing)
      || controller1.pollDisplay().status != LyricClockPollStatus::Held
      || controller1.observed().cues().size() != 3);
  backend1.forceUnstable = true;
  OPENHDK_FAIL_IF(11, controller1.pollDisplay().status != LyricClockPollStatus::Held
      || controller1.observed().cues().size() != 3);
  backend1.forceUnstable = false;
  // 4. A rejected start preserves the current binding and playback untouched.
  OPENHDK_FAIL_IF(12, controller1.start(lyrics).error != PlaybackController::StartError::AlreadyActive
      || !controller1.playbackActive() || controller1.libraryMutationAllowed()
      || controller1.expectedGeneration() != 1U || controller1.observed().cues().size() != 3
      || backend1.startCount != 1U || backend1.stopCount != 0U || !backend1.playing);
  // 5. A null handle while active is rejected by the active check itself.
  OPENHDK_FAIL_IF(13, controller1.start(nullptr).error != PlaybackController::StartError::AlreadyActive
      || controller1.expectedGeneration() != 1U);
  // 6. Backend rejection from idle establishes no binding and leaks nothing.
  FakeBackend backend2;
  backend2.failNextStart = true;
  PlaybackController controller2(backend2);
  const auto rejected = controller2.start(lyrics);
  OPENHDK_FAIL_IF(14, rejected.error != PlaybackController::StartError::BackendRejected
      || rejected.acknowledgedGeneration != 0U || controller2.playbackActive()
      || !controller2.libraryMutationAllowed() || controller2.expectedGeneration()
      || backend2.startCount != 1U || backend2.stopCount != 0U);
  // The same controller can start again once the backend accepts.
  OPENHDK_FAIL_IF(15, controller2.start(lyrics).error != PlaybackController::StartError::None
      || controller2.expectedGeneration() != 1U || !controller2.playbackActive());
  // 7. A zero generation fails the bind; the observer is stopped before the
  // backend stop and no active state leaks.
  FakeBackend backend3;
  backend3.nextGeneration = 0U;
  PlaybackController controller3(backend3);
  const auto zero = controller3.start(lyrics);
  OPENHDK_FAIL_IF(16, zero.error != PlaybackController::StartError::BindingFailed
      || zero.acknowledgedGeneration != 0U || controller3.playbackActive()
      || controller3.expectedGeneration() || controller3.observed().cues().size() != 0
      || backend3.startCount != 1U || backend3.stopCount != 1U || backend3.playing);
  // 8. A non-monotonic acknowledgement (one the observer already served on
  // this backend object) fails the bind with the same cleanup, and a later
  // monotonic generation can start again.
  FakeBackend backend4;
  PlaybackController controller4(backend4);
  OPENHDK_FAIL_IF(17, controller4.start(lyrics).acknowledgedGeneration != 1U);
  const auto firstStop = controller4.stop();
  OPENHDK_FAIL_IF(18, firstStop.error != PlaybackController::StopError::None
      || controller4.playbackActive() || backend4.stopCount != 1U);
  backend4.nextGeneration = 1U; // Duplicate/lower than the acknowledged generation.
  const auto stale = controller4.start(lyrics);
  OPENHDK_FAIL_IF(19, stale.error != PlaybackController::StartError::BindingFailed
      || stale.acknowledgedGeneration != 1U || controller4.playbackActive()
      || controller4.expectedGeneration() || backend4.stopCount != 2U);
  backend4.nextGeneration = 2U;
  OPENHDK_FAIL_IF(20, controller4.start(lyrics).acknowledgedGeneration != 2U
      || controller4.expectedGeneration() != 2U || !controller4.playbackActive());
  // 9. Ordering, observed through hooks at the moment backend calls begin:
  // idle starts clear the observer first, and stops clear the observer first.
  FakeBackend backend5;
  PlaybackController controller5(backend5);
  bool idleStartCleared = false, stopClearedFirst = false, restartCleared = false;
  backend5.startHook = [&] {
    if (!idleStartCleared) idleStartCleared = !controller5.playbackActive()
        || controller5.expectedGeneration() == std::nullopt;
    else restartCleared = controller5.expectedGeneration() == std::nullopt
        && controller5.observed().cues().empty();
  };
  OPENHDK_FAIL_IF(21, controller5.start(lyrics).acknowledgedGeneration != 1U || !idleStartCleared);
  OPENHDK_FAIL_IF(22, !backend5.publish(1U, 166666U));
  const auto midCue = controller5.pollDisplay();
  OPENHDK_FAIL_IF(23, !midCue.batch || indices(*midCue.batch) != std::vector<std::size_t>{0, 1, 2}
      || controller5.observed().cues().size() != 3);
  backend5.stopHook = [&] { stopClearedFirst = controller5.expectedGeneration() == std::nullopt
      && controller5.observed().cues().empty(); };
  const auto orderedStop = controller5.stop();
  OPENHDK_FAIL_IF(24, !stopClearedFirst
      || orderedStop.error != PlaybackController::StopError::None
      || controller5.playbackActive()
      || !controller5.libraryMutationAllowed() || controller5.expectedGeneration()
      || controller5.observed().cues().size() != 0 || backend5.stopCount != 1U
      || backend5.playing);
  // Stop is a no-op while idle: the backend is not touched again.
  const auto idleStop = controller5.stop();
  OPENHDK_FAIL_IF(25, idleStop.error != PlaybackController::StopError::None
      || !idleStop.message.empty() || backend5.stopCount != 1U);
  backend5.nextGeneration = 2U;
  OPENHDK_FAIL_IF(26, controller5.start(lyrics).acknowledgedGeneration != 2U || !restartCleared);
  // 10. Prepared lifetime survives catalog mutation; identity stays fixed.
  OPENHDK_FAIL_IF(27, !library.scan(*registered.root).succeeded()
      || controller5.preparedCatalogRevision() != revision || !controller5.playbackActive());
  // 11. Finished advances the last cue and retains the observed prefix.
  OPENHDK_FAIL_IF(28, !backend5.publish(2U, 333333U, MediaClockPhase::Finished));
  const auto finalCue = controller5.pollDisplay();
  OPENHDK_FAIL_IF(29, !finalCue.batch
      || indices(*finalCue.batch) != std::vector<std::size_t>{0, 1, 2, 3}
      || controller5.observed().cues().size() != 4);
  // 12. NoLyrics is valid: empty cue batches with a live position.
  FakeBackend backend6;
  PlaybackController controller6(backend6);
  OPENHDK_FAIL_IF(30, controller6.start(noLyrics).error != PlaybackController::StartError::None
      || controller6.expectedGeneration() != 1U);
  OPENHDK_FAIL_IF(31, !backend6.publish(1U, 123U));
  const auto noLyricsCue = controller6.pollDisplay();
  OPENHDK_FAIL_IF(32, !noLyricsCue.batch || !noLyricsCue.batch->cues().empty()
      || controller6.observed().cues().size() != 0);
  // 13. Failed/Stopped/Unavailable phases clear through the observer contract.
  FakeBackend backend7;
  PlaybackController controller7(backend7);
  OPENHDK_FAIL_IF(33, controller7.start(lyrics).acknowledgedGeneration != 1U
      || !backend7.publish(1U, 166666U, MediaClockPhase::Failed)
      || controller7.pollDisplay().status != LyricClockPollStatus::Cleared
      || controller7.expectedGeneration() || !controller7.playbackActive());
  // A cleared display does not stop the controller; an explicit stop ends it.
  const auto failedDisplayStop = controller7.stop();
  OPENHDK_FAIL_IF(34, failedDisplayStop.error != PlaybackController::StopError::None
      || controller7.start(lyrics).acknowledgedGeneration != 2U
      || !backend7.publish(2U, 166666U, MediaClockPhase::Stopped)
      || controller7.pollDisplay().status != LyricClockPollStatus::Cleared);
  const auto stoppedDisplayStop = controller7.stop();
  OPENHDK_FAIL_IF(35, stoppedDisplayStop.error != PlaybackController::StopError::None
      || controller7.start(lyrics).acknowledgedGeneration != 3U
      || !backend7.publish(3U, 166666U, MediaClockPhase::Unavailable)
      || controller7.pollDisplay().status != LyricClockPollStatus::Cleared);
  // 14. An exhausted clock publication clears through the observer contract.
  backend7.forceExhausted = true;
  OPENHDK_FAIL_IF(36, controller7.pollDisplay().status != LyricClockPollStatus::Exhausted
      || controller7.expectedGeneration());
  backend7.forceExhausted = false;
  // 15. A controller destroyed while active performs the ordered stop; the
  // fake backend outlives the controller so the count stays observable.
  auto scopedBackend = std::make_unique<FakeBackend>();
  {
    PlaybackController scopedController(*scopedBackend);
    OPENHDK_FAIL_IF(37, scopedController.start(lyrics).acknowledgedGeneration != 1U);
  }
  OPENHDK_FAIL_IF(38, scopedBackend->stopCount != 1U
      || scopedBackend->shutdownCount != 0U);
  // 16. The retained prepared handle outlives the controller's start scope.
  std::optional<std::uint64_t> retainedRevision;
  {
    FakeBackend backend8;
    PlaybackController controller8(backend8);
    OPENHDK_FAIL_IF(39, controller8.start(lyrics).acknowledgedGeneration != 1U);
    retainedRevision = controller8.preparedCatalogRevision();
  }
  OPENHDK_FAIL_IF(40, retainedRevision != revision || !lyrics || !noLyrics);
  // 17. A throwing backend start fails with no leaked active state: the
  // observer is already cleared before the attempt and the cleanup stop
  // confirms, leaving the controller idle.
  FakeBackend backend9;
  backend9.throwNextStart = true;
  PlaybackController controller9(backend9);
  bool startClearedObserver = false;
  backend9.startHook = [&] {
    startClearedObserver = controller9.expectedGeneration() == std::nullopt;
  };
  const auto thrown = controller9.start(lyrics);
  OPENHDK_FAIL_IF(41, thrown.error != PlaybackController::StartError::Failed
      || thrown.acknowledgedGeneration != 0U || !startClearedObserver
      || controller9.playbackActive() || !controller9.libraryMutationAllowed()
      || controller9.expectedGeneration()
      || controller9.preparedCatalogRevision().has_value());
  OPENHDK_FAIL_IF(42, backend9.startCount != 1U || backend9.stopCount != 1U
      || backend9.playing);
  // 18. A throwing start whose cleanup stop also fails enters the blocked
  // unconfirmed state; library mutations stay excluded until a stop() retry
  // confirms.
  FakeBackend backend10;
  backend10.throwNextStart = true;
  backend10.failNextStop = true;
  PlaybackController controller10(backend10);
  OPENHDK_FAIL_IF(43, controller10.start(lyrics).error
      != PlaybackController::StartError::StopFailed);
  OPENHDK_FAIL_IF(44, !controller10.playbackActive()
      || controller10.libraryMutationAllowed() || controller10.expectedGeneration()
      || controller10.preparedCatalogRevision() != revision);
  OPENHDK_FAIL_IF(45, controller10.stop().error != PlaybackController::StopError::None
      || controller10.playbackActive() || !controller10.libraryMutationAllowed()
      || controller10.preparedCatalogRevision().has_value()
      || backend10.stopCount != 2U);
  // 19. A bind failure whose cleanup stop fails behaves the same way.
  FakeBackend backend11;
  backend11.nextGeneration = 0U; // Zero generation fails the bind.
  backend11.failNextStop = true;
  PlaybackController controller11(backend11);
  const auto blockedBind = controller11.start(lyrics);
  OPENHDK_FAIL_IF(46, blockedBind.error != PlaybackController::StartError::StopFailed
      || blockedBind.acknowledgedGeneration != 0U || !controller11.playbackActive()
      || controller11.libraryMutationAllowed() || controller11.expectedGeneration());
  OPENHDK_FAIL_IF(47, controller11.stop().error != PlaybackController::StopError::None
      || !controller11.libraryMutationAllowed() || backend11.stopCount != 2U);
  // 20. A stop that returns false keeps the prepared handle, blocks mutations,
  // clears the observer, surfaces the backend message and allows retry.
  FakeBackend backend12;
  PlaybackController controller12(backend12);
  OPENHDK_FAIL_IF(48, controller12.start(lyrics).acknowledgedGeneration != 1U);
  OPENHDK_FAIL_IF(49, !backend12.publish(1U, 166666U));
  const auto preStopCue = controller12.pollDisplay();
  OPENHDK_FAIL_IF(50, !preStopCue.batch || controller12.observed().cues().size() != 3);
  backend12.failNextStop = true;
  const auto falseStop = controller12.stop();
  OPENHDK_FAIL_IF(51, falseStop.error != PlaybackController::StopError::StopUnconfirmed
      || falseStop.message.empty() || !controller12.playbackActive()
      || controller12.libraryMutationAllowed() || controller12.expectedGeneration()
      || controller12.observed().cues().size() != 0);
  OPENHDK_FAIL_IF(52, controller12.stop().error != PlaybackController::StopError::None
      || controller12.playbackActive() || backend12.stopCount != 2U);
  // 21. A throwing stop behaves like a failed stop and the retry confirms.
  FakeBackend backend13;
  PlaybackController controller13(backend13);
  OPENHDK_FAIL_IF(53, controller13.start(lyrics).acknowledgedGeneration != 1U);
  backend13.throwNextStop = true;
  const auto throwStop = controller13.stop();
  OPENHDK_FAIL_IF(54, throwStop.error != PlaybackController::StopError::StopUnconfirmed
      || throwStop.message.empty() || !controller13.playbackActive()
      || controller13.libraryMutationAllowed());
  OPENHDK_FAIL_IF(55, controller13.stop().error != PlaybackController::StopError::None
      || controller13.playbackActive() || backend13.stopCount != 2U);
  // 22. A controller destroyed with an unconfirmed stop attempts the ordered
  // stop once more and falls back to the backend's noexcept shutdown().
  auto backend14 = std::make_unique<FakeBackend>();
  backend14->failNextStop = true;
  {
    PlaybackController controller14(*backend14);
    OPENHDK_FAIL_IF(56, controller14.start(lyrics).acknowledgedGeneration != 1U);
  }
  OPENHDK_FAIL_IF(57, backend14->stopCount != 1U || backend14->shutdownCount != 1U);
  return 0;
}