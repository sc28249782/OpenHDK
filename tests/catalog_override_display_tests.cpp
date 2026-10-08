// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "library/SongDiscovery.hpp"
#include "tests/TestCheck.hpp"
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <cstdlib>
#include <new>


// Sweep real allocation points in isolated control-path calls. The injector is
// disarmed before any assertion/fixture cleanup, and absent from production.
namespace { std::ptrdiff_t allocationsBeforeFailure = -1; }
#if defined(_MSC_VER)
#define OPENHDK_NOINLINE __declspec(noinline)
#else
#define OPENHDK_NOINLINE __attribute__((noinline))
#endif
OPENHDK_NOINLINE void* operator new(std::size_t bytes) {
  if (allocationsBeforeFailure == 0) throw std::bad_alloc();
  if (allocationsBeforeFailure > 0) --allocationsBeforeFailure;
  if (void* p = std::malloc(bytes ? bytes : 1U)) return p;
  throw std::bad_alloc();
}
OPENHDK_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
OPENHDK_NOINLINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
#undef OPENHDK_NOINLINE
namespace OpenHDK {
struct CatalogMetadataTransactionTestAccess {
  static void revision(SongCatalog& catalog, std::uint64_t value) {
    auto staged = std::make_shared<CatalogSnapshot>(*catalog.snapshot_);
    staged->revision = value;
    catalog.snapshot_ = std::move(staged);
  }
};
}

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

}

