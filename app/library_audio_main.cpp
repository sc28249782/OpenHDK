// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
// Audio-enabled library session entry (OHK-CLI-030 step 5, slice 2). Owns the
// durable service, the guarded management session, the concrete FluidSynth
// backend and the playback controller in a lifetime order where the backend
// outlives the controller and both die before the service. A failed backend
// initialization is a clear process failure; the library-only OpenHDKLibrary
// entry remains usable without audio.
#include "app/LibraryAudioSession.hpp"
#include "audio/FluidSynthBackend.hpp"
#include <filesystem>
#include <iostream>
#include <new>
#include <string>
#include <string_view>
#include <vector>
#ifndef _WIN32
#include <csignal>
#endif
namespace {
constexpr int kRuntimeError = 1;
constexpr int kUsageError = 64;
struct Entry {
  std::string storeDir, storeName, soundFont;
  bool create{false};
};
void usage() {
  std::cout
      << "Usage: OpenHDKLibraryAudio --store-dir ABSOLUTE_DIR --store-name PRIMARY "
         "create|session --soundfont FILE\n"
      << "Commands: the OpenHDKLibrary management commands plus play SONG-ID, stop, "
         "status. Playback is experimental; library mutations are rejected while "
         "playback is active.\n";
}
} // namespace
int main(int argc, char** argv) {
  using namespace OpenHDK;
#ifndef _WIN32
  // Broken pipes become explicit delivery failures, matching the library entry.
  std::signal(SIGPIPE, SIG_IGN);
#endif
  try {
    Entry entry;
    bool haveMode = false;
    for (int i = 1; i < argc; ++i) {
      const std::string_view argument = argv[i];
      const auto value = [&](std::string& target) -> bool {
        if (i + 1 >= argc) return false;
        target = argv[++i];
        return true;
      };
      if (argument == "--store-dir") {
        if (!value(entry.storeDir)) { usage(); return kUsageError; }
      } else if (argument == "--store-name") {
        if (!value(entry.storeName)) { usage(); return kUsageError; }
      } else if (argument == "--soundfont") {
        if (!value(entry.soundFont)) { usage(); return kUsageError; }
      } else if (argument == "create") {
        entry.create = true; haveMode = true;
      } else if (argument == "session") {
        entry.create = false; haveMode = true;
      } else if (argument == "--help" || argument == "-h") {
        usage(); return 0;
      } else {
        usage(); return kUsageError;
      }
    }
    if (entry.storeDir.empty() || entry.storeName.empty() || entry.soundFont.empty()
        || !haveMode) {
      usage(); return kUsageError;
    }
    const auto caller = LibraryCliSession::kCallerBytes + entry.storeDir.size()
        + entry.storeName.size();
    auto opened = entry.create
        ? DurableLibraryService::createLinux(std::make_unique<SongDiscovery>(),
              entry.storeDir, entry.storeName, {}, {}, caller)
        : DurableLibraryService::openLinux(entry.storeDir, entry.storeName, {}, {}, caller);
    if (opened.error) {
      auto output = LibraryCliSession::factoryError(*opened.error);
      if (!output.bytes) return 74;
      std::cout << *output.bytes << std::flush;
      return std::cout ? LibraryCliSession::factoryExit(*opened.error) : 74;
    }
    FluidSynthBackend backend;
    std::string initFailure;
    if (!initializeAudioBackend(entry.soundFont, backend, initFailure)) {
      // Clear failure: this executable does not run a half-initialized session.
      std::cerr << "Audio initialization failed: " << initFailure << '\n';
      return kRuntimeError;
    }
    PlaybackController controller(backend);
    LibraryCliSession session(std::move(opened.service),
        [&controller] { return controller.libraryMutationAllowed(); });
    LibraryAudioSession audio(session, controller);
    LibraryCliJsonRecord row;
    row.text("type", "session");
    row.text("mode", entry.create ? "create" : "session");
    row.text("audio", "enabled");
    if (opened.receipt) {
      row.text("status", "Saved");
      row.number("revision", opened.receipt->capturedRevision);
      row.number("sequence", opened.receipt->token.sequence);
      if (opened.receipt->cleanupWarning)
        row.number("cleanup_warning",
            static_cast<std::uint64_t>(opened.receipt->cleanupWarning->code));
    } else row.text("status", "Opened");
    auto greeting = row.finish();
    if (!greeting.bytes) return 74;
    std::cout << *greeting.bytes << std::flush;
    if (!std::cout) return 74;
    return audio.run(std::cin, std::cout);
    // Destruction order: audio session, guarded session, controller (ordered
    // stop with the backend's noexcept shutdown fallback), then the backend.
  } catch (const std::bad_alloc&) { return 74; }
    catch (const std::filesystem::filesystem_error&) { return kRuntimeError; }
}