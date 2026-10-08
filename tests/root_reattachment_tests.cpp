// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/SongDiscovery.hpp"
#include "lyrics/LyricMediaConsumer.hpp"
#include "tests/TestCheck.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace OpenHDK;
namespace fs = std::filesystem;
namespace OpenHDK {
// Only this standalone target exposes private seams. Production has no counter
// reset or descriptor injection API.
struct RootReattachmentTestAccess {
  static void counters(SongDiscovery& owner, RootId root, std::uint64_t generation,
      std::uint64_t revision) {
    auto staged = std::make_shared<CatalogSnapshot>(*owner.catalog_.snapshot_);
    staged->revision = revision;
    for (auto& item : staged->roots) if (item.id == root) item.attachmentGeneration = generation;
    owner.catalog_.snapshot_ = std::move(staged);
  }
  static void invalidateAllStates(SongCatalog& catalog, RootId root) {
    auto staged = catalog.stageRootReattachment(root);
    catalog.snapshot_.swap(staged);
  }
};
}
namespace {
struct TemporaryRoot {
  fs::path path = fs::temp_directory_path() / ("openhdk-reattach-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryRoot() { if (!fs::create_directory(path)) throw std::runtime_error("fixture directory failed"); }
  ~TemporaryRoot() { std::error_code ec; fs::remove_all(path, ec); }
};
void write(const fs::path& path) {
  const std::vector<unsigned char> bytes{'M','T','h','d',0,0,0,6,0,0,0,1,0,3,
      'M','T','r','k',0,0,0,9,0,0xff,5,1,'A',0,0xff,0x2f,0};
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) throw std::runtime_error("fixture write failed");
}
std::uint64_t generation(const CatalogSnapshot& snapshot, RootId root) {
  for (const auto& item : snapshot.roots) if (item.id == root) return item.attachmentGeneration;
  throw std::runtime_error("missing root record");
}
bool failed(const RootReattachmentResult& result, DiscoveryError error) {
  return !result.succeeded() && !result.status && result.error && result.error->code == error
      && result.error->operation == DiscoveryOperation::ReattachRoot && !result.error->message().empty();
}
bool sourceChanged(const SongPreparationResult& result) {
  return !result.succeeded() && !result.prepared && result.error
      && result.error->code == PreparationErrorCode::SourceChanged
      && result.error->operation == PreparationOperation::Resolve;
}
}
int main() {
  TemporaryRoot temp;
  const auto old = temp.path / "old", newer = temp.path / "new", other = temp.path / "other";
  fs::create_directory(old); fs::create_directory(newer); fs::create_directory(other);
  write(old / "song.kar"); write(newer / "song.kar"); write(other / "other.mid");
  SongDiscovery library;
  const auto root = library.registerRoot(old).root, second = library.registerRoot(other).root;
  OPENHDK_FAIL_IF(1, !root || !second || !library.scan(*root).succeeded() || !library.scan(*second).succeeded());
  const auto original = library.snapshot();
  const auto id = original->songs[0].id, otherId = original->songs[1].id;
  const auto prepared = library.prepare(original, id);
  OPENHDK_FAIL_IF(2, !prepared.succeeded() || generation(*original, *root) != 1U);
  LyricMediaConsumer consumer;
  OPENHDK_FAIL_IF(3, !consumer.prepare(prepared.prepared->lyrics()));
  const auto batch = consumer.advance(0U);
  OPENHDK_FAIL_IF(4, !batch.succeeded() || batch.batch().cues().size() != 1U);
  const auto unchanged = library.reattachRoot(*root, old);
  OPENHDK_FAIL_IF(5, !unchanged.succeeded() || unchanged.status != RootReattachmentStatus::Unchanged
      || library.snapshot() != original);
  const auto updated = library.reattachRoot(*root, newer);
  const auto invalidated = library.snapshot();
  OPENHDK_FAIL_IF(6, !updated.succeeded() || updated.status != RootReattachmentStatus::Updated
      || invalidated->revision != original->revision + 1U || generation(*invalidated, *root) != 2U
      || generation(*invalidated, *second) != 1U || invalidated->songs[0].id != id
      || invalidated->songs[0].state != CatalogState::Invalid || invalidated->songs[0].sourceRevision()
      || invalidated->songs[1].state != original->songs[1].state
      || invalidated->songs[1].sourceRevision() != original->songs[1].sourceRevision());
  unsigned hooks = 0U; PreparationControl control;
  control.cancelled = [&] { ++hooks; return false; };
  OPENHDK_FAIL_IF(7, !sourceChanged(library.prepare(original, id, {}, control)) || hooks != 0U);
  OPENHDK_FAIL_IF(8, !library.prepare(original, otherId).succeeded()
      || prepared.prepared->lyrics()->cues()[0].decoded != "A"
      || batch.batch().cues()[0].decoded != "A");
  OPENHDK_FAIL_IF(9, !library.scan(*root).succeeded() || library.snapshot()->songs[0].id != id
      || library.snapshot()->songs[0].state != CatalogState::Ready);
  const auto newSnapshot = library.snapshot();
  OPENHDK_FAIL_IF(10, !library.prepare(newSnapshot, id).succeeded()
      || !sourceChanged(library.prepare(original, id)));
  OPENHDK_FAIL_IF(11, !library.scan(*root).succeeded() || !library.prepare(newSnapshot, id).succeeded());
  OPENHDK_FAIL_IF(12, !library.reattachRoot(*root, old).succeeded()
      || generation(*library.snapshot(), *root) != 3U || !sourceChanged(library.prepare(original, id)));
  OPENHDK_FAIL_IF(13, !library.scan(*root).succeeded());
  // A real root move leaves the old binding missing. Reattachment still succeeds.
  const auto moved = temp.path / "moved"; fs::rename(old, moved);
  OPENHDK_FAIL_IF(14, !library.reattachRoot(*root, moved).succeeded()
      || !library.scan(*root).succeeded() || library.snapshot()->songs[0].id != id);
  const auto beforeFailures = library.snapshot();
  OPENHDK_FAIL_IF(15, !failed(library.reattachRoot(*root, temp.path / "absent"), DiscoveryError::InvalidRoot)
      || !failed(library.reattachRoot(*root, moved / "song.kar"), DiscoveryError::InvalidRoot)
      || library.snapshot() != beforeFailures);
  fs::create_directory(other / "nested");
  OPENHDK_FAIL_IF(16, !failed(library.reattachRoot(*root, other), DiscoveryError::AmbiguousPath)
      || !failed(library.reattachRoot(*root, other / "nested"), DiscoveryError::AmbiguousPath)
      || !failed(library.reattachRoot(*root, temp.path), DiscoveryError::AmbiguousPath));
  SongCatalog foreignIds; foreignIds.addRoot(); foreignIds.addRoot(); const auto unknown = *foreignIds.addRoot();
  OPENHDK_FAIL_IF(17, !failed(library.reattachRoot(unknown, newer), DiscoveryError::InvalidRoot));
  RootReattachmentControl cancelled; cancelled.cancelled = [] { return true; };
  OPENHDK_FAIL_IF(18, !failed(library.reattachRoot(*root, newer, cancelled), DiscoveryError::Cancelled));
  bool cancel = false; cancelled.cancelled = [&] { return cancel; };
  cancelled.checkpoint = [&](auto point) { if (point == RootReattachmentCheckpoint::BeforeCommit) cancel = true; };
  OPENHDK_FAIL_IF(19, !failed(library.reattachRoot(*root, newer, cancelled), DiscoveryError::Cancelled)
      || library.snapshot() != beforeFailures || !library.prepare(beforeFailures, id).succeeded());
  for (auto point : {RootReattachmentCheckpoint::AfterValidation,
                    RootReattachmentCheckpoint::BeforeCatalogStaging, RootReattachmentCheckpoint::BeforeCommit}) {
    RootReattachmentControl allocation;
    allocation.checkpoint = [=](auto at) { if (point == at) throw std::bad_alloc(); };
    OPENHDK_FAIL_IF(20, !failed(library.reattachRoot(*root, newer, allocation), DiscoveryError::StorageFailure)
        || library.snapshot() != beforeFailures || !library.prepare(beforeFailures, id).succeeded());
  }
  // Replace the target with a different directory at the same canonical path.
  RootReattachmentControl replaced;
  replaced.checkpoint = [&](auto point) {
    if (point == RootReattachmentCheckpoint::BeforeCommit) {
      fs::rename(newer, temp.path / "saved-new"); fs::create_directory(newer);
    }
  };
  OPENHDK_FAIL_IF(21, !failed(library.reattachRoot(*root, newer, replaced), DiscoveryError::SourceChanged)
      || library.snapshot() != beforeFailures || !library.prepare(beforeFailures, id).succeeded());
  fs::remove(newer); fs::rename(temp.path / "saved-new", newer);
  RootReattachmentControl disappeared;
  disappeared.checkpoint = [&](auto point) {
    if (point == RootReattachmentCheckpoint::AfterValidation) fs::rename(newer, temp.path / "saved-new");
  };
  OPENHDK_FAIL_IF(22, !failed(library.reattachRoot(*root, newer, disappeared), DiscoveryError::SourceChanged)
      || library.snapshot() != beforeFailures);
  fs::rename(temp.path / "saved-new", newer);
  // Retained native handles release after success and all failures.
  OPENHDK_FAIL_IF(23, !library.reattachRoot(*root, newer).succeeded() || !library.scan(*root).succeeded());
  auto current = library.snapshot();
  RootReattachmentTestAccess::counters(library, *root, std::numeric_limits<std::uint64_t>::max(), current->revision);
  const auto exhausted = library.snapshot();
  OPENHDK_FAIL_IF(24, !failed(library.reattachRoot(*root, moved), DiscoveryError::RevisionExhausted)
      || library.snapshot() != exhausted);
  OPENHDK_FAIL_IF(25, library.reattachRoot(*root, newer).status != RootReattachmentStatus::Unchanged
      || library.snapshot() != exhausted);
  RootReattachmentTestAccess::counters(library, *root, 7U, std::numeric_limits<std::uint64_t>::max());
  current = library.snapshot();
  OPENHDK_FAIL_IF(26, !failed(library.reattachRoot(*root, moved), DiscoveryError::RevisionExhausted)
      || library.snapshot() != current || library.reattachRoot(*root, newer).status != RootReattachmentStatus::Unchanged);
  // Pure invalidation covers states not currently produced by filesystem scan.
  SongCatalog catalog; const auto pureRoot = *catalog.addRoot(), unrelated = *catalog.addRoot();
  const auto token = sourceRevision(std::vector<std::uint8_t>{1});
  OPENHDK_FAIL_IF(27, catalog.commitScan(pureRoot, {{"ready.mid", CatalogState::Ready, token},
      {"invalid.mid", CatalogState::Invalid, token}, {"unsupported.mid", CatalogState::UnsupportedProfile, token},
      {"missing.mid", CatalogState::Ready, token}}, true) != CatalogError::None);
  OPENHDK_FAIL_IF(28, catalog.commitScan(pureRoot, {{"ready.mid", CatalogState::Ready, token},
      {"invalid.mid", CatalogState::Invalid, token}, {"unsupported.mid", CatalogState::UnsupportedProfile, token}},
      true) != CatalogError::None || catalog.commitScan(unrelated, {{"other.mid"}}, true) != CatalogError::None);
  const auto pureBefore = catalog.snapshot();
  RootReattachmentTestAccess::invalidateAllStates(catalog, pureRoot);
  for (std::size_t i = 0; i < pureBefore->songs.size(); ++i) {
    const auto& song = catalog.snapshot()->songs[i];
    OPENHDK_FAIL_IF(29, song.id != pureBefore->songs[i].id || song.locator() != pureBefore->songs[i].locator()
        || (song.root == pureRoot && (song.state != CatalogState::Invalid || song.sourceRevision())));
  }
  OPENHDK_FAIL_IF(30, generation(*catalog.snapshot(), pureRoot) != 2U
      || generation(*catalog.snapshot(), unrelated) != 1U || catalog.snapshot()->revision != pureBefore->revision + 1U);
  // An unowned/foreign snapshot still fails lineage before attachment checks.
  SongDiscovery foreign; const auto foreignRoot = foreign.registerRoot(moved).root;
  OPENHDK_FAIL_IF(31, !foreignRoot || !foreign.scan(*foreignRoot).succeeded());
  const auto foreignResult = library.prepare(foreign.snapshot(), id);
  OPENHDK_FAIL_IF(32, !foreignResult.error || foreignResult.error->code != PreparationErrorCode::InvalidConfiguration);
#ifdef _WIN32
  OPENHDK_FAIL_IF(33, !failed(library.reattachRoot(*root, L"\\\\server\\share"), DiscoveryError::InvalidRoot));
#endif
  OPENHDK_FAIL_IF(34, !LibraryFilesystem::windowsNetworkPath("//server/share")
      || LibraryFilesystem::windowsNetworkPath("C:/local"));
  // Complete scan of a new empty directory marks the retained key Missing.
  RootReattachmentTestAccess::counters(library, *root, 9U, 100U);
  const auto empty = temp.path / "empty"; fs::create_directory(empty);
  OPENHDK_FAIL_IF(35, !library.reattachRoot(*root, empty).succeeded() || !library.scan(*root).succeeded()
      || library.snapshot()->songs[0].state != CatalogState::Missing || library.snapshot()->songs[0].id != id);
  // Native no-follow reading still rejects a substituted source symlink.
  const auto linked = temp.path / "linked"; fs::create_directory(linked);
  std::error_code linkError; fs::create_symlink(newer / "song.kar", linked / "song.kar", linkError);
  if (!linkError) {
    OPENHDK_FAIL_IF(36, !library.reattachRoot(*root, linked).succeeded());
    auto injected = std::make_shared<CatalogSnapshot>(*library.snapshot());
    injected->songs[0].state = CatalogState::Ready;
    injected->songs[0].member.revision = original->songs[0].sourceRevision();
    const auto denied = library.prepare(injected, id);
    OPENHDK_FAIL_IF(37, !denied.error || denied.error->code != PreparationErrorCode::AmbiguousPath);
  } else {
    std::cout << "Source symlink fixture skipped: OS denied link creation\n";
  }
  const auto surviving = [] {
    TemporaryRoot owned;const auto a = owned.path / "a", b = owned.path / "b";
    fs::create_directory(a);fs::create_directory(b);write(a / "song.kar");
    SongDiscovery owner;const auto registered = owner.registerRoot(a).root;
    if (!registered || !owner.scan(*registered).succeeded()) throw std::runtime_error("scan failed");
    auto snap = owner.snapshot();auto result = owner.prepare(snap, snap->songs[0].id);
    if (!result.succeeded()) throw std::runtime_error("preparation failed");
    LyricMediaConsumer reader;
    if (!reader.prepare(result.prepared->lyrics())) throw std::runtime_error("binding failed");
    auto due = reader.advance(0U);
    if (!owner.reattachRoot(*registered, b).succeeded()) throw std::runtime_error("reattach failed");
    return std::pair{result.prepared, due.batch()};
  }();
  OPENHDK_FAIL_IF(38, surviving.first->song().state != CatalogState::Ready
      || surviving.first->lyrics()->cues()[0].decoded != "A" || surviving.second.cues()[0].decoded != "A");
  // Missing captured attachment data is not accepted even with valid lineage.
  auto absentGeneration = std::make_shared<CatalogSnapshot>(*library.snapshot());
  absentGeneration->roots.clear();
  OPENHDK_FAIL_IF(39, !sourceChanged(library.prepare(absentGeneration, id)));
  return 0;
}
