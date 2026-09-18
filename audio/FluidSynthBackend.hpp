// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once
#include "audio/AudioBackend.hpp"
#include <memory>
namespace OpenHDK {
class FluidSynthBackend final : public AudioBackend {
public:
  FluidSynthBackend();
  ~FluidSynthBackend() override;
  FluidSynthBackend(const FluidSynthBackend&) = delete;
  FluidSynthBackend& operator=(const FluidSynthBackend&) = delete;
  [[nodiscard]] AudioBackendInfo info() const noexcept override;
  bool initialize(const AudioBackendConfig&, AudioBackendStatus&) override;
  bool playMidiFile(const std::filesystem::path&, AudioBackendStatus&) override;
  bool renderStereo(std::span<float>, AudioBackendStatus&) override;
  [[nodiscard]] bool isPlaying() const noexcept override;
  [[nodiscard]] bool hasActiveDevice() const noexcept override;
  void shutdown() noexcept override;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace OpenHDK
