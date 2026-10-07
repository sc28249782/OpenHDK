// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "lyrics/LyricTextDecoder.hpp"
#include "tests/TestCheck.hpp"
#include <array>
#include <string_view>
#include <vector>

namespace {
using namespace OpenHDK;
LyricTextDecodeResult decode(std::initializer_list<std::uint8_t> bytes,
                            LyricTextEncoding encoding = LyricTextEncoding::Utf8,
                            LyricTextLimits limits = {}) {
  return decodeLyricText(std::span<const std::uint8_t>(bytes.begin(), bytes.size()), encoding, limits);
}
bool fails(const LyricTextDecodeResult& result, LyricTextErrorCode code, std::size_t offset) {
  return !result.succeeded() && !result.text() && result.error()
      && result.error()->code == code && result.error()->byteOffset == offset;
}
// Fixed byte strings independently generated with Python's strict tis620 codec;
// C0 policy is applied separately. No decoder formula is used as a test oracle.
constexpr std::array<const char*, 256> tisExpected = {
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, "\x09", "\x0a", nullptr, nullptr, "\x0d", nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  "\x20", "\x21", "\x22", "\x23", "\x24", "\x25", "\x26", "\x27",
  "\x28", "\x29", "\x2a", "\x2b", "\x2c", "\x2d", "\x2e", "\x2f",
  "\x30", "\x31", "\x32", "\x33", "\x34", "\x35", "\x36", "\x37",
  "\x38", "\x39", "\x3a", "\x3b", "\x3c", "\x3d", "\x3e", "\x3f",
  "\x40", "\x41", "\x42", "\x43", "\x44", "\x45", "\x46", "\x47",
  "\x48", "\x49", "\x4a", "\x4b", "\x4c", "\x4d", "\x4e", "\x4f",
  "\x50", "\x51", "\x52", "\x53", "\x54", "\x55", "\x56", "\x57",
  "\x58", "\x59", "\x5a", "\x5b", "\x5c", "\x5d", "\x5e", "\x5f",
  "\x60", "\x61", "\x62", "\x63", "\x64", "\x65", "\x66", "\x67",
  "\x68", "\x69", "\x6a", "\x6b", "\x6c", "\x6d", "\x6e", "\x6f",
  "\x70", "\x71", "\x72", "\x73", "\x74", "\x75", "\x76", "\x77",
  "\x78", "\x79", "\x7a", "\x7b", "\x7c", "\x7d", "\x7e", "\x7f",
  "\xc2\x80", "\xc2\x81", "\xc2\x82", "\xc2\x83", "\xc2\x84", "\xc2\x85", "\xc2\x86", "\xc2\x87",
  "\xc2\x88", "\xc2\x89", "\xc2\x8a", "\xc2\x8b", "\xc2\x8c", "\xc2\x8d", "\xc2\x8e", "\xc2\x8f",
  "\xc2\x90", "\xc2\x91", "\xc2\x92", "\xc2\x93", "\xc2\x94", "\xc2\x95", "\xc2\x96", "\xc2\x97",
  "\xc2\x98", "\xc2\x99", "\xc2\x9a", "\xc2\x9b", "\xc2\x9c", "\xc2\x9d", "\xc2\x9e", "\xc2\x9f",
  nullptr, "\xe0\xb8\x81", "\xe0\xb8\x82", "\xe0\xb8\x83", "\xe0\xb8\x84", "\xe0\xb8\x85", "\xe0\xb8\x86", "\xe0\xb8\x87",
  "\xe0\xb8\x88", "\xe0\xb8\x89", "\xe0\xb8\x8a", "\xe0\xb8\x8b", "\xe0\xb8\x8c", "\xe0\xb8\x8d", "\xe0\xb8\x8e", "\xe0\xb8\x8f",
  "\xe0\xb8\x90", "\xe0\xb8\x91", "\xe0\xb8\x92", "\xe0\xb8\x93", "\xe0\xb8\x94", "\xe0\xb8\x95", "\xe0\xb8\x96", "\xe0\xb8\x97",
  "\xe0\xb8\x98", "\xe0\xb8\x99", "\xe0\xb8\x9a", "\xe0\xb8\x9b", "\xe0\xb8\x9c", "\xe0\xb8\x9d", "\xe0\xb8\x9e", "\xe0\xb8\x9f",
  "\xe0\xb8\xa0", "\xe0\xb8\xa1", "\xe0\xb8\xa2", "\xe0\xb8\xa3", "\xe0\xb8\xa4", "\xe0\xb8\xa5", "\xe0\xb8\xa6", "\xe0\xb8\xa7",
  "\xe0\xb8\xa8", "\xe0\xb8\xa9", "\xe0\xb8\xaa", "\xe0\xb8\xab", "\xe0\xb8\xac", "\xe0\xb8\xad", "\xe0\xb8\xae", "\xe0\xb8\xaf",
  "\xe0\xb8\xb0", "\xe0\xb8\xb1", "\xe0\xb8\xb2", "\xe0\xb8\xb3", "\xe0\xb8\xb4", "\xe0\xb8\xb5", "\xe0\xb8\xb6", "\xe0\xb8\xb7",
  "\xe0\xb8\xb8", "\xe0\xb8\xb9", "\xe0\xb8\xba", nullptr, nullptr, nullptr, nullptr, "\xe0\xb8\xbf",
  "\xe0\xb9\x80", "\xe0\xb9\x81", "\xe0\xb9\x82", "\xe0\xb9\x83", "\xe0\xb9\x84", "\xe0\xb9\x85", "\xe0\xb9\x86", "\xe0\xb9\x87",
  "\xe0\xb9\x88", "\xe0\xb9\x89", "\xe0\xb9\x8a", "\xe0\xb9\x8b", "\xe0\xb9\x8c", "\xe0\xb9\x8d", "\xe0\xb9\x8e", "\xe0\xb9\x8f",
  "\xe0\xb9\x90", "\xe0\xb9\x91", "\xe0\xb9\x92", "\xe0\xb9\x93", "\xe0\xb9\x94", "\xe0\xb9\x95", "\xe0\xb9\x96", "\xe0\xb9\x97",
  "\xe0\xb9\x98", "\xe0\xb9\x99", "\xe0\xb9\x9a", "\xe0\xb9\x9b", nullptr, nullptr, nullptr, nullptr,
};
} // namespace

