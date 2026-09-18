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
        if (offset_ == bytes_.size()) return std::nullopt;
        return bytes_[offset_++];
    }

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
