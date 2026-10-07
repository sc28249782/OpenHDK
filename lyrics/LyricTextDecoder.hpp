// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace OpenHDK {

enum class LyricTextEncoding { Utf8, Tis620 };
enum class LyricTextErrorCode { InvalidConfiguration, InvalidText, LimitExceeded };

struct LyricTextError {
  LyricTextErrorCode code;
  std::size_t byteOffset;
};

struct LyricTextLimits {
  static constexpr std::size_t kMaxSourceBytes = 4U * 1024U * 1024U;
  static constexpr std::size_t kMaxDecodedBytes = 64U * 1024U * 1024U;
  std::size_t sourceBytes = kMaxSourceBytes;
  std::size_t decodedBytes = kMaxDecodedBytes;
};

class LyricTextDecodeResult {
public:
  explicit LyricTextDecodeResult(std::string text) : text_(std::move(text)) {}
  explicit LyricTextDecodeResult(LyricTextError error) : error_(error) {}
  bool succeeded() const noexcept { return text_.has_value(); }
  const std::optional<std::string>& text() const noexcept { return text_; }
  const std::optional<LyricTextError>& error() const noexcept { return error_; }
private:
  std::optional<std::string> text_;
  std::optional<LyricTextError> error_;
};

// Pure control-path decoder; it allocates and MUST NOT run in an audio callback.
// Source ownership/provenance and aggregate staging budgets belong to the caller.
// Errors never return a partially decoded string. Allocation exceptions propagate.
inline LyricTextDecodeResult decodeLyricText(
    std::span<const std::uint8_t> bytes,
    LyricTextEncoding encoding = LyricTextEncoding::Utf8,
    LyricTextLimits limits = {}) {
  const auto fail = [](LyricTextErrorCode code, std::size_t offset) {
    return LyricTextDecodeResult(LyricTextError{code, offset});
  };
  if ((encoding != LyricTextEncoding::Utf8 && encoding != LyricTextEncoding::Tis620)
      || limits.sourceBytes == 0U || limits.sourceBytes > LyricTextLimits::kMaxSourceBytes
      || limits.decodedBytes == 0U || limits.decodedBytes > LyricTextLimits::kMaxDecodedBytes) {
    return fail(LyricTextErrorCode::InvalidConfiguration, 0U);
  }
  if (bytes.size() > limits.sourceBytes) {
    return fail(LyricTextErrorCode::LimitExceeded, limits.sourceBytes);
  }
  std::string text;
  for (std::size_t offset = 0U; offset < bytes.size();) {
    const auto first = bytes[offset];
    std::uint32_t scalar = first;
    std::size_t length = 1U;
    if (encoding == LyricTextEncoding::Tis620) {
      // Independently expressed ranges; mapping reference and C1 policy are in
      // docs/LYRIC-TIMELINE-CONTRACT.md (CPython v3.13.0 mapping, not CP874).
      if (first <= 0x9fU) scalar = first;
      else if (first >= 0xa1U && first <= 0xdaU) scalar = 0x0e01U + first - 0xa1U;
      else if (first == 0xdfU) scalar = 0x0e3fU;
      else if (first >= 0xe0U && first <= 0xfbU) scalar = 0x0e40U + first - 0xe0U;
      else return fail(LyricTextErrorCode::InvalidText, offset);
    } else if (first >= 0x80U) {
      if (first >= 0xc2U && first <= 0xdfU) { length = 2U; scalar = first & 0x1fU; }
      else if (first >= 0xe0U && first <= 0xefU) { length = 3U; scalar = first & 0x0fU; }
      else if (first >= 0xf0U && first <= 0xf4U) { length = 4U; scalar = first & 0x07U; }
      else return fail(LyricTextErrorCode::InvalidText, offset);
      for (std::size_t i = 1U; i < length; ++i) {
        // Missing continuation points one past the payload, not at its lead.
        if (i >= bytes.size() - offset) return fail(LyricTextErrorCode::InvalidText, bytes.size());
        const auto continuation = bytes[offset + i];
        if (continuation < 0x80U || continuation > 0xbfU
            || (i == 1U && ((first == 0xe0U && continuation < 0xa0U)
                         || (first == 0xedU && continuation > 0x9fU)
                         || (first == 0xf0U && continuation < 0x90U)
                         || (first == 0xf4U && continuation > 0x8fU)))) {
          return fail(LyricTextErrorCode::InvalidText, offset + i);
        }
        scalar = (scalar << 6U) | (continuation & 0x3fU);
      }
    }
    if (scalar < 0x20U && scalar != 0x09U && scalar != 0x0aU && scalar != 0x0dU) {
      return fail(LyricTextErrorCode::InvalidText, offset);
    }
    const std::size_t outputLength = scalar < 0x80U ? 1U : scalar < 0x800U ? 2U
                                    : scalar < 0x10000U ? 3U : 4U;
    if (outputLength > limits.decodedBytes - text.size()) {
      return fail(LyricTextErrorCode::LimitExceeded, offset);
    }
    if (encoding == LyricTextEncoding::Utf8) {
      // Preserve the validated UTF-8 bytes exactly, including BOM and combining marks.
      for (std::size_t i = 0U; i < length; ++i) text.push_back(static_cast<char>(bytes[offset + i]));
    } else if (outputLength == 1U) {
      text.push_back(static_cast<char>(scalar));
    } else if (outputLength == 2U) {
      text.push_back(static_cast<char>(0xc0U | (scalar >> 6U)));
      text.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
    } else {
      text.push_back(static_cast<char>(0xe0U | (scalar >> 12U)));
      text.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3fU)));
      text.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
    }
    offset += length;
  }
  return LyricTextDecodeResult(std::move(text));
}

} // namespace OpenHDK
