// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "audio/FluidSynthBackend.hpp"
#include <algorithm>
#include <filesystem>
#include <fluidsynth.h>
#include <miniaudio.h>
namespace OpenHDK {
namespace {
constexpr std::size_t kChannels = 2;
void ok(AudioBackendStatus& s) { s = {}; }
void fail(AudioBackendStatus& s, AudioBackendError e, std::string text) { s.error = e; s.message = std::move(text); }
}
struct FluidSynthBackend::Impl {
  fluid_settings_t* settings{}; fluid_synth_t* synth{}; fluid_player_t* player{}; ma_context context{}; ma_device device{}; float volume{1.0F}; bool muted{}; bool contextInitialized{}; bool deviceInitialized{}; bool initialized{};
  static void callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    auto* self = static_cast<Impl*>(device->pUserData); auto* samples = static_cast<float*>(output);
    if (self == nullptr || self->synth == nullptr || fluid_synth_write_float(self->synth, static_cast<int>(frames), samples, 0, 2, samples, 1, 2) != FLUID_OK)
      std::fill_n(samples, static_cast<std::size_t>(frames) * kChannels, 0.0F);
  }
};
FluidSynthBackend::FluidSynthBackend() : impl_(std::make_unique<Impl>()) {}
FluidSynthBackend::~FluidSynthBackend() { shutdown(); }
AudioBackendInfo FluidSynthBackend::info() const noexcept { return {"fluidsynth-miniaudio", "FluidSynth + miniaudio", AudioCapability::MidiSynthesis | AudioCapability::DeviceOutput}; }
bool FluidSynthBackend::initialize(const AudioBackendConfig& config, AudioBackendStatus& status) {
  shutdown();
  if (config.sampleRate == 0U) { fail(status, AudioBackendError::InvalidConfiguration, "Sample rate must be greater than zero."); return false; }
  if (config.volume < 0.0F || config.volume > 1.0F) { fail(status, AudioBackendError::InvalidConfiguration, "Volume must be between 0.0 and 1.0."); return false; }
  if (!std::filesystem::is_regular_file(config.soundFontPath)) { fail(status, AudioBackendError::SoundFontNotFound, "SoundFont was not found: " + config.soundFontPath.string()); return false; }
  impl_->settings = new_fluid_settings();
  if (impl_->settings == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "FluidSynth could not allocate settings."); return false; }
  fluid_settings_setnum(impl_->settings, "synth.sample-rate", static_cast<double>(config.sampleRate));
  impl_->synth = new_fluid_synth(impl_->settings);
  if (impl_->synth == nullptr) { shutdown(); fail(status, AudioBackendError::InvalidConfiguration, "FluidSynth could not create a synthesizer."); return false; }
  impl_->volume = config.volume; impl_->muted = config.muted;
  fluid_synth_set_gain(impl_->synth, config.muted ? 0.0 : static_cast<double>(config.volume));
  if (fluid_synth_sfload(impl_->synth, config.soundFontPath.string().c_str(), 1) == FLUID_FAILED) { shutdown(); fail(status, AudioBackendError::SoundFontLoadFailed, "FluidSynth could not load SoundFont: " + config.soundFontPath.string()); return false; }
  if (config.enableDeviceOutput) {
    const auto contextResult = ma_context_init(nullptr, 0, nullptr, &impl_->context);
    if (contextResult != MA_SUCCESS) { shutdown(); fail(status, AudioBackendError::AudioDeviceUnavailable, "Audio output context could not start: " + std::string(ma_result_description(contextResult))); return false; }
    impl_->contextInitialized = true;
    ma_device_id selectedId{}; ma_device_id* selectedIdPtr = nullptr;
    if (config.outputDeviceIndex.has_value()) {
      ma_device_info* devices{}; ma_uint32 count{};
      const auto enumeration = ma_context_get_devices(&impl_->context, &devices, &count, nullptr, nullptr);
      if (enumeration != MA_SUCCESS) { shutdown(); fail(status, AudioBackendError::AudioDeviceUnavailable, "Audio devices could not be enumerated: " + std::string(ma_result_description(enumeration))); return false; }
      if (*config.outputDeviceIndex >= count) { shutdown(); fail(status, AudioBackendError::AudioDeviceNotFound, "Requested playback device index is not available: " + std::to_string(*config.outputDeviceIndex)); return false; }
      selectedId = devices[*config.outputDeviceIndex].id; selectedIdPtr = &selectedId;
    }
    auto dc = ma_device_config_init(ma_device_type_playback); dc.playback.format = ma_format_f32; dc.playback.channels = 2; dc.playback.pDeviceID = selectedIdPtr; dc.sampleRate = config.sampleRate; dc.dataCallback = &Impl::callback; dc.pUserData = impl_.get();
    const auto result = ma_device_init(&impl_->context, &dc, &impl_->device);
    if (result != MA_SUCCESS) { shutdown(); fail(status, AudioBackendError::AudioDeviceUnavailable, "No usable audio output device is available: " + std::string(ma_result_description(result))); return false; }
    impl_->deviceInitialized = true;
    const auto started = ma_device_start(&impl_->device);
    if (started != MA_SUCCESS) { shutdown(); fail(status, AudioBackendError::AudioDeviceUnavailable, "Audio output device could not start: " + std::string(ma_result_description(started))); return false; }
  }
  impl_->initialized = true; ok(status); return true;
}
bool FluidSynthBackend::playMidiFile(const std::filesystem::path& midi, AudioBackendStatus& status) {
  if (!impl_->initialized || impl_->synth == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "Initialize the audio backend before MIDI playback."); return false; }
  if (!std::filesystem::is_regular_file(midi)) { fail(status, AudioBackendError::MidiFileNotFound, "MIDI file was not found: " + midi.string()); return false; }
  if (impl_->player != nullptr) { delete_fluid_player(impl_->player); impl_->player = nullptr; }
  impl_->player = new_fluid_player(impl_->synth);
  if (impl_->player == nullptr || fluid_player_add(impl_->player, midi.string().c_str()) != FLUID_OK || fluid_player_play(impl_->player) != FLUID_OK) {
    if (impl_->player != nullptr) { delete_fluid_player(impl_->player); impl_->player = nullptr; }
    fail(status, AudioBackendError::MidiPlaybackFailed, "FluidSynth could not start MIDI playback: " + midi.string()); return false;
  }
  ok(status); return true;
}
bool FluidSynthBackend::renderStereo(std::span<float> pcm, AudioBackendStatus& status) {
  if (!impl_->initialized || impl_->synth == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "Initialize the audio backend before rendering PCM."); return false; }
  if (pcm.empty() || pcm.size() % kChannels != 0U) { fail(status, AudioBackendError::InvalidConfiguration, "PCM output must contain complete stereo frames."); return false; }
  if (fluid_synth_write_float(impl_->synth, static_cast<int>(pcm.size() / kChannels), pcm.data(), 0, 2, pcm.data(), 1, 2) != FLUID_OK) { std::fill(pcm.begin(), pcm.end(), 0.0F); fail(status, AudioBackendError::RenderFailed, "FluidSynth could not render PCM."); return false; }
  ok(status); return true;
}
bool FluidSynthBackend::setVolume(float value, AudioBackendStatus& status) {
  if (!impl_->initialized || impl_->synth == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "Initialize the audio backend before changing volume."); return false; }
  if (value < 0.0F || value > 1.0F) { fail(status, AudioBackendError::InvalidConfiguration, "Volume must be between 0.0 and 1.0."); return false; }
  impl_->volume = value; fluid_synth_set_gain(impl_->synth, impl_->muted ? 0.0 : static_cast<double>(value)); ok(status); return true;
}
bool FluidSynthBackend::setMuted(bool value, AudioBackendStatus& status) {
  if (!impl_->initialized || impl_->synth == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "Initialize the audio backend before changing mute state."); return false; }
  impl_->muted = value; fluid_synth_set_gain(impl_->synth, value ? 0.0 : static_cast<double>(impl_->volume)); ok(status); return true;
}
float FluidSynthBackend::volume() const noexcept { return impl_->volume; }
bool FluidSynthBackend::isMuted() const noexcept { return impl_->muted; }
bool FluidSynthBackend::isPlaying() const noexcept { return impl_->player != nullptr && fluid_player_get_status(impl_->player) == FLUID_PLAYER_PLAYING; }
bool FluidSynthBackend::hasActiveDevice() const noexcept { return impl_->deviceInitialized; }
void FluidSynthBackend::shutdown() noexcept {
  if (impl_ == nullptr) return;
  if (impl_->player != nullptr) { delete_fluid_player(impl_->player); impl_->player = nullptr; }
  if (impl_->deviceInitialized) { ma_device_uninit(&impl_->device); impl_->deviceInitialized = false; }
  if (impl_->contextInitialized) { ma_context_uninit(&impl_->context); impl_->contextInitialized = false; }
  if (impl_->synth != nullptr) { delete_fluid_synth(impl_->synth); impl_->synth = nullptr; }
  if (impl_->settings != nullptr) { delete_fluid_settings(impl_->settings); impl_->settings = nullptr; }
  impl_->initialized = false;
}
} // namespace OpenHDK
