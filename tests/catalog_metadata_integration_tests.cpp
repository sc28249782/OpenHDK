// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "library/SongDiscovery.hpp"
#include "tests/TestCheck.hpp"
#include <chrono>
#include <fstream>
#include <stdexcept>

using namespace OpenHDK;
namespace fs = std::filesystem;
namespace {
using Bytes = std::vector<std::uint8_t>;
struct TemporaryRoot {
  fs::path path = fs::temp_directory_path() / ("openhdk-metadata-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryRoot() { if (!fs::create_directory(path)) throw std::runtime_error("fixture directory failed"); }
  ~TemporaryRoot() { std::error_code ec; fs::remove_all(path, ec); }
};
void vlq(Bytes& output, std::size_t value) {
  std::uint8_t encoded[4]{};
  std::size_t count = 0U;
  do { encoded[count++] = static_cast<std::uint8_t>(value & 127U); value >>= 7U; } while (value);
  while (count) { --count; output.push_back(static_cast<std::uint8_t>(encoded[count] | (count ? 128U : 0U))); }
}
void meta(Bytes& track, std::uint8_t kind, std::string_view text, unsigned delta = 0U) {
  vlq(track, delta); track.insert(track.end(), {0xffU, kind}); vlq(track, text.size());
  track.insert(track.end(), text.begin(), text.end());
}
Bytes named(std::string title, std::string words = "A") {
  Bytes result; meta(result, 1U, "@T" + title); meta(result, 3U, " Words ");
  meta(result, 1U, "@TAdditional"); meta(result, 1U, words, 1U); meta(result, 0x2fU, ""); return result;
}
Bytes smf(std::vector<Bytes> tracks) {
  Bytes output{'M','T','h','d',0,0,0,6,0,static_cast<std::uint8_t>(tracks.size() > 1U),0,
      static_cast<std::uint8_t>(tracks.size()),0,3};
  for (const auto& track : tracks) {
    output.insert(output.end(), {'M','T','r','k'});
    for (const auto shift : {24U,16U,8U,0U}) output.push_back(static_cast<std::uint8_t>(track.size() >> shift));
    output.insert(output.end(), track.begin(), track.end());
  }
  return output;
}
void write(const fs::path& path, const Bytes& bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) throw std::runtime_error("fixture write failed");
}
const CatalogSong& song(const CatalogSnapshot& snapshot, std::string_view locator) {
  const auto found = std::find_if(snapshot.songs.begin(), snapshot.songs.end(),
      [&](const auto& item) { return item.locator() == locator; });
  if (found == snapshot.songs.end()) throw std::runtime_error("fixture song absent");
  return *found;
}
}