int main() {
  SongCatalog catalog; const auto root = *catalog.addRoot();
  const auto token = sourceRevision(std::vector<std::uint8_t>{1U,2U});
  OPENHDK_FAIL_IF(1, catalog.commitScan(root, {{"dir/Live.Set.kar", CatalogState::Ready, token}}, true) != CatalogError::None);
  const auto initial = catalog.snapshot(); const auto id = initial->songs[0].id;
  const auto fallback = catalog.display(initial, id);
  OPENHDK_FAIL_IF(2, !fallback.succeeded() || fallback.display->title.text != "Live.Set"
      || fallback.display->title.origin != CatalogDisplayOrigin::FilenameFallback || fallback.display->title.source
      || fallback.display->artist || initial->songs[0].metadata || initial->songs[0].overrides);
  const auto set = catalog.replaceUserOverrides(id, {" User / Title ", "Artist-Name"});
  const auto edited = catalog.snapshot();
  OPENHDK_FAIL_IF(3, !set.succeeded() || set.status != CatalogOverrideStatus::Updated
      || edited->revision != initial->revision + 1U || edited->roots[0].attachmentGeneration != 1U
      || edited->songs[0].id != id || edited->songs[0].sourceRevision() != token
      || edited->songs[0].state != CatalogState::Ready || initial->songs[0].overrides);
  const auto display = catalog.display(edited, id);
  OPENHDK_FAIL_IF(4, display.display->title.text != " User / Title " || display.display->artist->text != "Artist-Name"
      || display.display->title.origin != CatalogDisplayOrigin::UserOverride || display.display->title.source
      || display.display->artist->origin != CatalogDisplayOrigin::UserOverride);
  OPENHDK_FAIL_IF(5, catalog.replaceUserOverrides(id, {" User / Title ", "Artist-Name"}).status != CatalogOverrideStatus::Unchanged
      || catalog.snapshot() != edited);
  for (const auto& bad : {std::string(""), std::string("a\0b",3U), std::string("a\x01",2U),
                         std::string("\xed\xa0\x80",3U), std::string("\xf4\x90\x80\x80",4U)}) {
    const auto rejected = catalog.replaceUserOverrides(id, {"Would change", bad});
    OPENHDK_FAIL_IF(6, rejected.succeeded() || !rejected.error || rejected.error->code != CatalogMetadataErrorCode::InvalidText
        || rejected.error->field != CatalogMetadataField::Artist || !rejected.error->byteOffset || catalog.snapshot() != edited);
  }
  OPENHDK_FAIL_IF(7, catalog.replaceUserOverrides(id, {"", {}}).error->field != CatalogMetadataField::Title
      || catalog.snapshot() != edited);
  const std::string exact(4096U,'T'), beyond(4097U,'T');
  OPENHDK_FAIL_IF(8, catalog.replaceUserOverrides(id, {beyond, {}}).error->code != CatalogMetadataErrorCode::LimitExceeded
      || catalog.snapshot() != edited);
  OPENHDK_FAIL_IF(9, !catalog.replaceUserOverrides(id, {exact, exact}).succeeded()
      || catalog.snapshot()->songs[0].overrides->title->bytes().size() != 4096U
      || catalog.snapshot()->songs[0].overrides->artist->bytes().size() != 4096U);
  const auto beforeConfig = catalog.snapshot();
  for (const auto limits : {CatalogMetadataLimits{0U,64U}, {4097U,64U}, {4096U,0U},
                          {4096U,MetadataPayloadBudget::kMaxBytes+1U}}) {
    OPENHDK_FAIL_IF(10, catalog.replaceUserOverrides(id, {}, limits).error->code != CatalogMetadataErrorCode::InvalidConfiguration
        || catalog.display(beforeConfig, id, limits).error->code != CatalogMetadataErrorCode::InvalidConfiguration
        || catalog.snapshot() != beforeConfig);
  }
  OPENHDK_FAIL_IF(11, catalog.replaceUserOverrides(id, {"abcd",{}}, {3U,65536U}).error->code != CatalogMetadataErrorCode::LimitExceeded
      || catalog.snapshot() != beforeConfig);
  OPENHDK_FAIL_IF(12, !catalog.replaceUserOverrides(id, {{}, " \t\r\n"}).succeeded()
      || catalog.snapshot()->songs[0].overrides->title || catalog.display(catalog.snapshot(),id).display->title.text != "Live.Set"
      || catalog.display(catalog.snapshot(),id).display->artist->text != " \t\r\n");
  OPENHDK_FAIL_IF(13, !catalog.replaceUserOverrides(id, {}).succeeded() || catalog.snapshot()->songs[0].overrides
      || catalog.replaceUserOverrides(id, {}).status != CatalogOverrideStatus::Unchanged);
  const std::string thai = "\xe0\xb8\x81", composed = "\xc3\xa9", decomposed = "e\xcc\x81";
  OPENHDK_FAIL_IF(14, !catalog.replaceUserOverrides(id, {thai, decomposed}).succeeded()
      || catalog.display(catalog.snapshot(), id).display->title.text != thai
      || catalog.display(catalog.snapshot(), id).display->artist->text != decomposed);
  OPENHDK_FAIL_IF(15, !catalog.replaceUserOverrides(id, {composed, decomposed}).succeeded()
      || catalog.snapshot()->songs[0].overrides->title->bytes() == catalog.snapshot()->songs[0].overrides->artist->bytes());
  const auto retained = catalog.snapshot();
  OPENHDK_FAIL_IF(16, catalog.commitScan(root, {{"dir/Live.Set.kar",CatalogState::Invalid,token}},true) != CatalogError::None
      || catalog.snapshot()->songs[0].overrides != retained->songs[0].overrides
      || catalog.display(catalog.snapshot(),id).display->title.text != composed);
  OPENHDK_FAIL_IF(17, catalog.commitScan(root, {}, true) != CatalogError::None
      || catalog.snapshot()->songs[0].state != CatalogState::Missing || catalog.snapshot()->songs[0].sourceRevision()
      || catalog.snapshot()->songs[0].overrides != retained->songs[0].overrides);
  OPENHDK_FAIL_IF(18, catalog.relocate(id,root,"new.mid") != CatalogError::None
      || catalog.snapshot()->songs[0].overrides != retained->songs[0].overrides
      || catalog.snapshot()->songs[0].state != CatalogState::Invalid);
  OPENHDK_FAIL_IF(19, catalog.remove(id) != CatalogError::None
      || catalog.replaceUserOverrides(id, {}).error->code != CatalogMetadataErrorCode::NotFound
      || catalog.display(catalog.snapshot(),id).error->code != CatalogMetadataErrorCode::NotFound);
  OPENHDK_FAIL_IF(20, catalog.commitScan(root, {{"new.mid"}},true) != CatalogError::None
      || catalog.snapshot()->songs[0].id == id || catalog.snapshot()->songs[0].overrides
      || catalog.display(retained,id).display->title.text != composed);
  SongCatalog foreign; const auto foreignRoot = *foreign.addRoot();
  OPENHDK_FAIL_IF(21, foreign.commitScan(foreignRoot, {{"new.mid"}},true) != CatalogError::None
      || catalog.display(foreign.snapshot(),foreign.snapshot()->songs[0].id).error->code != CatalogMetadataErrorCode::InvalidConfiguration
      || catalog.display(std::make_shared<CatalogSnapshot>(),id).error->code != CatalogMetadataErrorCode::InvalidConfiguration);
  SongCatalog bounded; const auto boundedRoot = *bounded.addRoot();
  OPENHDK_FAIL_IF(22, bounded.commitScan(boundedRoot, {{"a.mid"}},true) != CatalogError::None);
  const auto shortId = bounded.snapshot()->songs[0].id; const auto beforeBound = bounded.snapshot();
  // Two 5-byte locators + two validation/output copies each of A and B = 14.
  OPENHDK_FAIL_IF(23, bounded.replaceUserOverrides(shortId,{"A","B"},{4096U,13U}).error->code != CatalogMetadataErrorCode::LimitExceeded
      || bounded.snapshot() != beforeBound || !bounded.replaceUserOverrides(shortId,{"A","B"},{4096U,14U}).succeeded());
  // Shared override payload 2 + locator 5 + two display copies per field = 11.
  OPENHDK_FAIL_IF(24, bounded.display(bounded.snapshot(),shortId,{4096U,10U}).error->code != CatalogMetadataErrorCode::LimitExceeded
      || !bounded.display(bounded.snapshot(),shortId,{4096U,11U}).succeeded());
  // Two locators + old override payload 2 + two copies of replacement C = 14.
  const auto withBoth = bounded.snapshot();
  OPENHDK_FAIL_IF(25, bounded.replaceUserOverrides(shortId,{"C",{}},{4096U,13U}).error->code != CatalogMetadataErrorCode::LimitExceeded
      || bounded.snapshot()!=withBoth || !bounded.replaceUserOverrides(shortId,{"C",{}},{4096U,14U}).succeeded()
      || bounded.snapshot()->songs[0].overrides->artist);
  auto ledger = *MetadataPayloadBudget::create(1U); std::set<const CatalogUserOverrides*> charged;
  OPENHDK_FAIL_IF(26, chargeUserOverrides(ledger,withBoth->songs[0].overrides,charged) || ledger.used()!=0U || !charged.empty()
      || chargeUserOverrides(ledger,withBoth->songs[0].overrides,charged));
  ledger = *MetadataPayloadBudget::create(2U);
  OPENHDK_FAIL_IF(27, !chargeUserOverrides(ledger,withBoth->songs[0].overrides,charged)
      || !chargeUserOverrides(ledger,withBoth->songs[0].overrides,charged) || ledger.used()!=2U);
  CatalogLimits scanLimit; scanLimit.stagedLocatorBytes=30U;
  const auto beforeScan = bounded.snapshot();
  OPENHDK_FAIL_IF(28, bounded.commitScan(boundedRoot,{{"longer.mid"}},true,scanLimit)!=CatalogError::LimitExceeded
      || bounded.snapshot()!=beforeScan); // 10 old + 20 incoming + 1 override = 31.
  CatalogMetadataTransactionTestAccess::revision(bounded,std::numeric_limits<std::uint64_t>::max());
  const auto exhausted = bounded.snapshot();
  OPENHDK_FAIL_IF(29, bounded.replaceUserOverrides(shortId,{"D",{}}).error->code != CatalogMetadataErrorCode::RevisionExhausted
      || bounded.snapshot()!=exhausted || bounded.replaceUserOverrides(shortId,{"C",{}}).status!=CatalogOverrideStatus::Unchanged);
  // Real allocator-failure sweep: each failure must retain both fields/revision.
  SongCatalog faults; const auto faultRoot=*faults.addRoot();
  OPENHDK_FAIL_IF(30, faults.commitScan(faultRoot,{{"fault.mid"}},true)!=CatalogError::None);
  const auto faultId=faults.snapshot()->songs[0].id;
  const std::string longTitle(64U,'X'), longArtist(64U,'Y');
  const auto beforeFaults=faults.snapshot(); unsigned failures=0U; bool completed=false;
  for (std::ptrdiff_t point=0; point<100; ++point) {
    allocationsBeforeFailure=point;
    const auto result=faults.replaceUserOverrides(faultId,{longTitle,longArtist});
    allocationsBeforeFailure=-1;
    if (result.succeeded()) { completed=true; break; }
    ++failures;
    OPENHDK_FAIL_IF(31, result.error->code!=CatalogMetadataErrorCode::StorageFailure || faults.snapshot()!=beforeFaults);
  }
  OPENHDK_FAIL_IF(32, !completed || failures<5U || faults.snapshot()->revision!=beforeFaults->revision+1U
      || faults.snapshot()->songs[0].overrides->title->bytes()!=longTitle || faults.snapshot()->songs[0].overrides->artist->bytes()!=longArtist);
  completed=false; failures=0U;
  for (std::ptrdiff_t point=0; point<100; ++point) {
    allocationsBeforeFailure=point; const auto result=faults.display(faults.snapshot(),faultId); allocationsBeforeFailure=-1;
    if (result.succeeded()) { completed=true; break; }
    ++failures;
    OPENHDK_FAIL_IF(33, result.display || result.error->code!=CatalogMetadataErrorCode::StorageFailure);
  }
  OPENHDK_FAIL_IF(34, !completed || failures<5U);
  // Real scan -> source title -> prepared effective-selection display contexts.
  TemporaryRoot temp; write(temp.path/"song.kar",smf({named(" Source/Artist ")}));
  SongDiscovery library; const auto registered=*library.registerRoot(temp.path).root;
  OPENHDK_FAIL_IF(35, !library.scan(registered).succeeded());
  const auto acquired=library.snapshot(); const auto songId=acquired->songs[0].id;
  const auto sourceDisplay=library.display(acquired,songId);
  OPENHDK_FAIL_IF(36, !sourceDisplay.succeeded() || sourceDisplay.display->title.text!=" Source/Artist "
      || sourceDisplay.display->title.origin!=CatalogDisplayOrigin::SourceTitle || !sourceDisplay.display->title.source
      || sourceDisplay.display->title.source->revision!=*acquired->songs[0].sourceRevision()
      || sourceDisplay.display->title.source->position!=LyricSourcePosition{0U,0U,0U,0U} || sourceDisplay.display->artist);
  OPENHDK_FAIL_IF(37, !library.replaceUserOverrides(songId,{"User","Singer"}).succeeded());
  const auto overridden=library.snapshot(); const auto prepared=library.prepare(overridden,songId);
  OPENHDK_FAIL_IF(38, !prepared.succeeded() || prepared.prepared->displayMetadata().title.text!="User"
      || prepared.prepared->displayMetadata().artist->text!="Singer"
      || prepared.prepared->sourceMetadata().title->text.bytes()!=" Source/Artist ");
  OPENHDK_FAIL_IF(39, !library.replaceUserOverrides(songId,{}).succeeded()
      || prepared.prepared->displayMetadata().title.text!="User"
      || prepared.prepared->song().overrides->artist->bytes()!="Singer"
      || library.display(overridden,songId).display->title.text!="User");
  const auto sourcePrepared=library.prepare(library.snapshot(),songId);
  OPENHDK_FAIL_IF(40, !sourcePrepared.succeeded() || sourcePrepared.prepared->displayMetadata().title.origin!=CatalogDisplayOrigin::SourceTitle
      || sourcePrepared.prepared->displayMetadata().title.text!=" Source/Artist ");
  OPENHDK_FAIL_IF(41, !library.replaceUserOverrides(songId,{"Retain",{}}).succeeded());
  const auto beforeMissing=library.snapshot(); fs::remove(temp.path/"song.kar");
  OPENHDK_FAIL_IF(42, !library.scan(registered).succeeded() || library.snapshot()->songs[0].state!=CatalogState::Missing
      || library.snapshot()->songs[0].metadata || library.display(library.snapshot(),songId).display->title.text!="Retain"
      || library.snapshot()->songs[0].overrides!=beforeMissing->songs[0].overrides);
  const auto newRoot=temp.path/"new"; fs::create_directory(newRoot); write(newRoot/"song.kar",smf({named("New source")}));
  OPENHDK_FAIL_IF(43, !library.reattachRoot(registered,newRoot).succeeded()
      || library.snapshot()->songs[0].overrides!=beforeMissing->songs[0].overrides
      || library.snapshot()->songs[0].metadata || !library.scan(registered).succeeded()
      || library.snapshot()->songs[0].id!=songId || library.display(library.snapshot(),songId).display->title.text!="Retain");
  OPENHDK_FAIL_IF(44, sourcePrepared.prepared->displayMetadata().title.text!=" Source/Artist "
      || sourcePrepared.prepared->sourceMetadata().title->text.bytes()!=" Source/Artist "
      || fallback.display->title.text!="Live.Set" || retained->songs[0].overrides->artist->bytes()!=decomposed);
  // Prepared metadata must use effective track selection, not cached root title.
  TemporaryRoot selection; Bytes lyric; meta(lyric,5U,"B"); meta(lyric,0x2fU,"");
  write(selection.path/"choice.mid",smf({named("Hidden"),lyric}));
  SongDiscovery choices; const auto choiceRoot=*choices.registerRoot(selection.path).root;
  OPENHDK_FAIL_IF(45, !choices.scan(choiceRoot).succeeded());
  const auto choiceSnapshot=choices.snapshot(); const auto choiceId=choiceSnapshot->songs[0].id;
  PreparationOptions options; options.lyricOverride=LyricSelectionPolicy{LyricTextEncoding::Utf8,0U};
  const auto choice=choices.prepare(choiceSnapshot,choiceId,options);
  OPENHDK_FAIL_IF(46, !choice.succeeded() || choice.prepared->displayMetadata().title.text!="Hidden"
      || choice.prepared->displayMetadata().title.origin!=CatalogDisplayOrigin::SourceTitle
      || choice.prepared->displayMetadata().title.source->effectivePolicy!=*options.lyricOverride
      || choices.display(choiceSnapshot,choiceId).display->title.text!="choice"
      || choiceSnapshot->songs[0].metadata->title);
  // Hidden filename, multiple extensions, spelling and UTF-8 fallback preserved.
  SongCatalog names; const auto nameRoot=*names.addRoot();
  OPENHDK_FAIL_IF(47, names.commitScan(nameRoot,{{".mid"},{"dir/Live.Set.kar"},{"เพลง.mid"}},true)!=CatalogError::None);
  for (const auto& item:names.snapshot()->songs) {
    const auto actual=names.display(names.snapshot(),item.id);
    const auto expected=item.locator()==".mid" ? ".mid" : item.locator()=="dir/Live.Set.kar" ? "Live.Set" : "เพลง";
    OPENHDK_FAIL_IF(48, !actual.succeeded() || actual.display->title.text!=expected);
  }

  const auto ownerless = [] {
    SongCatalog owner; const auto r=*owner.addRoot();
    if(owner.commitScan(r,{{"owned.mid"}},true)!=CatalogError::None) throw std::runtime_error("catalog fixture");
    const auto ownId=owner.snapshot()->songs[0].id;
    if(!owner.replaceUserOverrides(ownId,{"Survives","Owner"}).succeeded()) throw std::runtime_error("override fixture");
    return owner.display(owner.snapshot(),ownId).display;
  }();
  OPENHDK_FAIL_IF(49, ownerless->title.text!="Survives" || ownerless->artist->text!="Owner");
  const auto ownerlessPrepared = [] {
    TemporaryRoot owned;write(owned.path/"own.kar",smf({named("Source")}));
    SongDiscovery owner;const auto r=*owner.registerRoot(owned.path).root;
    if(!owner.scan(r).succeeded()) throw std::runtime_error("scan fixture");
    const auto ownId=owner.snapshot()->songs[0].id;
    if(!owner.replaceUserOverrides(ownId,{"Saved","Artist"}).succeeded()) throw std::runtime_error("override fixture");
    const auto result=owner.prepare(owner.snapshot(),ownId);
    if(!result.succeeded()) throw std::runtime_error("prepare fixture");
    return result.prepared;
  }();
  OPENHDK_FAIL_IF(50, ownerlessPrepared->displayMetadata().title.text!="Saved"
      || ownerlessPrepared->song().overrides->artist->bytes()!="Artist"
      || ownerlessPrepared->sourceMetadata().title->text.bytes()!="Source");
  TemporaryRoot noLyricsDir;Bytes emptyTrack;meta(emptyTrack,0x2fU,"");write(noLyricsDir.path/"a.mid",smf({emptyTrack}));
  SongDiscovery noLyrics;const auto noRoot=*noLyrics.registerRoot(noLyricsDir.path).root;
  OPENHDK_FAIL_IF(51, !noLyrics.scan(noRoot).succeeded());
  const auto noId=noLyrics.snapshot()->songs[0].id;
  OPENHDK_FAIL_IF(52, !noLyrics.replaceUserOverrides(noId,{"A","B"}).succeeded());
  DiscoveryLimits scanBudget;scanBudget.catalog.stagedLocatorBytes=36U;const auto noBefore=noLyrics.snapshot();
  OPENHDK_FAIL_IF(53, noLyrics.scan(noRoot,scanBudget).error->code!=DiscoveryError::LimitExceeded || noLyrics.snapshot()!=noBefore);
  scanBudget.catalog.stagedLocatorBytes=37U; // 2 old + 5 incoming locator copies + shared override payload 2.
  OPENHDK_FAIL_IF(54, !noLyrics.scan(noRoot,scanBudget).succeeded());
  PreparationOptions prepareBudget;prepareBudget.stagedBytes=15U;
  OPENHDK_FAIL_IF(55, noLyrics.prepare(noLyrics.snapshot(),noId,prepareBudget).error->code!=PreparationErrorCode::LimitExceeded);
  prepareBudget.stagedBytes=16U; // 5 retained + 5 error reserve + 2 override + 2*(1+1) display coexistence.
  OPENHDK_FAIL_IF(56, !noLyrics.prepare(noLyrics.snapshot(),noId,prepareBudget).succeeded());
  auto unchangedLedger=*MetadataPayloadBudget::create(1U);
  const auto failedDisplay=resolveCatalogDisplay("abc.mid",nullptr,nullptr,4096U,unchangedLedger);
  OPENHDK_FAIL_IF(57, failedDisplay.succeeded() || failedDisplay.display || unchangedLedger.used()!=0U);
  return 0;
}
