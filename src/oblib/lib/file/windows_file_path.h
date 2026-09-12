// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef OCEANBASE_LIB_FILE_WINDOWS_FILE_PATH_H_
#define OCEANBASE_LIB_FILE_WINDOWS_FILE_PATH_H_

#ifdef _WIN32
#include <stdint.h>
#include <windows.h>

namespace oceanbase {
namespace common {
class ObIAllocator;

// Owns a normalized drive-absolute path and its Win32 extended spelling.
// The allocator must outlive this object. Relative input is resolved at assign()
// against the OS startup context; instance consumers must pass absolute paths.
// No process cwd, code page, locale, or long-path policy is changed.
class WindowsFilePath
{
public:
  static constexpr int64_t BASE_PATH_UNITS = 2048;
  static constexpr int64_t FILE_PATH_UNITS = 4096;
  explicit WindowsFilePath(ObIAllocator &allocator);
  ~WindowsFilePath();
  WindowsFilePath(const WindowsFilePath &) = delete;
  WindowsFilePath &operator=(const WindowsFilePath &) = delete;

  int assign(const char *path);
  int assign(const char *path, int64_t bytes, int64_t limit = FILE_PATH_UNITS);
  // Resolves only relative input against a live directory handle, never cwd.
  int assign_at(HANDLE directory, const char *relative);
  int error_to_errno(int result) const;
  const wchar_t *wide() const { return wide_; }
  const char *utf8() const { return utf8_; }
  int64_t length() const { return units_; } // excludes NUL and extended prefix
  DWORD win32_error() const { return win32_error_; }
  // Fully validates before the first mkdir. Existing non-directories are errors.
  int create_directory(bool recursive);
  int get_info(WIN32_FILE_ATTRIBUTE_DATA &info);
  int check_mode(int mode, bool &allowed);
  int open(int flags, int mode, int &fd);
  int delete_file();
  int delete_directory();
  int get_disk_space(int64_t &total, int64_t &available);
  // Iterative traversal bounds stack usage. Directory reparse points are removed
  // as entries, never traversed into another tree.
  int remove_tree(bool temporary_only);

private:
  int assign_input(const char *path, int64_t bytes, int64_t limit);
  void reset();
  int create_one_directory();
  ObIAllocator &allocator_;
  wchar_t *wide_;
  char *utf8_;
  int64_t units_;
  DWORD win32_error_;
};
// Owns the search handle and base path; next() produces an owned UTF-8 child.
class WindowsDirectoryIterator
{
public:
  explicit WindowsDirectoryIterator(ObIAllocator &allocator);
  ~WindowsDirectoryIterator();
  WindowsDirectoryIterator(const WindowsDirectoryIterator &) = delete;
  WindowsDirectoryIterator &operator=(const WindowsDirectoryIterator &) = delete;
  int open(const WindowsFilePath &directory);
  int next(WindowsFilePath &child, DWORD &attributes);
  DWORD win32_error() const { return win32_error_; }
private:
  void close();
  ObIAllocator &allocator_;
  WindowsFilePath directory_;
  HANDLE find_;
  WIN32_FIND_DATAW entry_;
  bool first_;
  bool opened_;
  DWORD win32_error_;
};
} // namespace common
} // namespace oceanbase
#endif // _WIN32
#endif
