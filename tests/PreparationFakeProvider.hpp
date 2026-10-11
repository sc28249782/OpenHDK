// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/DurableLibraryService.hpp"
namespace PreparationFixture {
using namespace OpenHDK;
struct Image { std::optional<CheckpointBytes> primary; bool leased = false; };
struct Fake final : DurableLibraryTestProvider {
  explicit Fake(Image& target) : image(target) {}
  Image& image;
  struct Slot { CheckpointBytes bytes; bool active = false, synced = false; };
  std::array<Slot, 2> slots;
  std::optional<StoreOperation> fail;
  StorePublication outcome = StorePublication::Published;
  bool uncertainNew = false, corruptVerify = false, cleanupWarning = false;
  bool* lateCancel = nullptr;
  void (*observe)(void*, StoreOperation) noexcept = nullptr;
  void* context = nullptr;
  std::size_t reads = 0, publications = 0, syncs = 0;
  std::optional<StoreProviderError> fault(StoreOperation op) noexcept {
    if (fail == op) return StoreProviderError{StoreErrorCode::StorageFailure, 123};
    return {};
  }
  std::optional<StoreProviderError> acquire() override {
    if (auto e = fault(StoreOperation::Open)) return e;
    if (image.leased) return StoreProviderError{StoreErrorCode::Busy};
    image.leased = true; return {};
  }
  void release() noexcept override { image.leased = false; }
  std::size_t retainedBytes() const noexcept override { return 5U; }
  std::optional<StoreProviderError> checkDirectory() noexcept override { return {}; }
  StoreReadResult readPrimary(std::size_t limit) override {
    ++reads;
    if (auto e = fault(StoreOperation::Read)) return {{}, e};
    if (!image.primary) return {{}, StoreProviderError{StoreErrorCode::NotFound}};
    if (image.primary->size() > limit) return {{}, StoreProviderError{StoreErrorCode::LimitExceeded}};
    return {*image.primary, {}};
  }
  StoreArtifactResult createArtifact(StoreArtifactKind kind) override {
    const auto i = kind == StoreArtifactKind::Candidate ? 0U : 1U;
    if (auto e = fault(i == 0 ? StoreOperation::CreateCandidate : StoreOperation::CreatePrior)) return {{}, e};
    if (slots[i].active) return {{}, StoreProviderError{StoreErrorCode::Busy}};
    slots[i].active = true; return {StoreArtifact{i + 1U}, {}};
  }
  std::optional<StoreProviderError> writeArtifact(StoreArtifact artifact, std::span<const std::uint8_t> bytes) override {
    if (auto e = fault(artifact.identity == 1 ? StoreOperation::WriteCandidate : StoreOperation::WritePrior)) return e;
    slots[artifact.identity - 1U].bytes.assign(bytes.begin(), bytes.end()); return {};
  }
  std::optional<StoreProviderError> syncArtifact(StoreArtifact artifact) noexcept override {
    if (auto e = fault(artifact.identity == 1 ? StoreOperation::SyncCandidate : StoreOperation::SyncPrior)) return e;
    slots[artifact.identity - 1U].synced = true; return {};
  }
  StoreReadResult readArtifact(StoreArtifact artifact, std::size_t limit) override {
    if (auto e = fault(artifact.identity == 1 ? StoreOperation::VerifyCandidate : StoreOperation::VerifyPrior)) return {{}, e};
    const auto& slot = slots[artifact.identity - 1U];
    if (slot.bytes.size() > limit) return {{}, StoreProviderError{StoreErrorCode::LimitExceeded}};
    auto bytes = slot.bytes; if (corruptVerify) bytes.back() ^= 1U; return {std::move(bytes), {}};
  }
  StorePublicationResult publish(StoreArtifact artifact, bool absent) noexcept override {
    ++publications;
    if (observe) observe(context, StoreOperation::Publish);


    if (lateCancel) *lateCancel = true;
    if (absent == image.primary.has_value() || !slots[artifact.identity - 1U].synced)
      return {StorePublication::NotCommitted, StoreProviderError{StoreErrorCode::SourceChanged}};
    if (outcome == StorePublication::Published || uncertainNew) {
      if (!image.primary) image.primary.emplace();
      image.primary->swap(slots[artifact.identity - 1U].bytes);
    }
    return {outcome, outcome == StorePublication::NotCommitted
        ? std::optional<StoreProviderError>{{StoreErrorCode::StorageFailure, 123}} : std::nullopt};
  }
  std::optional<StoreProviderError> syncPublication() noexcept override {
    ++syncs; if (observe) observe(context, StoreOperation::SyncPublication);
    return fault(StoreOperation::SyncPublication);
  }
  std::optional<StoreProviderError> reconcilePublication(bool) noexcept override { return fault(StoreOperation::Reconcile); }
  std::optional<StoreProviderError> cleanup(StoreArtifact artifact) noexcept override {
    if (cleanupWarning) return StoreProviderError{StoreErrorCode::StorageFailure, 123};
    slots[artifact.identity - 1U] = {}; return {};
  }
};

}
