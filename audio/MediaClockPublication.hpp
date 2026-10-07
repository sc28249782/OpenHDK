// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include <atomic>
#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace OpenHDK {

enum class MediaClockSource : std::uint32_t {
    None,
    CompiledTimeline,
    LegacyPlayer,
};

enum class MediaClockPhase : std::uint32_t {
    Unavailable,
    Preparing,
    Playing,
    Paused,
    Finished,
    Stopped,
    Failed,
};

enum class MediaClockFailure : std::uint32_t {
    None,
    PrepareFailed,
    DeviceStartFailed,
    DispatchFailed,
    RenderFailed,
    CompletionFailed,
};

struct MediaClockPayload {
    std::uint64_t generation{};
    std::uint64_t mediaMicroseconds{};
    MediaClockSource source{MediaClockSource::None};
    MediaClockPhase phase{MediaClockPhase::Unavailable};
    MediaClockFailure failure{MediaClockFailure::None};

    auto operator<=>(const MediaClockPayload&) const = default;
};

struct MediaClockSnapshot : MediaClockPayload {
    std::uint64_t revision{};
};

enum class MediaClockPublishStatus {
    Published,
    Exhausted,
};

enum class MediaClockReadStatus {
    Snapshot,
    Unstable,
    Exhausted,
};

struct MediaClockReadResult {
    MediaClockReadStatus status{MediaClockReadStatus::Unstable};
    std::optional<MediaClockSnapshot> snapshot;
};

struct MediaClockPublicationTestAccess;

// One writer publishes fixed values. Readers make one bounded attempt. Writer
// ownership and callback quiescence are enforced by the backend adapter.
class MediaClockPublicationCell {
public:
    MediaClockPublicationCell() noexcept = default;

    MediaClockPublicationCell(const MediaClockPublicationCell&) = delete;
    MediaClockPublicationCell& operator=(const MediaClockPublicationCell&) = delete;

    [[nodiscard]] MediaClockPublishStatus publish(const MediaClockPayload& payload) noexcept {
        return publishWithHook(payload, []() noexcept {});
    }

    [[nodiscard]] MediaClockReadResult tryRead() const noexcept {
        if (exhausted_.load(std::memory_order_acquire) != 0U) {
            return {MediaClockReadStatus::Exhausted, std::nullopt};
        }

        const auto before = sequence_.load(std::memory_order_acquire);
        if ((before & 1U) != 0U) return {MediaClockReadStatus::Unstable, std::nullopt};

        MediaClockSnapshot snapshot{};
        snapshot.revision = before;
        snapshot.generation = generation_.load(std::memory_order_acquire);
        snapshot.mediaMicroseconds = mediaMicroseconds_.load(std::memory_order_acquire);
        snapshot.source = static_cast<MediaClockSource>(source_.load(std::memory_order_acquire));
        snapshot.phase = static_cast<MediaClockPhase>(phase_.load(std::memory_order_acquire));
        snapshot.failure = static_cast<MediaClockFailure>(failure_.load(std::memory_order_acquire));

        const auto after = sequence_.load(std::memory_order_acquire);
        if (exhausted_.load(std::memory_order_acquire) != 0U) {
            return {MediaClockReadStatus::Exhausted, std::nullopt};
        }
        if (before != after || (after & 1U) != 0U) {
            return {MediaClockReadStatus::Unstable, std::nullopt};
        }
        return {MediaClockReadStatus::Snapshot, snapshot};
    }

private:
    explicit MediaClockPublicationCell(std::uint64_t sequence) noexcept : sequence_(sequence) {}

    template <typename AfterOdd>
    [[nodiscard]] MediaClockPublishStatus publishWithHook(
        const MediaClockPayload& payload, AfterOdd&& afterOdd) noexcept {
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        const auto current = sequence_.load(std::memory_order_relaxed);
        if (exhausted_.load(std::memory_order_relaxed) != 0U || current > maximum - 2U) {
            exhausted_.store(1U, std::memory_order_release);
            return MediaClockPublishStatus::Exhausted;
        }

        sequence_.store(current + 1U, std::memory_order_release);
        std::forward<AfterOdd>(afterOdd)();
        generation_.store(payload.generation, std::memory_order_release);
        mediaMicroseconds_.store(payload.mediaMicroseconds, std::memory_order_release);
        source_.store(static_cast<std::uint32_t>(payload.source), std::memory_order_release);
        phase_.store(static_cast<std::uint32_t>(payload.phase), std::memory_order_release);
        failure_.store(static_cast<std::uint32_t>(payload.failure), std::memory_order_release);
        sequence_.store(current + 2U, std::memory_order_release);
        return MediaClockPublishStatus::Published;
    }

    friend struct MediaClockPublicationTestAccess;

    std::atomic<std::uint64_t> sequence_{};
    std::atomic<std::uint64_t> generation_{};
    std::atomic<std::uint64_t> mediaMicroseconds_{};
    std::atomic<std::uint32_t> source_{};
    std::atomic<std::uint32_t> phase_{};
    std::atomic<std::uint32_t> failure_{};
    std::atomic<std::uint32_t> exhausted_{};
};

class PlaybackGenerationCounter {
public:
    PlaybackGenerationCounter() noexcept = default;

    [[nodiscard]] std::optional<std::uint64_t> admit() noexcept {
        if (current_ == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
        return ++current_;
    }

    [[nodiscard]] std::uint64_t current() const noexcept { return current_; }

private:
    explicit PlaybackGenerationCounter(std::uint64_t current) noexcept : current_(current) {}

    friend struct MediaClockPublicationTestAccess;

    std::uint64_t current_{};
};

#if defined(OPENHDK_ENABLE_TEST_SEAMS)
struct MediaClockPublicationTestAccess {
    [[nodiscard]] static MediaClockPublicationCell cellAtSequence(std::uint64_t sequence) noexcept {
        return MediaClockPublicationCell(sequence);
    }

    [[nodiscard]] static PlaybackGenerationCounter generationAt(std::uint64_t current) noexcept {
        return PlaybackGenerationCounter(current);
    }

    template <typename AfterOdd>
    [[nodiscard]] static MediaClockPublishStatus publishWithHook(
        MediaClockPublicationCell& cell,
        const MediaClockPayload& payload,
        AfterOdd&& afterOdd) noexcept {
        return cell.publishWithHook(payload, std::forward<AfterOdd>(afterOdd));
    }
};
#endif

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

} // namespace OpenHDK