int main() {
  using namespace OpenHDK;
  for (std::size_t n = 0U; n < tisExpected.size(); ++n) {
    const std::array<std::uint8_t, 1> source = {static_cast<std::uint8_t>(n)};
    const auto result = decodeLyricText(source, LyricTextEncoding::Tis620);
    if (tisExpected[n] == nullptr) {
      OPENHDK_FAIL_IF(1, !fails(result, LyricTextErrorCode::InvalidText, 0U));
    } else {
      OPENHDK_FAIL_IF(2, !result.succeeded() || result.error() || *result.text() != tisExpected[n]);
    }
    OPENHDK_FAIL_IF(3, source[0] != n);
  }
  const std::vector<std::uint8_t> utf8 = {
      0x20,0x09,0x0d,0x0a,0x2f,0x5c,0x40,0x7f, // literal controls/markers
      0xc2,0x80,0xdf,0xbf, // valid two-byte boundaries
      0xe0,0xa0,0x80,0xed,0x9f,0xbf,0xee,0x80,0x80,0xef,0xbf,0xbf,
      0xf0,0x90,0x80,0x80,0xf4,0x8f,0xbf,0xbf, // valid scalar extremes
      0xef,0xbb,0xbf, // BOM is retained
      0x65,0xcc,0x81, // no Unicode normalization
      0xe0,0xb8,0x81,0xe0,0xb9,0x88 // Thai consonant + combining tone
  };
  const auto copy = utf8;
  const auto valid = decodeLyricText(utf8);
  const std::string exact(utf8.begin(), utf8.end());
  OPENHDK_FAIL_IF(4, !valid.succeeded() || *valid.text() != exact || utf8 != copy);
  OPENHDK_FAIL_IF(5, !decode({}).succeeded() || !decode({}).text()->empty());
  OPENHDK_FAIL_IF(6, !decode({}, LyricTextEncoding::Tis620).succeeded());
  // First offending byte, or payload end for missing continuation.
  const std::vector<std::pair<std::vector<std::uint8_t>, std::size_t>> malformed = {
      {{0x80},0}, {{0xc0,0x80},0}, {{0xc1,0xbf},0}, {{0xf5,0x80,0x80,0x80},0},
      {{0xff},0}, {{0xc2},1}, {{0xe0,0xa0},2}, {{0xf0,0x90,0x80},3},
      {{0xc2,0x41},1}, {{0xe0,0x9f,0xbf},1}, {{0xed,0xa0,0x80},1},
      {{0xf0,0x8f,0xbf,0xbf},1}, {{0xf4,0x90,0x80,0x80},1},
      {{0x41,0xe1,0x80,0x41},3}, {{0x41,0x00},1}, {{0x41,0xc2},2}
  };
  for (const auto& [bytes, offset] : malformed) {
    OPENHDK_FAIL_IF(7, !fails(decodeLyricText(bytes), LyricTextErrorCode::InvalidText, offset));
  }
  for (std::uint8_t n = 0U; n < 32U; ++n) {
    const auto result = decode({n});
    OPENHDK_FAIL_IF(8, (n == 9U || n == 10U || n == 13U) ? !result.succeeded()
                    : !fails(result, LyricTextErrorCode::InvalidText, 0U));
  }
  OPENHDK_FAIL_IF(9, !fails(decode({0x41,0xa0}, LyricTextEncoding::Tis620),
                           LyricTextErrorCode::InvalidText, 1U));
  OPENHDK_FAIL_IF(10, !fails(decode({0xff}), LyricTextErrorCode::InvalidText, 0U));
  // No retry: Thai TIS bytes fail UTF-8, while the explicit TIS selection succeeds.
  OPENHDK_FAIL_IF(11, decode({0xa1,0xe8}).succeeded()
                   || !decode({0xa1,0xe8}, LyricTextEncoding::Tis620).succeeded());
  OPENHDK_FAIL_IF(12, !fails(decode({}, static_cast<LyricTextEncoding>(99)),
                            LyricTextErrorCode::InvalidConfiguration, 0U));
  for (const auto limits : std::array<LyricTextLimits,4>{{
      {0,1},{1,0},{LyricTextLimits::kMaxSourceBytes+1,1},
      {1,LyricTextLimits::kMaxDecodedBytes+1}}}) {
    OPENHDK_FAIL_IF(13, !fails(decode({}, LyricTextEncoding::Utf8, limits),
                              LyricTextErrorCode::InvalidConfiguration, 0U));
  }
  OPENHDK_FAIL_IF(14, !decode({0x41,0x42}, LyricTextEncoding::Utf8, {2,2}).succeeded());
  OPENHDK_FAIL_IF(15, !fails(decode({0x41,0x42}, LyricTextEncoding::Utf8, {1,2}),
                            LyricTextErrorCode::LimitExceeded, 1U));
  OPENHDK_FAIL_IF(16, !fails(decode({0x41,0x42}, LyricTextEncoding::Utf8, {2,1}),
                            LyricTextErrorCode::LimitExceeded, 1U));
  OPENHDK_FAIL_IF(17, !decode({0xa1}, LyricTextEncoding::Tis620, {1,3}).succeeded());
  OPENHDK_FAIL_IF(18, !fails(decode({0xa1}, LyricTextEncoding::Tis620, {1,2}),
                            LyricTextErrorCode::LimitExceeded, 0U));
  OPENHDK_FAIL_IF(19, !fails(decode({0x41,0xa1}, LyricTextEncoding::Tis620, {2,3}),
                            LyricTextErrorCode::LimitExceeded, 1U));
  OPENHDK_FAIL_IF(20, !decode({0xf4,0x8f,0xbf,0xbf}, LyricTextEncoding::Utf8, {4,4}).succeeded());
  OPENHDK_FAIL_IF(21, !fails(decode({0xf4,0x8f,0xbf,0xbf}, LyricTextEncoding::Utf8, {4,3}),
                            LyricTextErrorCode::LimitExceeded, 0U));
  std::vector<std::uint8_t> boundary(LyricTextLimits::kMaxSourceBytes, 0x41U);
  OPENHDK_FAIL_IF(22, !decodeLyricText(boundary).succeeded());
  boundary.push_back(0x41U);
  OPENHDK_FAIL_IF(23, !fails(decodeLyricText(boundary), LyricTextErrorCode::LimitExceeded,
                            LyricTextLimits::kMaxSourceBytes));
  return 0;
}
