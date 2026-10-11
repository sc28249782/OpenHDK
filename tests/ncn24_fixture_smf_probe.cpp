// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
// Cross-check exported original synthetic bytes against existing SMF/text APIs.
// This probe does not parse CUR, build NCN cues or claim NCN compatibility.
#include "audio/SmfParser.hpp"
#include "audio/SmfTimelineCompiler.hpp"
#include "lyrics/LyricTextDecoder.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
std::vector<std::uint8_t> read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("fixture read");
    std::array<char, 4097> buffer{};
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = stream.gcount();
    if (stream.bad() || count > 4096) throw std::runtime_error("fixture read/limit");
    std::vector<std::uint8_t> result;
    result.reserve(static_cast<std::size_t>(count));
    for (std::streamsize i = 0; i < count; ++i)
        result.push_back(static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(i)]));
    return result;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 64;
    try {
        const std::filesystem::path root(argv[1]);
        struct Midi { const char* name; std::uint16_t ppqn; std::uint64_t tick; std::uint64_t us; };
        constexpr std::array<Midi, 4> midis{{
            {"quarter", 480, 1440, 1500000}, {"floor-collision", 3, 3, 500000},
            {"high-ff-max", 480, 1310700, 1365312500}, {"tempo-change", 480, 1440, 2500000}}};
        for (const auto& expected : midis) {
            const auto bytes = read(root / expected.name / "Song/example.mid");
            const auto parsed = OpenHDK::SmfParser::parse(bytes);
            if (!parsed.succeeded() || parsed.file()->division() != expected.ppqn)
                throw std::runtime_error("fixture parse");
            const auto compiled = OpenHDK::SmfTimelineCompiler::compile(*parsed.file());
            if (!compiled.succeeded() || compiled.timeline()->events().empty()
                || compiled.timeline()->events().back().tick() != expected.tick
                || compiled.timeline()->events().back().timeMicroseconds() != expected.us)
                throw std::runtime_error("fixture timeline");
        }
        struct Text { const char* name; std::string_view utf8; };
        constexpr std::array<Text, 3> texts{{
            {"thai-combining", "\xe0\xb8\x81\xe0\xb8\xb4\xe0\xb9\x88"},
            {"thai-am", "\xe0\xb8\x81\xe0\xb8\xb3"},
            {"thai-am-decomposed", "\xe0\xb8\x81\xe0\xb9\x8d\xe0\xb8\xb2"}}};
        for (const auto& expected : texts) {
            const auto bytes = read(root / expected.name / "Lyrics/example.lyr");
            if (bytes.size() < 7) throw std::runtime_error("fixture header");
            const auto decoded = OpenHDK::decodeLyricText(
                std::span<const std::uint8_t>(bytes).subspan(7), OpenHDK::LyricTextEncoding::Tis620);
            if (!decoded.succeeded() || *decoded.text() != expected.utf8)
                throw std::runtime_error("fixture text");
        }
        std::cout << "{\"smf_vectors\":4,\"tis620_vectors\":3,\"status\":\"verified\","
                     "\"scope\":\"synthetic-only\",\"ncn_acceptance\":\"Pending\"}\n";
        return std::cout ? 0 : 74;
    } catch (const std::exception&) {
        std::cerr << "Synthetic fixture probe failed; NCN acceptance Pending\n";
        return 1;
    }
}