int main() {
  TemporaryRoot temp;
  SongDiscovery library, reference;
  const auto empty = library.snapshot();
  for (const auto policy : {RootSourcePolicy{static_cast<LibrarySourceMode>(999), {}},
       RootSourcePolicy{LibrarySourceMode::SmfKar, {static_cast<LyricTextEncoding>(999), {}}}}) {
    const auto bad = library.registerRoot(temp.path / "does-not-exist", policy);
    OPENHDK_FAIL_IF(1, bad.root || !bad.error || bad.error->code != DiscoveryError::InvalidConfiguration
        || library.snapshot() != empty);
  }
  const auto root = *library.registerRoot(temp.path).root;
  const auto cleanRoot = *reference.registerRoot(temp.path).root;
  OPENHDK_FAIL_IF(2, root != cleanRoot || library.snapshot()->revision != reference.snapshot()->revision
      || library.snapshot()->roots[0].policy != RootSourcePolicy{});
  const auto titledBytes = smf({named(" Title/Artist ")});
  write(temp.path / "title.mid", titledBytes);
  Bytes plain; meta(plain, 0x2fU, ""); write(temp.path / "plain.kar", smf({plain}));
  Bytes tags; meta(tags, 3U, "Lyrics"); meta(tags, 1U, "@TOnly"); meta(tags, 0x2fU, "");
  write(temp.path / "tags.kar", smf({tags}));
  OPENHDK_FAIL_IF(3, !library.scan(root).succeeded());
  const auto first = library.snapshot(); const auto& titled = song(*first, "title.mid"); const auto id = titled.id;
  OPENHDK_FAIL_IF(4, titled.state != CatalogState::Ready || titled.member.locator != titled.locator()
      || &titled.member.revision != &titled.sourceRevision() || !titled.metadata || !titled.metadata->title
      || titled.metadata->title->text.bytes() != " Title/Artist "
      || titled.metadata->kind != VerifiedSourceKind::CanonicalSmf
      || titled.metadata->lyrics->profile != KarLyricProfile::NamedText
      || titled.metadata->lyrics->selectedTrack != 0U || titled.metadata->lyrics->cueCount != 1U);
  const auto& provenance = titled.metadata->title->provenance;
  OPENHDK_FAIL_IF(5, provenance.revision != sourceRevision(titledBytes)
      || provenance.effectivePolicy != LyricSelectionPolicy{}
      || provenance.position != LyricSourcePosition{0U,0U,0U,0U});
  for (const auto locator : {"plain.kar", "tags.kar"}) {
    const auto& item = song(*first, locator);
    OPENHDK_FAIL_IF(6, item.state != CatalogState::Ready || !item.metadata || item.metadata->title
        || item.metadata->lyrics->profile != KarLyricProfile::NoLyrics
        || item.metadata->lyrics->selectedTrack || item.metadata->lyrics->cueCount != 0U);
  }
  auto prepared = library.prepare(first, id);
  OPENHDK_FAIL_IF(7, !prepared.succeeded() || prepared.prepared->options().lyricOverride
      || prepared.prepared->effectivePolicy() != first->roots[0].policy.lyrics
      || prepared.prepared->sourceMetadata().title->text.bytes() != " Title/Artist "
      || prepared.prepared->sourceMetadata().title->provenance.position != provenance.position);
  // FF05 wins; the title on an unselected NamedText track must not leak across.
  Bytes ff05; meta(ff05, 5U, "/\\", 1U); meta(ff05, 0x2fU, "");
  write(temp.path / "preferred.mid", smf({named("Hidden"), ff05}));
  OPENHDK_FAIL_IF(8, !library.scan(root).succeeded());
  const auto& preferred = song(*library.snapshot(), "preferred.mid");
  OPENHDK_FAIL_IF(9, !preferred.metadata || preferred.metadata->title
      || preferred.metadata->lyrics->profile != KarLyricProfile::LyricMeta
      || preferred.metadata->lyrics->selectedTrack != 1U || preferred.metadata->lyrics->cueCount != 1U);
  const auto unchanged = library.snapshot();
  PreparationOptions selectNamed; selectNamed.lyricOverride = LyricSelectionPolicy{LyricTextEncoding::Utf8, 0U};
  auto alternate = library.prepare(unchanged, preferred.id, selectNamed);
  OPENHDK_FAIL_IF(10, !alternate.succeeded() || !alternate.prepared->sourceMetadata().title
      || alternate.prepared->sourceMetadata().title->text.bytes() != "Hidden"
      || alternate.prepared->sourceMetadata().lyrics->effectivePolicy != *selectNamed.lyricOverride
      || alternate.prepared->song().metadata->title || library.snapshot() != unchanged);
  unsigned hooks = 0U; PreparationControl noIo;
  noIo.cancelled = [&] { ++hooks; return false; }; noIo.checkpoint = [&](auto) { ++hooks; };
  PreparationOptions unknown; unknown.lyricOverride = LyricSelectionPolicy{static_cast<LyricTextEncoding>(999), {}};
  fs::remove(temp.path / "title.mid");
  const auto rejected = library.prepare(unchanged, id, unknown, noIo);
  OPENHDK_FAIL_IF(11, rejected.prepared || rejected.error->code != PreparationErrorCode::InvalidConfiguration
      || rejected.error->operation != PreparationOperation::Resolve || hooks != 0U);
  write(temp.path / "title.mid", titledBytes);
  // Selected-track invalid text becomes Invalid; another good song still commits.
  write(temp.path / "title.mid", smf({named("Bad", std::string(1U, static_cast<char>(0xa1)))}));
  const auto invalid = library.scan(root);
  OPENHDK_FAIL_IF(12, !invalid.succeeded() || invalid.diagnostics.size() != 1U
      || invalid.diagnostics[0].code != DiscoveryError::InvalidLyrics
      || !invalid.diagnostics[0].lyricError || !invalid.diagnostics[0].lyricError->position
      || invalid.diagnostics[0].lyricError->payloadByteOffset != 0U);
  const auto invalidSnapshot = library.snapshot(); const auto& invalidSong = song(*invalidSnapshot, "title.mid");
  OPENHDK_FAIL_IF(13, invalidSong.id != id || invalidSong.state != CatalogState::Invalid
      || !invalidSong.sourceRevision() || !invalidSong.metadata
      || invalidSong.metadata->kind != VerifiedSourceKind::CanonicalSmf
      || invalidSong.metadata->title || invalidSong.metadata->lyrics
      || song(*invalidSnapshot, "plain.kar").state != CatalogState::Ready
      || titled.metadata->title->text.bytes() != " Title/Artist ");
  PreparationOptions rescue; rescue.lyricOverride = LyricSelectionPolicy{LyricTextEncoding::Tis620, {}};
  const auto notReady = library.prepare(invalidSnapshot, id, rescue);
  OPENHDK_FAIL_IF(14, notReady.prepared || notReady.error->code != PreparationErrorCode::NotReady);
  fs::remove(temp.path / "title.mid");
  OPENHDK_FAIL_IF(15, !library.scan(root).succeeded() || song(*library.snapshot(), "title.mid").metadata
      || song(*library.snapshot(), "title.mid").sourceRevision()
      || song(*library.snapshot(), "title.mid").state != CatalogState::Missing
      || prepared.prepared->sourceMetadata().title->text.bytes() != " Title/Artist ");
  write(temp.path / "title.mid", titledBytes);
  OPENHDK_FAIL_IF(16, !library.scan(root).succeeded() || song(*library.snapshot(), "title.mid").id != id);
  auto before = library.snapshot(); DiscoveryControl change;
  change.checkpoint = [&](auto point) { if (point == DiscoveryCheckpoint::AfterExtraction)
      write(temp.path / "plain.kar", smf({named("Different")})); };
  auto changed = library.scan(root, {}, change);
  OPENHDK_FAIL_IF(17, changed.succeeded() || changed.error->code != DiscoveryError::SourceChanged || library.snapshot() != before);
  write(temp.path / "plain.kar", smf({plain}));
  bool cancelled = false; DiscoveryControl cancel;
  cancel.cancelled = [&] { return cancelled; };
  cancel.checkpoint = [&](auto point) { if (point == DiscoveryCheckpoint::AfterExtraction) cancelled = true; };
  const auto cancelledResult = library.scan(root, {}, cancel);
  OPENHDK_FAIL_IF(18, cancelledResult.succeeded() || cancelledResult.error->code != DiscoveryError::Cancelled || library.snapshot() != before);
  DiscoveryControl allocation; allocation.checkpoint = [](auto point) {
    if (point == DiscoveryCheckpoint::AfterExtraction) throw std::bad_alloc();
  };
  auto failedAllocation = library.scan(root, {}, allocation);
  OPENHDK_FAIL_IF(19, failedAllocation.succeeded() || failedAllocation.error->code != DiscoveryError::StorageFailure || library.snapshot() != before);
  DiscoveryLimits limited; limited.lyrics.titleBytes = 2U;
  auto failedLimit = library.scan(root, limited);
  OPENHDK_FAIL_IF(20, failedLimit.succeeded() || failedLimit.error->code != DiscoveryError::LimitExceeded
      || !failedLimit.error->lyricError || library.snapshot() != before);

  TemporaryRoot thaiDir, movedDir;
  const std::string thaiBytes(1U, static_cast<char>(0xa1));
  const auto thaiMidi = smf({named(thaiBytes, thaiBytes)}); write(thaiDir.path / "thai.kar", thaiMidi);
  const RootSourcePolicy thaiPolicy{LibrarySourceMode::SmfKar,{LyricTextEncoding::Tis620,0U}};
  SongDiscovery thai; const auto thaiRoot = *thai.registerRoot(thaiDir.path, thaiPolicy).root;
  OPENHDK_FAIL_IF(21, !thai.scan(thaiRoot).succeeded());
  const auto thaiSnapshot = thai.snapshot(); const auto thaiId = thaiSnapshot->songs[0].id;
  auto inherited = thai.prepare(thaiSnapshot, thaiId);
  OPENHDK_FAIL_IF(22, !inherited.succeeded() || inherited.prepared->effectivePolicy() != thaiPolicy.lyrics
      || inherited.prepared->sourceMetadata().title->text.bytes() != "\xe0\xb8\x81"
      || inherited.prepared->sourceMetadata().title->provenance.effectivePolicy != thaiPolicy.lyrics);
  PreparationOptions utf8; utf8.lyricOverride = LyricSelectionPolicy{};
  auto wrong = thai.prepare(thaiSnapshot, thaiId, utf8);
  OPENHDK_FAIL_IF(23, wrong.prepared || wrong.error->code != PreparationErrorCode::InvalidLyrics
      || !wrong.error->lyricError || thai.snapshot() != thaiSnapshot);
  write(movedDir.path / "thai.kar", thaiMidi);
  OPENHDK_FAIL_IF(24, !thai.reattachRoot(thaiRoot, movedDir.path).succeeded()
      || thai.snapshot()->roots[0].policy != thaiPolicy || thai.snapshot()->songs[0].metadata
      || thai.snapshot()->songs[0].sourceRevision());
  auto stale = thai.prepare(thaiSnapshot, thaiId);
  OPENHDK_FAIL_IF(25, stale.prepared || stale.error->code != PreparationErrorCode::SourceChanged
      || !thai.scan(thaiRoot).succeeded() || thai.snapshot()->songs[0].id != thaiId
      || thai.snapshot()->songs[0].metadata->title->text.bytes() != "\xe0\xb8\x81"
      || inherited.prepared->sourceMetadata().title->text.bytes() != "\xe0\xb8\x81");
  // Root-selected out-of-range index is registered, then per-file diagnosed.
  SongDiscovery absentTrack;
  const auto indexRoot = *absentTrack.registerRoot(movedDir.path,
      {LibrarySourceMode::SmfKar,{LyricTextEncoding::Tis620,99U}}).root;
  const auto noTrack = absentTrack.scan(indexRoot);
  OPENHDK_FAIL_IF(26, !noTrack.succeeded() || noTrack.diagnostics.size() != 1U
      || noTrack.diagnostics[0].lyricError->code != KarLyricErrorCode::InvalidLyricTrack
      || absentTrack.snapshot()->songs[0].state != CatalogState::Invalid);

  TemporaryRoot budgetDir; write(budgetDir.path / "a.mid", smf({plain}));
  SongDiscovery bounded; const auto budgetRoot = *bounded.registerRoot(budgetDir.path).root;
  DiscoveryLimits payload; payload.catalog.stagedLocatorBytes = 24U;
  const auto unvisited = bounded.snapshot();
  OPENHDK_FAIL_IF(27, bounded.scan(budgetRoot, payload).error->code != DiscoveryError::LimitExceeded || bounded.snapshot() != unvisited);
  payload.catalog.stagedLocatorBytes = 25U; // Five copies of the 5-byte locator, no text payload.
  OPENHDK_FAIL_IF(28, !bounded.scan(budgetRoot, payload).succeeded());
  payload.catalog.stagedLocatorBytes = 34U;
  const auto boundedBefore = bounded.snapshot();
  OPENHDK_FAIL_IF(29, bounded.scan(budgetRoot, payload).error->code != DiscoveryError::LimitExceeded || bounded.snapshot() != boundedBefore);
  payload.catalog.stagedLocatorBytes = 35U; // Two current/staged copies + five incoming.
  OPENHDK_FAIL_IF(30, !bounded.scan(budgetRoot, payload).succeeded());
  PreparationOptions prepBudget; prepBudget.stagedBytes = 10U;
  const auto budgetSnapshot = bounded.snapshot();
  OPENHDK_FAIL_IF(31, !bounded.prepare(budgetSnapshot, budgetSnapshot->songs[0].id, prepBudget).succeeded());
  prepBudget.stagedBytes = 9U;
  OPENHDK_FAIL_IF(32, bounded.prepare(budgetSnapshot, budgetSnapshot->songs[0].id, prepBudget).error->code != PreparationErrorCode::LimitExceeded);
  // Readable-invalid canonical input retains its only token; complete invalidation clears title.
  write(budgetDir.path / "a.mid", {1U});
  OPENHDK_FAIL_IF(33, !bounded.scan(budgetRoot).succeeded() || !bounded.snapshot()->songs[0].sourceRevision()
      || bounded.snapshot()->songs[0].metadata || bounded.snapshot()->songs[0].state != CatalogState::Invalid);
  // Title byte ceiling applies to canonical extraction and compact storage.
  TemporaryRoot titleDir;
  write(titleDir.path / "long.kar", smf({named(std::string(4096U, 'T'))}));
  SongDiscovery titles; const auto titlesRoot = *titles.registerRoot(titleDir.path).root;
  OPENHDK_FAIL_IF(34, !titles.scan(titlesRoot).succeeded()
      || titles.snapshot()->songs[0].metadata->title->text.bytes().size() != 4096U);
  const auto atLimit = titles.snapshot();
  write(titleDir.path / "long.kar", smf({named(std::string(4097U, 'T'))}));
  const auto tooLong = titles.scan(titlesRoot);
  OPENHDK_FAIL_IF(35, tooLong.succeeded() || tooLong.error->code != DiscoveryError::LimitExceeded
      || titles.snapshot() != atLimit || atLimit->songs[0].metadata->title->text.bytes().size() != 4096U);
  const auto preparationBefore = library.snapshot();
  PreparationControl prepFailure;
  prepFailure.checkpoint = [](auto point) {
    if (point == PreparationCheckpoint::AfterExtraction) throw std::bad_alloc();
  };
  const auto noPrepared = library.prepare(preparationBefore, id, {}, prepFailure);
  OPENHDK_FAIL_IF(36, noPrepared.prepared || noPrepared.error->code != PreparationErrorCode::StorageFailure
      || library.snapshot() != preparationBefore || prepared.prepared->sourceMetadata().title->text.bytes() != " Title/Artist ");
  // Ambiguous selection is a per-file nested error, not a partial mixed timeline.
  write(titleDir.path / "long.kar", smf({named("First"), named("Second")}));
  const auto ambiguous = titles.scan(titlesRoot);
  OPENHDK_FAIL_IF(37, !ambiguous.succeeded() || ambiguous.diagnostics.size() != 1U
      || ambiguous.diagnostics[0].lyricError->code != KarLyricErrorCode::AmbiguousLyricTrack
      || titles.snapshot()->songs[0].metadata->title || titles.snapshot()->songs[0].metadata->lyrics);
  // Shared records are counted once; failed credit must not poison that set.
  auto metadata = std::make_shared<CatalogSourceMetadata>();
  metadata->title.emplace(SourceTitleMetadata{*MetadataText::create("ABCD").text(),
      {SourceMemberRole::PrimaryMidi, {}, {}, {0U,0U,0U,0U}}});
  const std::shared_ptr<const CatalogSourceMetadata> shared = metadata;
  auto credit = *MetadataPayloadBudget::create(4U);
  std::set<const CatalogSourceMetadata*> charged;
  OPENHDK_FAIL_IF(38, !chargeSourceMetadata(credit, shared, charged)
      || !chargeSourceMetadata(credit, shared, charged) || credit.used() != 4U || charged.size() != 1U);
  const auto separate = std::make_shared<const CatalogSourceMetadata>(*metadata);
  OPENHDK_FAIL_IF(39, chargeSourceMetadata(credit, separate, charged)
      || chargeSourceMetadata(credit, separate, charged) || charged.size() != 1U || credit.used() != 4U);
  return 0;
}
