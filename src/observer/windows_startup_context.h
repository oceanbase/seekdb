// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#pragma once
#ifdef _WIN32
#include "lib/file/windows_file_path.h"

namespace oceanbase {
namespace observer {
// Owned by wmain until console or service execution has finished. Capture is
// one-shot; no CRT code-page argv, process cwd or locale mutation is involved.
class WindowsStartupContext
{
public:
  explicit WindowsStartupContext(common::ObIAllocator &allocator);
  ~WindowsStartupContext();
  WindowsStartupContext(const WindowsStartupContext &) = delete;
  WindowsStartupContext &operator=(const WindowsStartupContext &) = delete;
  int capture(int argc, wchar_t *const argv[]);
  // Caller owns the returned mutable CreateProcessW buffer.
  int build_daemon_command(common::ObIAllocator &allocator, wchar_t *&command) const;
  int argc() const { return argc_; }
  char **argv() const { return argv_; }
  wchar_t *const *wide_argv() const { return wide_argv_; }
  const wchar_t *cwd() const { return cwd_; }
  const wchar_t *executable() const { return executable_; }
  DWORD win32_error() const { return win32_error_; }
  const char *stage() const { return stage_; }
private:
  int encode(const wchar_t *input, int64_t length, char *&output);
  common::ObIAllocator &allocator_;
  int argc_;
  char **argv_;
  wchar_t **wide_argv_;
  wchar_t *cwd_;
  wchar_t *executable_;
  DWORD win32_error_;
  const char *stage_;
};

// Instance paths are fixed before creating any directory. The owner must
// outlive all consumers; CLI data/redo resolve before the historical chdir.
class WindowsInstancePaths
{
public:
  explicit WindowsInstancePaths(common::ObIAllocator &allocator);
  int initialize(const char *base, const char *data, const char *redo);
  int create_directories();
  int install_executable(const WindowsStartupContext &startup);
  const common::WindowsFilePath &base() const { return base_; }
  const common::WindowsFilePath &data() const { return data_; }
  const common::WindowsFilePath &redo() const { return redo_; }
  const common::WindowsFilePath &run() const { return run_; }
  const common::WindowsFilePath &log() const { return log_; }
  const common::WindowsFilePath &etc() const { return etc_; }
  const common::WindowsFilePath &pid() const { return pid_; }
  const common::WindowsFilePath &log_file() const { return log_file_; }
  const char *option() const { return option_; }
  DWORD win32_error() const { return win32_error_; }
  int64_t input_bytes() const { return input_bytes_; }
  int64_t input_units() const { return input_units_; }
private:
  void set_input(const char *input, const char *option);
  int normalize(common::WindowsFilePath &path, const char *input, const char *option, int64_t limit);
  int derive(common::WindowsFilePath &path, const char *suffix);
  common::ObIAllocator &allocator_;
  common::WindowsFilePath base_, data_, redo_, run_, log_, etc_, pid_, log_file_, run_executable_;
  bool attempted_;
  bool ready_;
  const char *option_;
  DWORD win32_error_;
  int64_t input_bytes_;
  int64_t input_units_;
};
} // namespace observer
} // namespace oceanbase
#endif
