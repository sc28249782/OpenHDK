// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/CatalogMetadata.hpp"
#include "tests/TestCheck.hpp"
#include <limits>
#include <type_traits>

int main() {
  using namespace OpenHDK;
  static_assert(isValidRootSourcePolicy({}));
  static_assert(!isValidRootSourcePolicy({static_cast<LibrarySourceMode>(999), {}}));
  static_assert(!isValidLyricSelectionPolicy({static_cast<LyricTextEncoding>(999), {}}));
  static_assert(noexcept(isValidRootSourcePolicy({})));
  static_assert(!std::is_default_constructible_v<MetadataText>);
  static_assert(noexcept(std::declval<MetadataPayloadBudget&>().charge(0U)));
  const auto maximum = std::numeric_limits<std::size_t>::max();
  const LyricSelectionPolicy tis{LyricTextEncoding::Tis620, 7U};
  OPENHDK_FAIL_IF(1, !isValidRootSourcePolicy({LibrarySourceMode::SmfKar, tis}));
  OPENHDK_FAIL_IF(2, !isValidLyricSelectionPolicy({LyricTextEncoding::Utf8, maximum}));
  const auto defaults = karOptionsForPolicy({});
  OPENHDK_FAIL_IF(3, !defaults || defaults->encoding != LyricTextEncoding::Utf8 || defaults->trackIndex);
  const KarLyricLimits limits{1U, 2U, 3U, 4U};
  const auto selected = karOptionsForPolicy(tis, limits);
  OPENHDK_FAIL_IF(4, !selected || selected->encoding != LyricTextEncoding::Tis620 || selected->trackIndex != 7U
      || selected->limits.sourceBytes != 1U || selected->limits.cues != 2U
      || selected->limits.titleBytes != 3U || selected->limits.stagedBytes != 4U);
  OPENHDK_FAIL_IF(5, karOptionsForPolicy({static_cast<LyricTextEncoding>(999), 0U}));

  // Exact bytes, not normalized or trimmed. This also exercises multibyte bounds.
  const std::string original = " \xef\xbb\xbfเพลง e\xcc\x81 É\t\r\n ";
  auto text = MetadataText::create(original);
  OPENHDK_FAIL_IF(6, !text.succeeded() || text.text()->bytes() != original || text.error());
  auto exact = MetadataText::create("ก", 3U);
  OPENHDK_FAIL_IF(7, !exact.succeeded() || exact.text()->bytes() != "ก");
  auto small = MetadataText::create("ก", 2U);
  OPENHDK_FAIL_IF(8, small.succeeded() || small.error() != MetadataTextError{MetadataTextErrorCode::LimitExceeded, 2U});
  for (const auto limit : {0U, 4097U}) {
    const auto invalid = MetadataText::create("A", limit);
    OPENHDK_FAIL_IF(9, invalid.succeeded() || invalid.error()->code != MetadataTextErrorCode::InvalidConfiguration);
  }
  auto empty = MetadataText::create("");
  OPENHDK_FAIL_IF(10, empty.succeeded() || empty.text() || empty.error()->code != MetadataTextErrorCode::InvalidText);
  const std::string boundary(4096U, 'A');
  OPENHDK_FAIL_IF(11, !MetadataText::create(boundary).succeeded());
  auto beyond = MetadataText::create(boundary + "A");
  OPENHDK_FAIL_IF(12, beyond.succeeded() || beyond.text() || beyond.error()->code != MetadataTextErrorCode::LimitExceeded);
  // Independent fixed malformed cases and positions, including payload-end truncation.
  const std::pair<std::string, std::size_t> invalid[] = {
      {std::string("A\0B", 3U), 1U}, {"\x7f\xc0\xaf", 1U}, {"\xed\xa0\x80", 1U},
      {"\xf4\x90\x80\x80", 1U}, {"\xe0\xa0", 2U}, {"A\xe1\x80" "B", 3U}, {"\x80", 0U}};
  for (const auto& [bytes, offset] : invalid) {
    const auto result = MetadataText::create(bytes);
    OPENHDK_FAIL_IF(13, result.succeeded() || result.text()
        || result.error() != MetadataTextError{MetadataTextErrorCode::InvalidText, offset});
  }
  for (unsigned char control = 0U; control < 32U; ++control) {
    const auto result = MetadataText::create(std::string(1U, static_cast<char>(control)));
    const bool allowed = control == 9U || control == 10U || control == 13U;
    OPENHDK_FAIL_IF(14, result.succeeded() != allowed);
  }
  OPENHDK_FAIL_IF(15, !MetadataText::create("\x7f\xc2\x80").succeeded());
  OPENHDK_FAIL_IF(16, !MetadataText::create("   ").succeeded());
  // TIS bytes must not silently become a UTF-8 override.
  OPENHDK_FAIL_IF(17, MetadataText::create("\xa1\xe8").succeeded());

  CatalogUserOverrides absent;
  CatalogUserOverrides titleOnly{*text.text(), std::nullopt};
  const CatalogUserOverrides copied = titleOnly;
  titleOnly.title.reset();
  OPENHDK_FAIL_IF(18, titleOnly != absent || copied.title->bytes() != original || copied.artist);
  CatalogUserOverrides both{*exact.text(), *MetadataText::create("Artist").text()};
  OPENHDK_FAIL_IF(19, both == copied || both.artist->bytes() != "Artist");

  CatalogLyricSummary summary;
  OPENHDK_FAIL_IF(20, !isValidCatalogLyricSummary(summary));
  summary.selectedTrack = 0U;
  OPENHDK_FAIL_IF(21, isValidCatalogLyricSummary(summary));
  summary.selectedTrack.reset(); summary.cueCount = 1U;
  OPENHDK_FAIL_IF(22, isValidCatalogLyricSummary(summary));
  for (const auto profile : {KarLyricProfile::LyricMeta, KarLyricProfile::NamedText}) {
    summary = {profile, 7U, 1U, tis}; // One action-only cue is still available lyrics.
    OPENHDK_FAIL_IF(23, !isValidCatalogLyricSummary(summary));
    summary.selectedTrack = 6U;
    OPENHDK_FAIL_IF(24, isValidCatalogLyricSummary(summary));
    summary.effectivePolicy.trackIndex.reset();
    OPENHDK_FAIL_IF(25, !isValidCatalogLyricSummary(summary));
    summary.selectedTrack.reset();
    OPENHDK_FAIL_IF(26, isValidCatalogLyricSummary(summary));
    summary.selectedTrack = 7U; summary.cueCount = 0U;
    OPENHDK_FAIL_IF(27, isValidCatalogLyricSummary(summary));
  }
  summary = {static_cast<KarLyricProfile>(999), 0U, 1U, {}};
  OPENHDK_FAIL_IF(28, isValidCatalogLyricSummary(summary));
  summary = {KarLyricProfile::NoLyrics, {}, 0U, {static_cast<LyricTextEncoding>(999), {}}};
  OPENHDK_FAIL_IF(29, isValidCatalogLyricSummary(summary));

  // Owning source title/provenance survives the input string and local results.
  const auto source = [&] {
    std::string input = " Title/Artist ";
    const auto owned = MetadataText::create(input);
    SourceRevision revision{}; revision.byteCount = 42U; revision.sha256[0] = 99U;
    return SourceTitleMetadata{*owned.text(), {SourceMemberRole::PrimaryMidi, revision, tis, {3U, 416666U, 7U, 4U}}};
  }();
  OPENHDK_FAIL_IF(30, source.text.bytes() != " Title/Artist " || source.provenance.revision.byteCount != 42U
      || source.provenance.revision.sha256[0] != 99U || source.provenance.effectivePolicy != tis
      || source.provenance.position != LyricSourcePosition{3U, 416666U, 7U, 4U});

  OPENHDK_FAIL_IF(31, MetadataPayloadBudget::create(0U) || MetadataPayloadBudget::create(maximum)
      || MetadataPayloadBudget::create(MetadataPayloadBudget::kMaxBytes + 1U)
      || MetadataPayloadBudget::create(5U, 6U));
  auto budget = *MetadataPayloadBudget::create(10U, 4U);
  OPENHDK_FAIL_IF(32, budget.used() != 4U || budget.remaining() != 6U || !budget.charge(6U)
      || budget.used() != 10U || budget.remaining() != 0U);
  OPENHDK_FAIL_IF(33, budget.charge(1U) || budget.charge(maximum) || budget.used() != 10U || !budget.charge(0U));
  auto full = *MetadataPayloadBudget::create();
  OPENHDK_FAIL_IF(34, !full.charge(MetadataPayloadBudget::kMaxBytes) || full.charge(1U));
  auto coexist = *MetadataPayloadBudget::create(9U, 3U);
  OPENHDK_FAIL_IF(35, !coexist.charge(3U) || !coexist.charge(3U) || coexist.charge(1U));
  return 0;
}
