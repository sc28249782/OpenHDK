// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/CatalogCheckpointCapture.hpp"
#include "library/CatalogCheckpointRestore.hpp"
#include "lyrics/LyricMediaConsumer.hpp"
#include "tests/TestCheck.hpp"
#include "tests/fixtures/checkpoint_vectors.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <new>
#include <stdexcept>
#include <type_traits>
namespace { std::ptrdiff_t failAfter=-1; }
#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif
TEST_NOINLINE void* operator new(std::size_t n) {
  if(failAfter==0) throw std::bad_alloc();
  if(failAfter>0) --failAfter;
  if(auto p=std::malloc(n?n:1U)) return p;
  throw std::bad_alloc();
}
TEST_NOINLINE void operator delete(void* p) noexcept {std::free(p);}
TEST_NOINLINE void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
#undef TEST_NOINLINE
using namespace OpenHDK;
namespace fs = std::filesystem;
struct TemporaryRoot {
  fs::path path = fs::temp_directory_path() / ("openhdk-capture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryRoot() { if (!fs::create_directory(path)) throw std::runtime_error("fixture directory failed"); }
  ~TemporaryRoot() { std::error_code ec; fs::remove_all(path, ec); }
};
void write(const fs::path& path) {
  const std::vector<std::uint8_t> bytes{'M','T','h','d',0,0,0,6,0,0,0,1,0,3,'M','T','r','k',0,0,0,9,0,0xff,5,1,'A',0,0xff,0x2f,0};
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!f) throw std::runtime_error("fixture write failed");
}
bool error(const CatalogCaptureResult& r, CheckpointErrorCode code) {
  return r.captured || !r.error || r.error->code != code || r.error->operation != CheckpointOperation::Capture;
}
int main() {
  static_assert(!std::is_constructible_v<RootId, std::uint64_t>);
  static_assert(!std::is_constructible_v<SongId, std::uint64_t>);
  SongDiscovery empty;
  auto initial = CatalogCheckpointCapture::acquire(empty);
  OPENHDK_FAIL_IF(1, !initial.succeeded() || initial.captured->snapshot() != empty.snapshot());
  OPENHDK_FAIL_IF(2, initial.captured->projection() != CatalogCheckpointProjection{});
  auto invalid = CatalogCheckpointLimits{}; invalid.roots = 0;
  OPENHDK_FAIL_IF(3, error(CatalogCheckpointCapture::acquire(empty, invalid), CheckpointErrorCode::InvalidConfiguration));
  OPENHDK_FAIL_IF(4, error(CatalogCheckpointCapture::acquire(empty, {}, SIZE_MAX), CheckpointErrorCode::LimitExceeded));

  // Imported allocator gaps and exhausted attachment history must not be rebuilt
  // from row counts. No directory behind this hint is needed for acquisition.
  CatalogCheckpointProjection input; input.catalogRevision = 77; input.nextRoot = 100; input.nextSong = 1000;
  input.roots.push_back({2, UINT64_MAX, {LibrarySourceMode::SmfKar, {LyricTextEncoding::Tis620, 7}}, "/nonexistent/capture-hint"});
  input.songs.push_back({4, 2, SourceMemberRole::PrimaryMidi, "song.kar", " Title e\xcc\x81 ", "ศิลปิน"});
  auto restored = CatalogCheckpointRestore::fromProjection(input);
  OPENHDK_FAIL_IF(5, !restored.succeeded());
  auto imported = CatalogCheckpointCapture::acquire(*restored.owner);
  OPENHDK_FAIL_IF(6, !imported.succeeded() || imported.captured->projection() != input);
  OPENHDK_FAIL_IF(7, imported.captured->snapshot()->songs[0].state != CatalogState::Invalid);
  auto encoded = encodeCatalogCheckpoint(imported.captured->projection());
  OPENHDK_FAIL_IF(8, !encoded.succeeded());
  auto decoded = decodeCatalogCheckpoint(*encoded.value);
  OPENHDK_FAIL_IF(9, !decoded.succeeded() || *decoded.value != input);
  auto hintless = input; hintless.roots[0].hint.reset();
  auto noHint = CatalogCheckpointRestore::fromProjection(hintless);
  OPENHDK_FAIL_IF(10, !noHint.succeeded() || CatalogCheckpointCapture::acquire(*noHint.owner).captured->projection() != hintless);
  auto exhausted = input; exhausted.nextRoot = UINT64_MAX; exhausted.nextSong = UINT64_MAX;
  auto full = CatalogCheckpointRestore::fromProjection(exhausted);
  OPENHDK_FAIL_IF(11, !full.succeeded() || CatalogCheckpointCapture::acquire(*full.owner).captured->projection() != exhausted);
  CatalogCheckpointLimits limits; limits.textBytes = 3;
  OPENHDK_FAIL_IF(12, error(CatalogCheckpointCapture::acquire(*restored.owner, limits), CheckpointErrorCode::LimitExceeded));
  limits = {}; limits.fileBytes = 96;
  OPENHDK_FAIL_IF(13, error(CatalogCheckpointCapture::acquire(*restored.owner, limits), CheckpointErrorCode::LimitExceeded));
  // Find the exact logical allowance required by this detached fixture. Fixed
  // descriptor allocations are count-bounded; this is not process-memory usage.
  std::size_t boundary = 1;
  for (; boundary < 4096; ++boundary) {
    limits = {}; limits.stagedBytes = boundary;
    auto trial = CatalogCheckpointCapture::acquire(*restored.owner, limits);
    if (trial.succeeded()) break;
    OPENHDK_FAIL_IF(14, error(trial, CheckpointErrorCode::LimitExceeded));
  }
  OPENHDK_FAIL_IF(15, boundary == 4096);
  limits.stagedBytes = boundary;
  OPENHDK_FAIL_IF(16, !CatalogCheckpointCapture::acquire(*restored.owner, limits).succeeded());
  limits.stagedBytes = boundary - 1;
  OPENHDK_FAIL_IF(17, error(CatalogCheckpointCapture::acquire(*restored.owner, limits), CheckpointErrorCode::LimitExceeded));
  limits.stagedBytes = boundary;
  OPENHDK_FAIL_IF(18, error(CatalogCheckpointCapture::acquire(*restored.owner, limits, 1), CheckpointErrorCode::LimitExceeded));

  TemporaryRoot a, b; write(a.path / "song.kar"); write(b.path / "song.kar");
  SongDiscovery owner;
  auto registered = owner.registerRoot(a.path);
  OPENHDK_FAIL_IF(19, !registered.root || !owner.scan(*registered.root).succeeded());
  const auto snapshot = owner.snapshot();
  auto saved = CatalogCheckpointCapture::acquire(owner);
  OPENHDK_FAIL_IF(20, !saved.succeeded() || saved.captured->snapshot() != snapshot);
  const auto oldProjection = saved.captured->projection();
  OPENHDK_FAIL_IF(21, oldProjection.roots[0].hint != LibraryFilesystem::utf8(fs::canonical(a.path)));
  const auto id = snapshot->songs[0].id;
  auto changed = owner.replaceUserOverrides(id, {" New title ", "Artist"});
  OPENHDK_FAIL_IF(22, !changed.succeeded());
  auto overridden = CatalogCheckpointCapture::acquire(owner);
  OPENHDK_FAIL_IF(23, !overridden.succeeded() || overridden.captured->projection().songs[0].title != " New title ");
  OPENHDK_FAIL_IF(24, saved.captured->projection() != oldProjection || snapshot != saved.captured->snapshot());
  auto rebound = owner.reattachRoot(*registered.root, b.path);
  OPENHDK_FAIL_IF(25, !rebound.succeeded());
  auto current = CatalogCheckpointCapture::acquire(owner);
  OPENHDK_FAIL_IF(26, !current.succeeded() || current.captured->projection().roots[0].attachmentGeneration != 2
      || current.captured->projection().roots[0].hint != LibraryFilesystem::utf8(fs::canonical(b.path)));
  OPENHDK_FAIL_IF(27, saved.captured->projection() != oldProjection);
  // No source lookup/read/hash during capture: delete an attached directory.
  fs::remove_all(b.path);
  auto absent = CatalogCheckpointCapture::acquire(owner);
  OPENHDK_FAIL_IF(28, !absent.succeeded() || absent.captured->projection() != current.captured->projection());
  const auto retained = owner.snapshot();
  bool success = false; std::size_t failures = 0;
  for (std::ptrdiff_t budget = 0; budget < 128; ++budget) {
    failAfter = budget;
    auto trial = CatalogCheckpointCapture::acquire(owner);
    failAfter = -1;
    if (trial.succeeded()) { success = true; break; }
    ++failures;
    OPENHDK_FAIL_IF(29, error(trial, CheckpointErrorCode::StorageFailure) || owner.snapshot() != retained);
  }
  OPENHDK_FAIL_IF(30, !success || failures == 0 || owner.snapshot() != retained);
  auto retainedCapture = [] {
    SongDiscovery local;
    return CatalogCheckpointCapture::acquire(local).captured;
  }();
  OPENHDK_FAIL_IF(31, !retainedCapture || retainedCapture->projection() != CatalogCheckpointProjection{});
  auto restoreAgain = CatalogCheckpointRestore::fromProjection(saved.captured->projection());
  OPENHDK_FAIL_IF(32, !restoreAgain.succeeded() || restoreAgain.owner->snapshot()->songs[0].sourceRevision()
      || restoreAgain.owner->snapshot()->songs[0].metadata || restoreAgain.owner->snapshot()->songs[0].state != CatalogState::Invalid);
  auto two = input; two.roots.push_back({3, 1, {}, {}});
  two.songs.push_back({5, 3, SourceMemberRole::PrimaryMidi, "other.mid", {}, {}});
  auto multi = CatalogCheckpointRestore::fromProjection(two);
  OPENHDK_FAIL_IF(33, !multi.succeeded());
  limits = {}; limits.roots = 1;
  OPENHDK_FAIL_IF(34, error(CatalogCheckpointCapture::acquire(*multi.owner, limits), CheckpointErrorCode::LimitExceeded));
  limits = {}; limits.songs = 1;
  OPENHDK_FAIL_IF(35, error(CatalogCheckpointCapture::acquire(*multi.owner, limits), CheckpointErrorCode::LimitExceeded));
  restored.owner.reset();
  OPENHDK_FAIL_IF(36, imported.captured->projection() != input || imported.captured->snapshot()->songs.size() != 1);

}
