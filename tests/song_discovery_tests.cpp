// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/SongDiscovery.hpp"
#include "tests/TestCheck.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
struct TemporaryRoot {
  fs::path path;
  TemporaryRoot() : path(fs::temp_directory_path() / ("openhdk-discovery-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    if (!fs::create_directory(path)) throw std::runtime_error("Could not create isolated fixture directory");
  }
  ~TemporaryRoot() { std::error_code ec; fs::remove_all(path, ec); }
};
void write(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) throw std::runtime_error("Could not write synthetic fixture");
}
std::string hex(const OpenHDK::SourceRevision& revision) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (auto byte : revision.sha256) { result += digits[byte >> 4U]; result += digits[byte & 15U]; }
  return result;
}

int main() {
  using namespace OpenHDK;
  // Fixed independent Python hashlib vectors, covering both padding branches.
  struct Vector { std::size_t size; const char* digest; };
  const Vector vectors[] = {
    {0U, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {1U, "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"},
    {55U, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
    {56U, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
    {63U, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
    {64U, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
    {65U, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
    {1000U, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"},
  };
  for (const auto& v : vectors) {
    const std::vector<std::uint8_t> bytes(v.size, 97U);
    const auto revision = sourceRevision(bytes);
    OPENHDK_FAIL_IF(1, hex(revision) != v.digest || revision.byteCount != v.size);
  }
  const std::vector<std::uint8_t> abc = {'a','b','c'};
  OPENHDK_FAIL_IF(2, hex(sourceRevision(abc)) != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  std::vector<std::uint8_t> binary(256U);
  for (std::size_t i = 0U; i < binary.size(); ++i) binary[i] = static_cast<std::uint8_t>(i);
  OPENHDK_FAIL_IF(45, hex(sourceRevision(binary)) != "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880");
  const std::vector<std::uint8_t> midi = {'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
                                         'M','T','r','k',0,0,0,4,0,0xff,0x2f,0};
  TemporaryRoot fixture;
  fs::create_directory(fixture.path / "sub");
  write(fixture.path / "z.MID", midi);
  write(fixture.path / "a.kAr", midi);
  write(fixture.path / "ignore.txt", midi);
  write(fixture.path / "sub" / LibraryFilesystem::fromUtf8("เพลง.midi"), midi);
  SongDiscovery discovery;
  const auto registered = discovery.registerRoot(fixture.path);
  OPENHDK_FAIL_IF(3, !registered.root || registered.error);
  const auto root = *registered.root;
  OPENHDK_FAIL_IF(4, discovery.registerRoot(fixture.path / ".").error->code != DiscoveryError::AmbiguousPath);
  OPENHDK_FAIL_IF(5, discovery.registerRoot(fixture.path / "sub").error->code != DiscoveryError::AmbiguousPath);
  OPENHDK_FAIL_IF(6, !discovery.registerRoot(fixture.path / "missing").error);
  auto result = discovery.scan(root);
  OPENHDK_FAIL_IF(7, !result.succeeded() || result.candidates != 3U || !result.diagnostics.empty());
  const auto first = discovery.snapshot();
  OPENHDK_FAIL_IF(8, first->songs.size() != 3U || first->songs[0].locator != "a.kAr" || first->songs[2].locator != "z.MID");
  for (const auto& song : first->songs) {
    OPENHDK_FAIL_IF(9, song.state != CatalogState::Ready || song.sourceRevision != sourceRevision(midi));
  }
  const auto id = first->songs[2].id;
  const auto time = fs::last_write_time(fixture.path / "z.MID");
  auto changed = midi; changed[13] = 97U;
  write(fixture.path / "z.MID", changed); fs::last_write_time(fixture.path / "z.MID", time);
  result = discovery.scan(root);
  OPENHDK_FAIL_IF(10, !result.succeeded() || discovery.snapshot()->songs[2].id != id || discovery.snapshot()->songs[2].sourceRevision == first->songs[2].sourceRevision);
  OPENHDK_FAIL_IF(11, first->songs[2].sourceRevision != sourceRevision(midi));
  write(fixture.path / "bad.mid", {1U});
  result = discovery.scan(root);
  OPENHDK_FAIL_IF(12, !result.succeeded() || result.diagnostics.size() != 1U || !result.diagnostics[0].parseError || result.diagnostics[0].parseError->code != SmfParseErrorCode::TruncatedHeader || result.diagnostics[0].parseError->offset != 1U);
  // Decoder errors retain their nested track error, not only InvalidSmf.
  auto badTrack = midi; badTrack[24] = 0x51U;
  write(fixture.path / "track.mid", badTrack);
  result = discovery.scan(root);
  OPENHDK_FAIL_IF(13, !result.succeeded() || result.diagnostics.size() != 2U || !result.diagnostics[1].timelineError || !result.diagnostics[1].timelineError->trackDecodeError());
  auto before = discovery.snapshot();
  DiscoveryControl cancel; cancel.cancelled = [] { return true; };
  OPENHDK_FAIL_IF(14, discovery.scan(root, {}, cancel).error->code != DiscoveryError::Cancelled || discovery.snapshot() != before);
  DiscoveryLimits small; small.catalog.candidates = 4U;
  OPENHDK_FAIL_IF(15, discovery.scan(root, small).error->code != DiscoveryError::LimitExceeded || discovery.snapshot() != before);
  small = {}; small.smfBytes = midi.size() - 1U;
  OPENHDK_FAIL_IF(16, discovery.scan(root, small).error->code != DiscoveryError::LimitExceeded || discovery.snapshot() != before);
  small = {}; small.depth = 0U;
  OPENHDK_FAIL_IF(17, discovery.scan(root, small).error->code != DiscoveryError::LimitExceeded || discovery.snapshot() != before);
  DiscoveryControl mutate;
  mutate.checkpoint = [&](DiscoveryCheckpoint phase) {
    if (phase == DiscoveryCheckpoint::AfterValidation) { write(fixture.path / "z.MID", midi); fs::last_write_time(fixture.path / "z.MID", time); }
  };
  OPENHDK_FAIL_IF(18, discovery.scan(root, {}, mutate).error->code != DiscoveryError::SourceChanged || discovery.snapshot() != before);
  mutate.checkpoint = [&](DiscoveryCheckpoint phase) {
    if (phase == DiscoveryCheckpoint::AfterEnumeration) fs::remove(fixture.path / "a.kAr");
  };
  OPENHDK_FAIL_IF(19, discovery.scan(root, {}, mutate).error->code != DiscoveryError::SourceChanged || discovery.snapshot() != before);
  // Complete scan can now mark the disappeared source Missing.
  OPENHDK_FAIL_IF(20, !discovery.scan(root).succeeded() || discovery.snapshot()->songs[0].state != CatalogState::Missing || before->songs[0].state != CatalogState::Ready);
  before = discovery.snapshot();
  mutate.checkpoint = [&](DiscoveryCheckpoint phase) {
    if (phase == DiscoveryCheckpoint::BeforePublication) write(fixture.path / "new.mid", midi);
  };
  OPENHDK_FAIL_IF(21, discovery.scan(root, {}, mutate).error->code != DiscoveryError::SourceChanged || discovery.snapshot() != before);
  std::error_code ec;
  fs::create_hard_link(fixture.path / "z.MID", fixture.path / "hard.mid", ec);
  OPENHDK_FAIL_IF(22, ec || !discovery.scan(root).succeeded());
  const auto hard = discovery.snapshot();
  const auto hardSong = std::find_if(hard->songs.begin(), hard->songs.end(), [](const auto& song) { return song.locator == "hard.mid"; });
  OPENHDK_FAIL_IF(23, hardSong == hard->songs.end() || hardSong->id == id);
  TemporaryRoot external;
  write(external.path / "outside.mid", midi);
  fs::create_directory_symlink(external.path, fixture.path / "escape", ec);
#ifndef _WIN32
  OPENHDK_FAIL_IF(24, ec);
#endif
  if (!ec) {
    OPENHDK_FAIL_IF(25, !discovery.scan(root).succeeded());
    for (const auto& song : discovery.snapshot()->songs) {
      OPENHDK_FAIL_IF(26, song.locator.starts_with("escape/"));
    }
    OPENHDK_FAIL_IF(27, LibraryFilesystem::read(fixture.path, "escape/outside.mid", 100U).error != LibraryFilesystem::ReadError::AmbiguousPath);
  } else { std::cerr << "Directory symlink fixture unavailable: " << ec.message() << '\n'; }
  fs::create_symlink(external.path / "outside.mid", fixture.path / "linked.mid", ec);
#ifndef _WIN32
  OPENHDK_FAIL_IF(28, ec);
#endif
  if (!ec) {
    OPENHDK_FAIL_IF(29, !discovery.scan(root).succeeded() || LibraryFilesystem::read(fixture.path, "linked.mid", 100U).error != LibraryFilesystem::ReadError::AmbiguousPath);
  }
  OPENHDK_FAIL_IF(30, LibraryFilesystem::read(fixture.path, "../outside.mid", 100U).error != LibraryFilesystem::ReadError::AmbiguousPath);
  TemporaryRoot collision;
  write(collision.path / "a.mid", midi);
  const bool caseSensitive = !fs::exists(collision.path / "A.mid");
  if (caseSensitive) {
    write(collision.path / "A.mid", midi);
    SongDiscovery aliases;
    const auto ra = *aliases.registerRoot(collision.path).root;
    const auto empty = aliases.snapshot();
    OPENHDK_FAIL_IF(31, aliases.scan(ra).error->code != DiscoveryError::AmbiguousPath || aliases.snapshot() != empty);
  }
  fs::create_directory(fixture.path / "sub" / "deep");
  small = {}; small.depth = 1U;
  before = discovery.snapshot();
  OPENHDK_FAIL_IF(32, discovery.scan(root, small).error->code != DiscoveryError::LimitExceeded || discovery.snapshot() != before);
  small = {}; small.catalog.locatorBytes = 2U;
  OPENHDK_FAIL_IF(33, discovery.scan(root, small).error->code != DiscoveryError::LimitExceeded || discovery.snapshot() != before);
  small = {}; small.catalog.stagedLocatorBytes = 1U;
  OPENHDK_FAIL_IF(34, discovery.scan(root, small).error->code != DiscoveryError::LimitExceeded || discovery.snapshot() != before);
  // Exact configured candidate and byte boundaries are accepted.
  const auto full = discovery.scan(root);
  OPENHDK_FAIL_IF(35, !full.succeeded());
  small = {}; small.catalog.candidates = full.candidates; small.smfBytes = midi.size();
  OPENHDK_FAIL_IF(36, !discovery.scan(root, small).succeeded());
  before = discovery.snapshot();
  bool stop = false;
  DiscoveryControl lateCancel;
  lateCancel.cancelled = [&] { return stop; };
  lateCancel.checkpoint = [&](DiscoveryCheckpoint phase) { if (phase == DiscoveryCheckpoint::AfterValidation) stop = true; };
  OPENHDK_FAIL_IF(37, discovery.scan(root, {}, lateCancel).error->code != DiscoveryError::Cancelled || discovery.snapshot() != before);
  // Known reparse aliases cannot be registered as an overlapping second root.
  if (fs::exists(fixture.path / "escape")) {
    SongDiscovery equivalent;
    OPENHDK_FAIL_IF(38, !equivalent.registerRoot(external.path).root || equivalent.registerRoot(fixture.path / "escape").error->code != DiscoveryError::AmbiguousPath);
  }
#ifdef _WIN32
  // A read-sharing violation is a diagnostic-only invalid source, not Ready.
  write(fixture.path / "locked.mid", midi);
  HANDLE locked = CreateFileW((fixture.path / "locked.mid").c_str(), GENERIC_READ,
      0U, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  OPENHDK_FAIL_IF(39, locked == INVALID_HANDLE_VALUE);
  const auto lockResult = discovery.scan(root);
  CloseHandle(locked);
  OPENHDK_FAIL_IF(40, !lockResult.succeeded());
  const auto lockedSnapshot = discovery.snapshot();
  const auto lockedSong = std::find_if(lockedSnapshot->songs.begin(), lockedSnapshot->songs.end(), [](const auto& song) { return song.locator == "locked.mid"; });
  OPENHDK_FAIL_IF(41, lockedSong == lockedSnapshot->songs.end() || lockedSong->state != CatalogState::Invalid || lockedSong->sourceRevision);
  OPENHDK_FAIL_IF(42, !discovery.scan(root).succeeded());
#else
  if (::geteuid() != 0) {
    write(fixture.path / "unreadable.mid", midi);
    fs::permissions(fixture.path / "unreadable.mid", fs::perms::none);
    const auto denied = discovery.scan(root);
    fs::permissions(fixture.path / "unreadable.mid", fs::perms::owner_all);
    OPENHDK_FAIL_IF(43, !denied.succeeded() || std::none_of(denied.diagnostics.begin(), denied.diagnostics.end(), [](const auto& d) { return d.code == DiscoveryError::SourceUnreadable; }));
    fs::permissions(fixture.path / "sub", fs::perms::none);
    before = discovery.snapshot();
    const auto enumDenied = discovery.scan(root);
    fs::permissions(fixture.path / "sub", fs::perms::owner_all);
    OPENHDK_FAIL_IF(44, enumDenied.succeeded() || discovery.snapshot() != before);
  } else { std::cerr << "Permission fixture skipped for privileged local process\n"; }
#endif
  return 0;
}
