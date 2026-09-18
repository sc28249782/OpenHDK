// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace OpenHDK {

class SmfByteReader {
public:
    explicit SmfByteReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::optional<std::uint8_t> readByte() {
        if (offset_ >= bytes_.size()) return std::nullopt;
        return bytes_[offset_++];
    }

    [[nodiscard]] std::optional<std::uint16_t> readBigEndian16() {
        const auto bytes = readBytes(2U);
        if (!bytes) return std::nullopt;
        return (static_cast<std::uint16_t>((*bytes)[0]) << 8U)
             | static_cast<std::uint16_t>((*bytes)[1]);
    }

    [[nodiscard]] std::optional<std::uint32_t> readBigEndian32() {
        const auto bytes = readBytes(4U);
        if (!bytes) return std::nullopt;
        return (static_cast<std::uint32_t>((*bytes)[0]) << 24U)
             | (static_cast<std::uint32_t>((*bytes)[1]) << 16U)
             | (static_cast<std::uint32_t>((*bytes)[2]) << 8U)
             | static_cast<std::uint32_t>((*bytes)[3]);
    }

    [[nodiscard]] std::optional<std::span<const std::uint8_t>> readBytes(std::size_t count) {
        if (count > remaining()) return std::nullopt;
        const auto result = bytes_.subspan(offset_, count);
        offset_ += count;
        return result;
    }

    [[nodiscard]] std::size_t offset() const { return offset_; }
    [[nodiscard]] std::size_t remaining() const { return bytes_.size() - offset_; }

    [[nodiscard]] std::optional<std::uint32_t> readVariableLength() {
        std::uint32_t value = 0;
        for (int count = 0; count < 4; ++count) {
            const auto byte = readByte();
            if (!byte) return std::nullopt;
            value = (value << 7U) | (*byte & 0x7fU);
            if ((*byte & 0x80U) == 0U) return value;
        }
        return std::nullopt;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{0};
};

} // namespace OpenHDK
