// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/SongCatalog.hpp"
#include "tests/TestCheck.hpp"
#include <type_traits>

int main() {
  using namespace OpenHDK;
  static_assert(!std::is_default_constructible_v<SongId>);
  static_assert(!std::is_copy_constructible_v<SongCatalog>);
  for (const auto path : {"", "/a.mid", "a//b.mid", "a/../b.mid", "./a.mid",
                          "a/", "C:/a.mid", "a\\b.mid"}) {
    OPENHDK_FAIL_IF(1, isCatalogLocator(path));
  }
  OPENHDK_FAIL_IF(2, isCatalogLocator(std::string("a\0.mid", 6U)));
  for (const auto& text : {std::string("\xc0\xaf"), std::string("\xed\xa0\x80"),
                          std::string("\xf4\x90\x80\x80"), std::string("\xe0\xa0")}) {
    OPENHDK_FAIL_IF(3, isCatalogLocator(text));
  }
  OPENHDK_FAIL_IF(4, !isCatalogLocator("เพลง/ก.mid") || !isCatalogLocator("a b.mid"));
  OPENHDK_FAIL_IF(5, !catalogByteLess("z", "เพลง"));

  SongCatalog catalog;
  const auto root = *catalog.addRoot();
  const auto otherRoot = *catalog.addRoot();
  const auto empty = catalog.snapshot();
  OPENHDK_FAIL_IF(6, catalog.commitScan(root, {{"z.mid"}, {"a.mid"}}, true) != CatalogError::None);
  const auto first = catalog.snapshot();
  const auto a = first->songs[0].id;
  const auto z = first->songs[1].id;
  OPENHDK_FAIL_IF(7, first->revision != empty->revision + 1U || first->songs[0].locator() != "a.mid" || a == z);
  OPENHDK_FAIL_IF(8, empty->revision != 2U || !empty->songs.empty());
  OPENHDK_FAIL_IF(9, catalog.commitScan(root, {{"a.mid", CatalogState::Invalid}, {"z.mid"}}, true) != CatalogError::None);
  OPENHDK_FAIL_IF(10, catalog.snapshot()->songs[0].id != a || catalog.snapshot()->songs[0].state != CatalogState::Invalid);
  OPENHDK_FAIL_IF(11, first->songs[0].state != CatalogState::Ready);
  auto before = catalog.snapshot();
  OPENHDK_FAIL_IF(12, catalog.commitScan(root, {{"a.mid"}}, false) != CatalogError::IncompleteScan || catalog.snapshot() != before);
  OPENHDK_FAIL_IF(13, catalog.commitScan(root, {{"A.mid"}, {"a.mid"}}, true) != CatalogError::AmbiguousPath || catalog.snapshot() != before);
  OPENHDK_FAIL_IF(14, catalog.commitScan(root, {{"new.mid"}, {"new.mid"}}, true) != CatalogError::AmbiguousPath || catalog.snapshot() != before);
  OPENHDK_FAIL_IF(15, catalog.commitScan(root, {{"a.mid"}, {"z.mid"}}, true, {1U, 4096U, 64U * 1048576U}) != CatalogError::LimitExceeded || catalog.snapshot() != before);
  OPENHDK_FAIL_IF(16, catalog.commitScan(root, {{"a.mid"}}, true, {1U, 4U, 5U}) != CatalogError::LimitExceeded || catalog.snapshot() != before);
  OPENHDK_FAIL_IF(17, catalog.commitScan(root, {{"a.mid"}}, true, {1U, 5U, 4U}) != CatalogError::LimitExceeded || catalog.snapshot() != before);
  OPENHDK_FAIL_IF(18, catalog.commitScan(root, {{"a.mid"}}, true, {0U, 5U, 5U}) != CatalogError::LimitExceeded || catalog.snapshot() != before);
  OPENHDK_FAIL_IF(19, catalog.commitScan(root, {{"a.mid"}}, true, {10001U, 5U, 5U}) != CatalogError::LimitExceeded || catalog.snapshot() != before);
  // Two existing 5-byte locator copies + two incoming copies = 30 bytes.
  OPENHDK_FAIL_IF(20, catalog.commitScan(root, {{"a.mid"}}, true, {1U, 5U, 30U}) != CatalogError::None);
  OPENHDK_FAIL_IF(21, catalog.snapshot()->songs[1].id != z || catalog.snapshot()->songs[1].state != CatalogState::Missing);
  before = catalog.snapshot();
  OPENHDK_FAIL_IF(22, catalog.commitScan(root, {{"Z.mid"}}, true) != CatalogError::AmbiguousPath || catalog.snapshot() != before);
  OPENHDK_FAIL_IF(23, catalog.relocate(a, root, "Z.mid") != CatalogError::AmbiguousPath || catalog.snapshot() != before);
  OPENHDK_FAIL_IF(24, catalog.relocate(z, root, "Z.mid") != CatalogError::None);
  OPENHDK_FAIL_IF(25, catalog.snapshot()->songs[1].id != z || catalog.snapshot()->songs[1].locator() != "Z.mid" || catalog.snapshot()->songs[1].state != CatalogState::Invalid);
  OPENHDK_FAIL_IF(26, catalog.commitScan(root, {{"a.mid"}, {"Z.mid"}}, true) != CatalogError::None || catalog.snapshot()->songs[1].state != CatalogState::Ready);
  OPENHDK_FAIL_IF(27, catalog.commitScan(otherRoot, {{"a.mid", CatalogState::UnsupportedProfile}}, true) != CatalogError::None);
  OPENHDK_FAIL_IF(28, catalog.snapshot()->songs[2].id == a || catalog.snapshot()->songs[2].state != CatalogState::UnsupportedProfile);
  OPENHDK_FAIL_IF(29, catalog.commitScan(root, {}, true) != CatalogError::None || catalog.snapshot()->songs[2].state != CatalogState::UnsupportedProfile);
  before = catalog.snapshot();
  OPENHDK_FAIL_IF(30, catalog.commitScan(root, {{"bad.mid", CatalogState::Missing}}, true) != CatalogError::InvalidCandidate || catalog.snapshot() != before);
  OPENHDK_FAIL_IF(31, catalog.commitScan(root, {{"../bad.mid"}}, true) != CatalogError::InvalidLocator || catalog.snapshot() != before);

  // Enumeration order never affects the first committed assignment.
  SongCatalog forward, reverse;
  const auto rf = *forward.addRoot(), rr = *reverse.addRoot();
  OPENHDK_FAIL_IF(32, forward.commitScan(rf, {{"a.mid"}, {"b.mid"}, {"c.mid"}}, true) != CatalogError::None);
  OPENHDK_FAIL_IF(33, reverse.commitScan(rr, {{"c.mid"}, {"a.mid"}, {"b.mid"}}, true) != CatalogError::None);
  for (std::size_t i = 0U; i < 3U; ++i) {
    OPENHDK_FAIL_IF(34, forward.snapshot()->songs[i].id != reverse.snapshot()->songs[i].id || forward.snapshot()->songs[i].locator() != reverse.snapshot()->songs[i].locator());
  }
  // Failed transactions must not consume a song ID.
  SongCatalog failed, clean;
  const auto rfailed = *failed.addRoot(), rclean = *clean.addRoot();
  OPENHDK_FAIL_IF(35, failed.commitScan(rfailed, {{"a.mid"}, {"A.mid"}}, true) != CatalogError::AmbiguousPath);
  OPENHDK_FAIL_IF(36, failed.commitScan(rfailed, {{"ok.mid"}}, true) != CatalogError::None || clean.commitScan(rclean, {{"ok.mid"}}, true) != CatalogError::None);
  OPENHDK_FAIL_IF(37, failed.snapshot()->songs[0].id != clean.snapshot()->songs[0].id);
  const auto retained = catalog.snapshot();
  OPENHDK_FAIL_IF(38, catalog.remove(a) != CatalogError::None || catalog.snapshot()->songs.size() != 2U || retained->songs.size() != 3U);
  before = catalog.snapshot();
  OPENHDK_FAIL_IF(39, catalog.remove(a) != CatalogError::NotFound || catalog.relocate(a, root, "again.mid") != CatalogError::NotFound || catalog.snapshot() != before);
  OPENHDK_FAIL_IF(40, catalog.commitScan(root, {{"a.mid"}}, true) != CatalogError::None || catalog.snapshot()->songs.back().id == a);
  SongCatalog foreign;
  foreign.addRoot(); foreign.addRoot();
  const auto unknownRoot = *foreign.addRoot();
  before = catalog.snapshot();
  OPENHDK_FAIL_IF(41, catalog.commitScan(unknownRoot, {}, true) != CatalogError::UnknownRoot || catalog.relocate(z, unknownRoot, "z.mid") != CatalogError::UnknownRoot || catalog.snapshot() != before);
  OPENHDK_FAIL_IF(42, catalog.relocate(z, root, "../x.mid") != CatalogError::InvalidLocator || catalog.snapshot() != before);
  // Roll back after staging a new song, not only during input validation.
  SongCatalog lateFailure, reference;
  const auto lf = *lateFailure.addRoot(), ref = *reference.addRoot();
  OPENHDK_FAIL_IF(43, lateFailure.commitScan(lf, {{"z.mid"}}, true) != CatalogError::None || reference.commitScan(ref, {{"z.mid"}}, true) != CatalogError::None);
  const auto prior = lateFailure.snapshot();
  OPENHDK_FAIL_IF(44, lateFailure.commitScan(lf, {{"Anew.mid"}, {"Z.mid"}}, true) != CatalogError::AmbiguousPath || lateFailure.snapshot() != prior);
  OPENHDK_FAIL_IF(45, lateFailure.commitScan(lf, {{"Anew.mid"}, {"z.mid"}}, true) != CatalogError::None || reference.commitScan(ref, {{"Anew.mid"}, {"z.mid"}}, true) != CatalogError::None);
  OPENHDK_FAIL_IF(46, lateFailure.snapshot()->songs[1].id != reference.snapshot()->songs[1].id);
  SongCatalog boundary;
  const auto rb = *boundary.addRoot();
  const std::string maxLocator = std::string(4092U, 'a') + ".mid";
  OPENHDK_FAIL_IF(47, boundary.commitScan(rb, {{maxLocator}}, true) != CatalogError::None);
  const auto boundaryBefore = boundary.snapshot();
  OPENHDK_FAIL_IF(48, boundary.commitScan(rb, {{maxLocator + "x"}}, true) != CatalogError::LimitExceeded || boundary.snapshot() != boundaryBefore);
  // Existing 4096-byte locator twice + two 5-byte candidates twice = 8212.
  OPENHDK_FAIL_IF(49, boundary.commitScan(rb, {{"a.mid"}, {"b.mid"}}, true, {2U, 5U, 8212U}) != CatalogError::None);
  const auto sumBefore = boundary.snapshot();
  // Rescan retains the Missing long locator too: 2*(4096+5+5)+2*(5+5)=8232.
  OPENHDK_FAIL_IF(50, boundary.commitScan(rb, {{"a.mid"}, {"b.mid"}}, true, {2U, 5U, 8231U}) != CatalogError::LimitExceeded || boundary.snapshot() != sumBefore);
  // Non-ASCII case and distinct normalization spellings remain distinct keys.
  OPENHDK_FAIL_IF(51, boundary.commitScan(rb, {{"É.mid"}, {"é.mid"}, {"e\xcc\x81.mid"}}, true) != CatalogError::None);
  std::shared_ptr<const CatalogSnapshot> outlivesOwner;
  {
    SongCatalog temporary;
    const auto rt = *temporary.addRoot();
    OPENHDK_FAIL_IF(52, temporary.commitScan(rt, {{"retained.mid"}}, true) != CatalogError::None);
    outlivesOwner = temporary.snapshot();
  }
  OPENHDK_FAIL_IF(53, outlivesOwner->songs.size() != 1U || outlivesOwner->songs[0].locator() != "retained.mid");
  return 0;
}
