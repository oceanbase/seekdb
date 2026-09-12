// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "observer/windows_startup_context.h"
#ifdef _WIN32
#include "lib/alloc/ob_iallocator.h"
#include "lib/ob_errno.h"
#include <cstring>
#include <cwchar>

namespace oceanbase {
namespace observer {
using namespace common;

WindowsStartupContext::WindowsStartupContext(ObIAllocator &allocator)
  : allocator_(allocator), argc_(0), argv_(nullptr), wide_argv_(nullptr),
    cwd_(nullptr), executable_(nullptr), win32_error_(0), stage_("arguments")
{}

WindowsStartupContext::~WindowsStartupContext()
{
  for (int i = 0; i < argc_; ++i) {
    if (argv_ != nullptr && argv_[i] != nullptr) { allocator_.free(argv_[i]); }
    if (wide_argv_ != nullptr && wide_argv_[i] != nullptr) { allocator_.free(wide_argv_[i]); }
  }
  if (argv_ != nullptr) { allocator_.free(argv_); }
  if (wide_argv_ != nullptr) { allocator_.free(wide_argv_); }
  if (cwd_ != nullptr) { allocator_.free(cwd_); }
  if (executable_ != nullptr) { allocator_.free(executable_); }
}

int WindowsStartupContext::encode(const wchar_t *input, int64_t length, char *&output)
{
  int ret = OB_SUCCESS;
  const int count = length == 0 ? 0 : WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
      input, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
  if (length != 0 && count == 0) {
    win32_error_ = GetLastError();
    ret = OB_INVALID_ARGUMENT;
  } else if (nullptr == (output = static_cast<char *>(allocator_.alloc(count + 1)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (count != 0 && count != WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
      input, static_cast<int>(length), output, count, nullptr, nullptr)) {
    win32_error_ = GetLastError();
    ret = OB_INVALID_ARGUMENT;
  } else {
    output[count] = '\0';
  }
  return ret;
}

int WindowsStartupContext::capture(int argc, wchar_t *const argv[])
{
  int ret = OB_SUCCESS;
  if (argc_ != 0) {
    ret = OB_INIT_TWICE;
  } else if (argc <= 0 || argc > 32767 || argv == nullptr) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    argc_ = argc;
    argv_ = static_cast<char **>(allocator_.alloc((argc + 1LL) * sizeof(char *)));
    if (argv_ != nullptr) { memset(argv_, 0, (argc + 1LL) * sizeof(char *)); }
    wide_argv_ = static_cast<wchar_t **>(allocator_.alloc((argc + 1LL) * sizeof(wchar_t *)));
    if (wide_argv_ != nullptr) { memset(wide_argv_, 0, (argc + 1LL) * sizeof(wchar_t *)); }
    if (argv_ == nullptr || wide_argv_ == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    for (int i = 0; ret == OB_SUCCESS && i < argc; ++i) {
      const int64_t length = argv[i] == nullptr ? 0 : wcsnlen(argv[i], 32768);
      if (argv[i] == nullptr) {
        ret = OB_INVALID_ARGUMENT;
      } else if (length >= 32767) {
        ret = OB_SIZE_OVERFLOW;
      } else if (nullptr == (wide_argv_[i] = static_cast<wchar_t *>(
          allocator_.alloc((length + 1) * sizeof(wchar_t))))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        memcpy(wide_argv_[i], argv[i], (length + 1) * sizeof(wchar_t));
        ret = encode(wide_argv_[i], length, argv_[i]);
      }
    }
  }
  if (ret == OB_SUCCESS) {
    stage_ = "startup-cwd";
    const DWORD capacity = GetCurrentDirectoryW(0, nullptr);
    if (capacity == 0) {
      win32_error_ = GetLastError();
      ret = OB_IO_ERROR;
    } else if (capacity > 32767) {
      ret = OB_SIZE_OVERFLOW;
    } else if (nullptr == (cwd_ = static_cast<wchar_t *>(allocator_.alloc(capacity * sizeof(wchar_t))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      const DWORD actual = GetCurrentDirectoryW(capacity, cwd_);
      if (actual == 0) { win32_error_ = GetLastError(); ret = OB_IO_ERROR; }
      else if (actual >= capacity) { ret = OB_SIZE_OVERFLOW; }
    }
  }
  if (ret == OB_SUCCESS) {
    stage_ = "executable";
    bool complete = false;
    for (DWORD capacity = 256; ret == OB_SUCCESS && !complete; capacity *= 2) {
      if (capacity > 32768) {
        ret = OB_SIZE_OVERFLOW;
      } else if (nullptr == (executable_ = static_cast<wchar_t *>(allocator_.alloc(capacity * sizeof(wchar_t))))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        const DWORD actual = GetModuleFileNameW(nullptr, executable_, capacity);
        if (actual == 0) {
          win32_error_ = GetLastError();
          ret = OB_IO_ERROR;
        } else if (actual < capacity) {
          complete = true;
        } else {
          allocator_.free(executable_);
          executable_ = nullptr;
        }
      }
    }
  }
  return ret;
}

int WindowsStartupContext::build_daemon_command(ObIAllocator &allocator, wchar_t *&command) const
{
  int ret = OB_SUCCESS;
  command = nullptr;
  if (executable_ == nullptr || cwd_ == nullptr || argv_ == nullptr) {
    ret = OB_NOT_INIT;
  } else {
    // Quote every argument using the Windows CRT backslash/quote rules.
    // Count first so no truncated command can reach CreateProcessW.
    int64_t count = 0;
    auto emit_command = [&](wchar_t *output) {
      int64_t pos = 0;
      auto emit = [&](wchar_t ch) {
        if (pos < 32767) {
          if (output != nullptr) { output[pos] = ch; }
          ++pos;
        } else {
          ret = OB_SIZE_OVERFLOW;
        }
      };
      auto argument = [&](const wchar_t *value) {
        emit(L'"');
        int64_t slashes = 0;
        for (const wchar_t *p = value; ret == OB_SUCCESS; ++p) {
          if (*p == L'\\') {
            ++slashes;
          } else {
            const int64_t n = (*p == L'"' || *p == L'\0') ? slashes * 2 : slashes;
            for (int64_t i = 0; ret == OB_SUCCESS && i < n; ++i) { emit(L'\\'); }
            slashes = 0;
            if (*p == L'\0') { break; }
            if (*p == L'"') { emit(L'\\'); }
            emit(*p);
          }
        }
        emit(L'"');
      };
      argument(executable_);
      emit(L' ');
      argument(L"--nodaemon"); // Before all user options, including "--".
      for (int i = 1; ret == OB_SUCCESS && i < argc_; ++i) {
        emit(L' ');
        argument(wide_argv_[i]);
      }
      emit(L'\0');
      return pos;
    };
    count = emit_command(nullptr);
    if (ret != OB_SUCCESS) {
    } else if (nullptr == (command = static_cast<wchar_t *>(allocator.alloc(count * sizeof(wchar_t))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      emit_command(command);
    }
  }
  return ret;
}

WindowsInstancePaths::WindowsInstancePaths(ObIAllocator &allocator)
  : allocator_(allocator), base_(allocator), data_(allocator), redo_(allocator),
    run_(allocator), log_(allocator), etc_(allocator), pid_(allocator), log_file_(allocator), run_executable_(allocator),
    attempted_(false), ready_(false), option_("base-dir"), win32_error_(0),
    input_bytes_(0), input_units_(0)
{}

void WindowsInstancePaths::set_input(const char *input, const char *option)
{
  option_ = option;
  input_bytes_ = input == nullptr ? 0 : strnlen(input, 4 * 32767 + 1);
  input_units_ = input_bytes_ <= 4 * 32767 && input_bytes_ > 0
      ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input,
          static_cast<int>(input_bytes_), nullptr, 0) : 0;
  win32_error_ = 0;
}

int WindowsInstancePaths::normalize(WindowsFilePath &path, const char *input,
                                   const char *option, int64_t limit)
{
  set_input(input, option);
  const int ret = path.assign(input, input_bytes_, limit);
  win32_error_ = path.win32_error();
  return ret;
}

int WindowsInstancePaths::derive(WindowsFilePath &path, const char *suffix)
{
  int ret = OB_SUCCESS;
  const int64_t bytes = strlen(base_.utf8()) + strlen(suffix) + 1;
  char *joined = static_cast<char *>(allocator_.alloc(bytes));
  if (joined == nullptr) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    memcpy(joined, base_.utf8(), strlen(base_.utf8()));
    memcpy(joined + strlen(base_.utf8()), suffix, strlen(suffix) + 1);
    ret = normalize(path, joined, "base-dir-derived", WindowsFilePath::FILE_PATH_UNITS);
    allocator_.free(joined);
  }
  return ret;
}

int WindowsInstancePaths::initialize(const char *base, const char *data, const char *redo)
{
  int ret = OB_SUCCESS;
  if (attempted_) {
    ret = OB_INIT_TWICE;
  } else {
    attempted_ = true;
    ret = normalize(base_, base, "base-dir", WindowsFilePath::BASE_PATH_UNITS);
    if (ret == OB_SUCCESS && data != nullptr && data[0] != '\0') {
      ret = normalize(data_, data, "data-dir", WindowsFilePath::FILE_PATH_UNITS);
    }
    if (ret == OB_SUCCESS && redo != nullptr && redo[0] != '\0') {
      ret = normalize(redo_, redo, "redo-dir", WindowsFilePath::FILE_PATH_UNITS);
    }
    auto equal = [](const WindowsFilePath &a, const WindowsFilePath &b) {
      return a.wide() != nullptr && b.wide() != nullptr &&
          CompareStringOrdinal(a.wide(), -1, b.wide(), -1, TRUE) == CSTR_EQUAL;
    };
    if (ret == OB_SUCCESS && equal(base_, data_)) { set_input(data, "data-dir"); ret = OB_INVALID_ARGUMENT; }
    if (ret == OB_SUCCESS && (equal(base_, redo_) || equal(data_, redo_))) {
      set_input(redo, "redo-dir"); ret = OB_INVALID_ARGUMENT;
    }
    if (ret == OB_SUCCESS) { ret = derive(run_, "\\run"); }
    if (ret == OB_SUCCESS) { ret = derive(log_, "\\log"); }
    if (ret == OB_SUCCESS) { ret = derive(etc_, "\\etc"); }
    if (ret == OB_SUCCESS) { ret = derive(pid_, "\\run\\seekdb.pid"); }
    if (ret == OB_SUCCESS) { ret = derive(log_file_, "\\log\\seekdb.log"); }
    if (ret == OB_SUCCESS) { ret = derive(run_executable_, "\\run\\seekdb.exe"); }
    ready_ = ret == OB_SUCCESS;
  }
  return ret;
}

int WindowsInstancePaths::install_executable(const WindowsStartupContext &startup)
{
  int ret = OB_SUCCESS;
  win32_error_ = 0;
  if (!ready_ || startup.executable() == nullptr) {
    ret = OB_NOT_INIT;
  } else {
    // Preserve the existing best-effort copy/hardlink behavior, using only
    // the preflighted instance target and the captured executable path.
    (void)DeleteFileW(run_executable_.wide());
    if (!CopyFileW(startup.executable(), run_executable_.wide(), FALSE) &&
        !CreateHardLinkW(run_executable_.wide(), startup.executable(), nullptr)) {
      win32_error_ = GetLastError();
      ret = OB_IO_ERROR;
    }
  }
  return ret;
}

int WindowsInstancePaths::create_directories()
{
  int ret = OB_SUCCESS;
  if (!ready_) {
    ret = OB_NOT_INIT;
  } else {
    option_ = "base-dir";
    ret = base_.create_directory(true);
    win32_error_ = base_.win32_error();
    if (ret == OB_SUCCESS && data_.wide() != nullptr) {
      option_ = "data-dir"; ret = data_.create_directory(true); win32_error_ = data_.win32_error();
    }
    if (ret == OB_SUCCESS && redo_.wide() != nullptr) {
      option_ = "redo-dir"; ret = redo_.create_directory(true); win32_error_ = redo_.win32_error();
    }
  }
  return ret;
}
} // namespace observer
} // namespace oceanbase
#endif
