// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/SongCatalog.hpp"
#include <array>
#include <cerrno>
#include <filesystem>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace OpenHDK::LibraryFilesystem {
namespace fs = std::filesystem;
inline std::string utf8(const fs::path& path) {
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}
inline fs::path fromUtf8(std::string_view text) {
  return fs::path(std::u8string(text.begin(), text.end()));
}
inline bool contains(const fs::path& root, const fs::path& path) {
  auto a = root.begin(), b = path.begin();
  for (; a != root.end(); ++a, ++b) {
    if (b == path.end()) return false;
#ifdef _WIN32
    if (!catalogAliases(utf8(*a), utf8(*b))) return false;
#else
    if (*a != *b) return false;
#endif
  }
  return true;
}
inline bool reparse(const fs::path& path, std::error_code& ec) {
#ifdef _WIN32
  const auto attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) { ec = std::error_code(static_cast<int>(GetLastError()), std::system_category()); return false; }
  return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
#else
  const auto status = fs::symlink_status(path, ec);
  return !ec && fs::is_symlink(status);
#endif
}
enum class ReadError { None, Unreadable, AmbiguousPath, LimitExceeded };
struct ReadResult {
  ReadError error = ReadError::None;
  std::vector<std::uint8_t> bytes;
};

// Reads only regular files with no-follow traversal. All work is control-path I/O.
inline ReadResult read(const fs::path& root, std::string_view locator, std::size_t maximum) {
  if (!isCatalogLocator(locator)) return {ReadError::AmbiguousPath, {}};
  const auto relative = fromUtf8(locator);
#ifdef _WIN32
  struct Handle {
    HANDLE value;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit Handle(HANDLE v) : value(v) {}
  };
  // Keep each parent open without delete sharing while resolving its children.
  std::vector<std::unique_ptr<Handle>> parents;
  auto current = root;
  const auto lockDirectory = [&](const fs::path& path) {
    auto h = std::make_unique<Handle>(CreateFileW(path.c_str(), 0U, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (h->value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(h->value, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    parents.push_back(std::move(h)); return true;
  };
  if (!lockDirectory(current)) return {ReadError::Unreadable, {}};
  auto parts = relative.begin();
  while (parts != relative.end()) {
    const auto part = *parts++;
    if (parts == relative.end()) break;
    current /= part;
    if (!lockDirectory(current)) return {ReadError::AmbiguousPath, {}};
  }
  Handle file(CreateFileW((root / relative).c_str(), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (file.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.value, &info)) return {ReadError::Unreadable, {}};
  if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return {ReadError::AmbiguousPath, {}};
  const auto size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32U) | info.nFileSizeLow;
  if (size > maximum) return {ReadError::LimitExceeded, {}};
  // Reject resolved aliases (including short names) and escapes before reading.
  const auto resolved = [&](HANDLE h) -> fs::path {
    const DWORD count = GetFinalPathNameByHandleW(h, nullptr, 0U, FILE_NAME_NORMALIZED);
    if (count == 0U) return {};
    std::wstring name(count, L'\0');
    const DWORD length = GetFinalPathNameByHandleW(h, name.data(), count, FILE_NAME_NORMALIZED);
    if (length == 0U || length >= count) return {};
    name.resize(length); return fs::path(name);
  };
  const auto actualRoot = resolved(parents.front()->value), actualFile = resolved(file.value);
  if (actualRoot.empty() || actualFile.empty() ||
      !catalogAliases(utf8(actualRoot / relative), utf8(actualFile))) return {ReadError::AmbiguousPath, {}};
#else
  struct Descriptor {
    int value;
    explicit Descriptor(int v) : value(v) {}
    ~Descriptor() { if (value >= 0) ::close(value); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
  };
  Descriptor parent(::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (parent.value < 0) return {ReadError::Unreadable, {}};
  auto parts = relative.begin();
  while (parts != relative.end()) {
    const auto part = *parts++;
    const bool last = parts == relative.end();
    const int child = ::openat(parent.value, part.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC |
        (last ? O_NONBLOCK : O_DIRECTORY));
    if (child < 0) return {errno == ELOOP || errno == ENOTDIR ? ReadError::AmbiguousPath : ReadError::Unreadable, {}};
    ::close(parent.value); parent.value = child;
  }
  struct stat info{};
  if (::fstat(parent.value, &info) != 0) return {ReadError::Unreadable, {}};
  if (!S_ISREG(info.st_mode)) return {ReadError::AmbiguousPath, {}};
  if (info.st_size < 0 || static_cast<std::uint64_t>(info.st_size) > maximum) return {ReadError::LimitExceeded, {}};
#endif
  ReadResult result;
  std::array<std::uint8_t, 65536> chunk{};
  for (;;) {
#ifdef _WIN32
    DWORD count{};
    if (!ReadFile(file.value, chunk.data(), static_cast<DWORD>(chunk.size()), &count, nullptr)) return {ReadError::Unreadable, {}};
#else
    const auto got = ::read(parent.value, chunk.data(), chunk.size());
    if (got < 0) { if (errno == EINTR) continue; return {ReadError::Unreadable, {}}; }
    const auto count = static_cast<std::size_t>(got);
#endif
    if (count == 0U) break;
    if (count > maximum - result.bytes.size()) return {ReadError::LimitExceeded, {}};
    result.bytes.insert(result.bytes.end(), chunk.begin(), chunk.begin() + count);
  }
  return result;
}
}  // namespace OpenHDK::LibraryFilesystem
