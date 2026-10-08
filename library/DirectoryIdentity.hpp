// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/FilesystemSource.hpp"

namespace OpenHDK::LibraryFilesystem {
struct DirectoryIdentity {
  std::uint64_t volume;
  std::uint64_t file;
  bool operator==(const DirectoryIdentity&) const = default;
};
// Control-path native directory observation. Retain the handle through staging
// so replacement cannot recycle the observed directory identity before verification.
class DirectoryHandle {
 public:
  explicit DirectoryHandle(const fs::path& path) noexcept {
#ifdef _WIN32
    handle_ = CreateFileW(path.c_str(), 0U, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if (handle_ != INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle_, &info) &&
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
      identity_ = DirectoryIdentity{info.dwVolumeSerialNumber,
          (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32U) | info.nFileIndexLow};
    }
#else
    do { handle_ = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); }
    while (handle_ < 0 && errno == EINTR);
    struct stat info{};
    if (handle_ >= 0 && ::fstat(handle_, &info) == 0 && S_ISDIR(info.st_mode))
      identity_ = DirectoryIdentity{static_cast<std::uint64_t>(info.st_dev),
          static_cast<std::uint64_t>(info.st_ino)};
#endif
  }
  ~DirectoryHandle() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
    if (handle_ >= 0) ::close(handle_);
#endif
  }
  DirectoryHandle(const DirectoryHandle&) = delete;
  DirectoryHandle& operator=(const DirectoryHandle&) = delete;
  const std::optional<DirectoryIdentity>& identity() const noexcept { return identity_; }
 private:
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int handle_ = -1;
#endif
  std::optional<DirectoryIdentity> identity_;
};
}  // namespace OpenHDK::LibraryFilesystem
