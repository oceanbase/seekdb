// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
// Mounts the origin private file system at the data directory when asked,
// strips open flags WasmFS rejects, answers getcwd inside a mounted backend,
// emulates the directory moves the OPFS backend refuses, and reports failing
// file calls.
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <emscripten/emscripten.h>
#include <emscripten/wasmfs.h>
#include <wasi/api.h>

extern "C" backend_t seekdb_create_memory_backend();

namespace {
constexpr const char *MOVE_JOURNAL = "/seekdb/.move";
std::atomic<int> reported{0};
char working_directory[4096] = {0};
std::atomic<bool> working_directory_known{false};
std::mutex directory_move_mutex;

void report(const char *name, long ret, const char *path, long fd)
{
  if (reported.fetch_add(1) >= 300) return;
  if (path != nullptr) std::fprintf(stderr, "seekdb-runtime: %s(%s) -> %ld\n", name, path, ret);
  else std::fprintf(stderr, "seekdb-runtime: %s(fd %ld) -> %ld\n", name, fd, ret);
}

const char *text(intptr_t path)
{
  return reinterpret_cast<const char *>(path);
}

bool wal_index_file(const char *path)
{
  const size_t length = std::strlen(path);
  return length >= 4 && std::strcmp(path + length - 4, "-shm") == 0;
}

int move_directory_tree(const std::string &from, const std::string &to)
{
  if (mkdir(to.c_str(), 0755) != 0 && errno != EEXIST) return -errno;
  struct stat destination;
  if (stat(to.c_str(), &destination) != 0) return -errno;
  if (!S_ISDIR(destination.st_mode)) return -ENOTDIR;
  std::vector<std::string> names;
  DIR *dir = opendir(from.c_str());
  if (dir == nullptr) return -errno;
  while (dirent *entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (name != "." && name != "..") names.push_back(name);
  }
  closedir(dir);
  for (const std::string &name : names) {
    const std::string source = from + "/" + name;
    const std::string target = to + "/" + name;
    struct stat info;
    if (lstat(source.c_str(), &info) != 0) return -errno;
    if (S_ISDIR(info.st_mode)) {
      const int ret = move_directory_tree(source, target);
      if (ret != 0) return ret;
    } else {
      if (lstat(target.c_str(), &info) == 0) return -EEXIST;
      if (errno != ENOENT) return -errno;
      if (rename(source.c_str(), target.c_str()) != 0) return -errno;
    }
  }
  return rmdir(from.c_str()) == 0 ? 0 : -errno;
}

bool record_move(const char *from, const char *to)
{
  const int fd = open(MOVE_JOURNAL, O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (fd < 0) return false;
  const std::string record = std::string(from) + "\n" + to + "\n";
  const bool written = write(fd, record.data(), record.size()) == static_cast<ssize_t>(record.size()) && fsync(fd) == 0;
  close(fd);
  if (!written) unlink(MOVE_JOURNAL);
  return written;
}

int read_recorded_move(std::string &from, std::string &to)
{
  const int fd = open(MOVE_JOURNAL, O_RDONLY);
  if (fd < 0) return -errno;
  char buffer[8192];
  ssize_t length;
  do {
    length = read(fd, buffer, sizeof(buffer));
  } while (length < 0 && errno == EINTR);
  const int error = length < 0 ? errno : 0;
  close(fd);
  if (error != 0) return -error;
  if (length == sizeof(buffer)) return -EOVERFLOW;
  const std::string record(buffer, static_cast<size_t>(length));
  const size_t first = record.find('\n');
  const size_t second = first == std::string::npos ? std::string::npos : record.find('\n', first + 1);
  if (first == std::string::npos || first == 0 || second == std::string::npos
      || second == first + 1 || second + 1 != record.size()) return -EINVAL;
  from = record.substr(0, first);
  to = record.substr(first + 1, second - first - 1);
  return 0;
}

int finish_recorded_move()
{
  std::lock_guard<std::mutex> lock(directory_move_mutex);
  std::string from;
  std::string to;
  int ret = read_recorded_move(from, to);
  if (ret == -ENOENT) return 0;
  if (ret != 0) return ret;
  struct stat info;
  if (lstat(from.c_str(), &info) == 0) {
    ret = S_ISDIR(info.st_mode) ? move_directory_tree(from, to) : -ENOTDIR;
  } else if (errno != ENOENT) {
    ret = -errno;
  } else if (lstat(to.c_str(), &info) != 0) {
    ret = -errno;
  } else if (!S_ISDIR(info.st_mode)) {
    ret = -ENOTDIR;
  }
  if (ret == 0 && unlink(MOVE_JOURNAL) != 0) ret = -errno;
  std::fprintf(stderr, "seekdb-runtime: finished recorded directory move %s -> %s: %d\n", from.c_str(), to.c_str(), ret);
  return ret;
}
}

bool seekdb_mount_storage(bool persistent)
{
  if (!persistent) {
    backend_t backend = seekdb_create_memory_backend();
    return backend != nullptr && wasmfs_create_directory("/seekdb", 0777, backend) == 0;
  }
  const int available = EM_ASM_INT({
    return typeof navigator !== 'undefined' && navigator.storage
        && typeof navigator.storage.getDirectory === 'function' ? 1 : 0;
  });
  if (!available) {
    std::fprintf(stderr, "seekdb-runtime: persistent storage requested but OPFS is unavailable\n");
    return false;
  }
  backend_t backend = wasmfs_create_opfs_backend();
  if (backend == nullptr) {
    std::fprintf(stderr, "seekdb-runtime: creating the OPFS backend failed\n");
    return false;
  }
  const int ret = wasmfs_create_directory("/seekdb", 0777, backend);
  std::fprintf(stderr, "seekdb-runtime: OPFS mounted at /seekdb, status %d\n", ret);
  if (ret != 0) return false;
  return finish_recorded_move() == 0;
}

extern "C" {
int __real___syscall_openat(int dirfd, intptr_t path, int flags, ...);
int __real___syscall_newfstatat(int dirfd, intptr_t path, intptr_t buf, int flags);
int __real___syscall_stat64(intptr_t path, intptr_t buf);
int __real___syscall_lstat64(intptr_t path, intptr_t buf);
int __real___syscall_fstat64(int fd, intptr_t buf);
int __real___syscall_mkdirat(int dirfd, intptr_t path, int mode);
int __real___syscall_fcntl64(int fd, int cmd, ...);
int __real___syscall_renameat(int olddirfd, intptr_t oldpath, int newdirfd, intptr_t newpath);
int __real___syscall_ftruncate64(int fd, off_t length);
int __real___syscall_truncate64(intptr_t path, off_t length);
int __real___syscall_fallocate(int fd, int mode, off_t offset, off_t len);
int __real___syscall_getcwd(intptr_t buf, size_t size);
int __real___syscall_getdents64(int fd, intptr_t dirp, size_t count);
int __real___syscall_unlinkat(int dirfd, intptr_t path, int flags);
int __real___syscall_rmdir(intptr_t path);
int __real___syscall_statfs64(intptr_t path, size_t size, intptr_t buf);
int __real___syscall_fstatfs64(int fd, size_t size, intptr_t buf);
int __real___syscall_fdatasync(int fd);
int __real___syscall_faccessat(int dirfd, intptr_t path, int amode, int flags);
int __real___syscall_chdir(intptr_t path);
int __real___syscall_ioctl(int fd, int request, ...);
int __real___syscall_utimensat(int dirfd, intptr_t path, intptr_t times, int flags);
__wasi_errno_t __real___wasi_fd_pread(__wasi_fd_t fd, const __wasi_iovec_t *iovs, size_t iovs_len, __wasi_filesize_t offset, __wasi_size_t *nread);
__wasi_errno_t __real___wasi_fd_pwrite(__wasi_fd_t fd, const __wasi_ciovec_t *iovs, size_t iovs_len, __wasi_filesize_t offset, __wasi_size_t *nwritten);
__wasi_errno_t __real___wasi_fd_sync(__wasi_fd_t fd);
__wasi_errno_t __real___wasi_fd_seek(__wasi_fd_t fd, __wasi_filedelta_t offset, __wasi_whence_t whence, __wasi_filesize_t *newoffset);
__wasi_errno_t __real___wasi_fd_close(__wasi_fd_t fd);

int __wrap___syscall_openat(int dirfd, intptr_t path, int flags, ...)
{
  va_list args;
  va_start(args, flags);
  const int mode = va_arg(args, int);
  va_end(args);
  int unsupported = 0;
#if defined(O_DIRECT)
  unsupported |= O_DIRECT;
#endif
#if defined(O_SYNC)
  unsupported |= O_SYNC;
#endif
#if defined(O_DSYNC)
  unsupported |= O_DSYNC;
#endif
#if defined(O_NOATIME)
  unsupported |= O_NOATIME;
#endif
#if defined(O_NOCTTY)
  unsupported |= O_NOCTTY;
#endif
  static std::atomic<bool> stripped{false};
  if ((flags & unsupported) != 0 && !stripped.exchange(true)) {
    std::fprintf(stderr, "seekdb-runtime: WasmFS open flags 0x%x stripped to 0x%x for %s\n",
                 flags, flags & ~unsupported, text(path));
  }
  const int ret = __real___syscall_openat(dirfd, path, flags & ~unsupported, mode);
  if (ret < 0) report("openat", ret, text(path), dirfd);
  return ret;
}

int __wrap___syscall_newfstatat(int dirfd, intptr_t path, intptr_t buf, int flags)
{
  const int ret = __real___syscall_newfstatat(dirfd, path, buf, flags);
  if (ret < 0 && ret != -ENOENT) report("newfstatat", ret, text(path), dirfd);
  return ret;
}

int __wrap___syscall_stat64(intptr_t path, intptr_t buf)
{
  const int ret = __real___syscall_stat64(path, buf);
  if (ret < 0 && ret != -ENOENT) report("stat", ret, text(path), -1);
  return ret;
}

int __wrap___syscall_lstat64(intptr_t path, intptr_t buf)
{
  const int ret = __real___syscall_lstat64(path, buf);
  if (ret < 0 && ret != -ENOENT) report("lstat", ret, text(path), -1);
  return ret;
}

int __wrap___syscall_fstat64(int fd, intptr_t buf)
{
  const int ret = __real___syscall_fstat64(fd, buf);
  if (ret < 0) report("fstat", ret, nullptr, fd);
  return ret;
}

int __wrap___syscall_mkdirat(int dirfd, intptr_t path, int mode)
{
  const int ret = __real___syscall_mkdirat(dirfd, path, mode);
  if (ret < 0 && ret != -EEXIST) report("mkdirat", ret, text(path), dirfd);
  return ret;
}

int __wrap___syscall_fcntl64(int fd, int cmd, ...)
{
  va_list args;
  va_start(args, cmd);
  const intptr_t arg = va_arg(args, intptr_t);
  va_end(args);
  const int ret = __real___syscall_fcntl64(fd, cmd, arg);
  if (ret < 0) {
    char detail[64];
    std::snprintf(detail, sizeof(detail), "fd %d cmd %d", fd, cmd);
    report("fcntl", ret, detail, fd);
  }
  return ret;
}

int __wrap___syscall_renameat(int olddirfd, intptr_t oldpath, int newdirfd, intptr_t newpath)
{
  int ret = __real___syscall_renameat(olddirfd, oldpath, newdirfd, newpath);
  if ((ret == -EBUSY || ret == -ENOTEMPTY) && text(oldpath)[0] == '/' && text(newpath)[0] == '/') {
    struct stat info;
    if (stat(text(oldpath), &info) == 0 && S_ISDIR(info.st_mode)) {
      std::lock_guard<std::mutex> lock(directory_move_mutex);
      std::string from;
      std::string to;
      int journal_ret = read_recorded_move(from, to);
      if (journal_ret == 0 && lstat(from.c_str(), &info) != 0 && errno == ENOENT
          && lstat(to.c_str(), &info) == 0 && S_ISDIR(info.st_mode)
          && unlink(MOVE_JOURNAL) == 0) {
        journal_ret = -ENOENT;
      }
      bool recorded = journal_ret == 0 && from == text(oldpath) && to == text(newpath);
      if (!recorded && ret == -EBUSY && journal_ret == -ENOENT) {
        recorded = record_move(text(oldpath), text(newpath));
      }
      if (recorded) {
        ret = move_directory_tree(text(oldpath), text(newpath));
        if (ret == 0) unlink(MOVE_JOURNAL);
        std::fprintf(stderr, "seekdb-runtime: emulated directory move %s -> %s: %d\n", text(oldpath), text(newpath), ret);
      }
    }
  }
  if (ret < 0) {
    char detail[512];
    std::snprintf(detail, sizeof(detail), "%s -> %s", text(oldpath), text(newpath));
    report("renameat", ret, detail, olddirfd);
  }
  return ret;
}

int __wrap___syscall_ftruncate64(int fd, off_t length)
{
  const int ret = __real___syscall_ftruncate64(fd, length);
  if (ret < 0) report("ftruncate", ret, nullptr, fd);
  return ret;
}

int __wrap___syscall_truncate64(intptr_t path, off_t length)
{
  const int ret = __real___syscall_truncate64(path, length);
  if (ret < 0) report("truncate", ret, text(path), -1);
  return ret;
}

int __wrap___syscall_fallocate(int fd, int mode, off_t offset, off_t len)
{
  const int ret = __real___syscall_fallocate(fd, mode, offset, len);
  if (ret < 0) report("fallocate", ret, nullptr, fd);
  return ret;
}

int __wrap___syscall_getcwd(intptr_t buf, size_t size)
{
  const int ret = __real___syscall_getcwd(buf, size);
  if (ret < 0) {
    report("getcwd", ret, nullptr, -1);
    return ret;
  }
  char *out = reinterpret_cast<char *>(buf);
  if (working_directory_known.load(std::memory_order_acquire) && std::strcmp(out, "/") == 0) {
    const size_t length = std::strlen(working_directory) + 1;
    if (length > size) return -ERANGE;
    std::memcpy(out, working_directory, length);
    return static_cast<int>(length);
  }
  return ret;
}

int __wrap___syscall_getdents64(int fd, intptr_t dirp, size_t count)
{
  const int ret = __real___syscall_getdents64(fd, dirp, count);
  if (ret < 0) report("getdents", ret, nullptr, fd);
  return ret;
}

int __wrap___syscall_unlinkat(int dirfd, intptr_t path, int flags)
{
  const int ret = __real___syscall_unlinkat(dirfd, path, flags);
  if (ret < 0 && ret != -ENOENT && !(ret == -EIO && wal_index_file(text(path)))) report("unlinkat", ret, text(path), dirfd);
  return ret;
}

int __wrap___syscall_rmdir(intptr_t path)
{
  const int ret = __real___syscall_rmdir(path);
  if (ret < 0 && ret != -ENOENT) report("rmdir", ret, text(path), -1);
  return ret;
}

int __wrap___syscall_statfs64(intptr_t path, size_t size, intptr_t buf)
{
  const int ret = __real___syscall_statfs64(path, size, buf);
  if (ret < 0) report("statfs", ret, text(path), -1);
  return ret;
}

int __wrap___syscall_fstatfs64(int fd, size_t size, intptr_t buf)
{
  const int ret = __real___syscall_fstatfs64(fd, size, buf);
  if (ret < 0) report("fstatfs", ret, nullptr, fd);
  return ret;
}

int __wrap___syscall_fdatasync(int fd)
{
  const int ret = __real___syscall_fdatasync(fd);
  if (ret < 0) report("fdatasync", ret, nullptr, fd);
  return ret;
}

int __wrap___syscall_faccessat(int dirfd, intptr_t path, int amode, int flags)
{
  const int ret = __real___syscall_faccessat(dirfd, path, amode, flags);
  if (ret < 0 && ret != -ENOENT) report("faccessat", ret, text(path), dirfd);
  return ret;
}

int __wrap___syscall_chdir(intptr_t path)
{
  const int ret = __real___syscall_chdir(path);
  if (ret < 0) {
    report("chdir", ret, text(path), -1);
  } else if (text(path)[0] == '/' && std::strlen(text(path)) < sizeof(working_directory)) {
    std::strcpy(working_directory, text(path));
    working_directory_known.store(true, std::memory_order_release);
  }
  return ret;
}

int __wrap___syscall_ioctl(int fd, int request, ...)
{
  va_list args;
  va_start(args, request);
  const intptr_t arg = va_arg(args, intptr_t);
  va_end(args);
  const int ret = __real___syscall_ioctl(fd, request, arg);
  if (ret < 0) report("ioctl", ret, nullptr, fd);
  return ret;
}

int __wrap___syscall_utimensat(int dirfd, intptr_t path, intptr_t times, int flags)
{
  const int ret = __real___syscall_utimensat(dirfd, path, times, flags);
  if (ret < 0) report("utimensat", ret, text(path), dirfd);
  return ret;
}

__wasi_errno_t __wrap___wasi_fd_pread(__wasi_fd_t fd, const __wasi_iovec_t *iovs, size_t iovs_len, __wasi_filesize_t offset, __wasi_size_t *nread)
{
  const __wasi_errno_t ret = __real___wasi_fd_pread(fd, iovs, iovs_len, offset, nread);
  if (ret != 0) report("fd_pread", ret, nullptr, fd);
  return ret;
}

__wasi_errno_t __wrap___wasi_fd_pwrite(__wasi_fd_t fd, const __wasi_ciovec_t *iovs, size_t iovs_len, __wasi_filesize_t offset, __wasi_size_t *nwritten)
{
  const __wasi_errno_t ret = __real___wasi_fd_pwrite(fd, iovs, iovs_len, offset, nwritten);
  if (ret != 0) report("fd_pwrite", ret, nullptr, fd);
  return ret;
}

__wasi_errno_t __wrap___wasi_fd_sync(__wasi_fd_t fd)
{
  const __wasi_errno_t ret = __real___wasi_fd_sync(fd);
  if (ret != 0) report("fd_sync", ret, nullptr, fd);
  return ret;
}

__wasi_errno_t __wrap___wasi_fd_seek(__wasi_fd_t fd, __wasi_filedelta_t offset, __wasi_whence_t whence, __wasi_filesize_t *newoffset)
{
  const __wasi_errno_t ret = __real___wasi_fd_seek(fd, offset, whence, newoffset);
  if (ret != 0) report("fd_seek", ret, nullptr, fd);
  return ret;
}

__wasi_errno_t __wrap___wasi_fd_close(__wasi_fd_t fd)
{
  const __wasi_errno_t ret = __real___wasi_fd_close(fd);
  if (ret != 0 && ret != __WASI_ERRNO_BADF) report("fd_close", ret, nullptr, fd);
  return ret;
}
}
