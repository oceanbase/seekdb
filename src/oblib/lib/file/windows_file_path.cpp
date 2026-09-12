// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "lib/file/windows_file_path.h"
#ifdef _WIN32
#include "lib/alloc/ob_iallocator.h"
#include "lib/ob_errno.h"
#include <cstring>
#include <cwchar>
#include <fcntl.h>
#include <io.h>
#include <new>
#include <cerrno>

namespace oceanbase {
namespace common {
namespace {
bool is_drive_letter(wchar_t c)
{
  return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}

bool is_reserved_component(const wchar_t *part, int64_t length)
{
  int64_t stem = 0;
  while (stem < length && part[stem] != L'.') { ++stem; }
  while (stem > 0 && part[stem - 1] == L' ') { --stem; }
  // Longest DOS device stem is CONOUT$. This is a name, not a path buffer.
  wchar_t name[8] = {};
  if (stem > 7) { return false; }
  for (int64_t i = 0; i < stem; ++i) {
    name[i] = part[i] >= L'a' && part[i] <= L'z' ? part[i] - (L'a' - L'A') : part[i];
  }
  if (0 == wcscmp(name, L"CON") || 0 == wcscmp(name, L"PRN") ||
      0 == wcscmp(name, L"AUX") || 0 == wcscmp(name, L"NUL") ||
      0 == wcscmp(name, L"CONIN$") || 0 == wcscmp(name, L"CONOUT$")) {
    return true;
  }
  return stem == 4 && (0 == wcsncmp(name, L"COM", 3) || 0 == wcsncmp(name, L"LPT", 3)) &&
      ((name[3] >= L'1' && name[3] <= L'9') || name[3] == L'\u00b9' ||
       name[3] == L'\u00b2' || name[3] == L'\u00b3');
}

int validate_components(const wchar_t *path, int64_t length, int64_t start)
{
  for (int64_t end = start; end <= length; ++end) {
    if (end == length || path[end] == L'\\') {
      const int64_t size = end - start;
      if (size > 255) { return OB_SIZE_OVERFLOW; }
      if (size > 0 && !(size == 1 && path[start] == L'.') &&
          !(size == 2 && path[start] == L'.' && path[start + 1] == L'.')) {
        if (path[end - 1] == L' ' || path[end - 1] == L'.' ||
            is_reserved_component(path + start, size)) { return OB_INVALID_ARGUMENT; }
        for (int64_t i = start; i < end; ++i) {
          if (path[i] < 32 || wcschr(L"<>:\"|?*", path[i]) != nullptr) {
            return OB_INVALID_ARGUMENT;
          }
        }
      }
      start = end + 1;
    }
  }
  return OB_SUCCESS;
}
} // namespace

WindowsFilePath::WindowsFilePath(ObIAllocator &allocator)
  : allocator_(allocator), wide_(nullptr), utf8_(nullptr), units_(0), win32_error_(0)
{}

WindowsFilePath::~WindowsFilePath() { reset(); }

void WindowsFilePath::reset()
{
  if (wide_ != nullptr) { allocator_.free(wide_); wide_ = nullptr; }
  if (utf8_ != nullptr) { allocator_.free(utf8_); utf8_ = nullptr; }
  units_ = 0;
  win32_error_ = 0;
}

int WindowsFilePath::assign(const char *path)
{
  // Bound the scan and intermediate conversion independently of the final path.
  const int64_t bytes = path == nullptr ? 0 : strnlen(path, 4 * 32767 + 1);
  return assign(path, bytes);
}

int WindowsFilePath::assign(const char *path, int64_t bytes, int64_t limit)
{
  // Keep the previous value alive while decoding: callers may normalize utf8()
  // again. A failed assignment leaves no path available for a later file call.
  WindowsFilePath candidate(allocator_);
  const int ret = candidate.assign_input(path, bytes, limit);
  reset();
  win32_error_ = candidate.win32_error_;
  if (ret == OB_SUCCESS) {
    wide_ = candidate.wide_;
    utf8_ = candidate.utf8_;
    units_ = candidate.units_;
    candidate.wide_ = nullptr;
    candidate.utf8_ = nullptr;
  }
  return ret;
}

int WindowsFilePath::assign_at(HANDLE directory, const char *relative)
{
  int ret = OB_SUCCESS;
  DWORD error = ERROR_SUCCESS;
  wchar_t *directory_path = nullptr;
  char *joined = nullptr;
  do {
    const int64_t bytes = relative == nullptr ? 0 : strnlen(relative, 4 * 32767 + 1);
    if (bytes <= 0 || relative[0] == '/' || relative[0] == '\\' ||
        (bytes > 1 && relative[1] == ':')) {
      ret = OB_INVALID_ARGUMENT;
      break;
    } else if (bytes > 4 * 32767) {
      ret = OB_SIZE_OVERFLOW;
      break;
    }
    BY_HANDLE_FILE_INFORMATION info = {};
    if (!GetFileInformationByHandle(directory, &info)) {
      error = GetLastError();
      ret = OB_IO_ERROR;
      break;
    } else if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      error = ERROR_DIRECTORY;
      ret = OB_IO_ERROR;
      break;
    }
    const DWORD capacity = GetFinalPathNameByHandleW(directory, nullptr, 0, FILE_NAME_NORMALIZED);
    if (capacity == 0) {
      error = GetLastError();
      ret = OB_IO_ERROR;
      break;
    } else if (capacity > FILE_PATH_UNITS + 5) {
      ret = OB_SIZE_OVERFLOW;
      break;
    }
    directory_path = static_cast<wchar_t *>(allocator_.alloc(capacity * sizeof(wchar_t)));
    if (directory_path == nullptr) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      break;
    }
    const DWORD units = GetFinalPathNameByHandleW(directory, directory_path, capacity, FILE_NAME_NORMALIZED);
    if (units == 0 || units >= capacity) {
      error = units == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
      ret = OB_IO_ERROR;
      break;
    }
    const int directory_bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        directory_path, units, nullptr, 0, nullptr, nullptr);
    if (directory_bytes == 0) {
      error = GetLastError();
      ret = OB_INVALID_ARGUMENT;
      break;
    }
    joined = static_cast<char *>(allocator_.alloc(directory_bytes + bytes + 2));
    if (joined == nullptr) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      break;
    }
    if (directory_bytes != WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        directory_path, units, joined, directory_bytes, nullptr, nullptr)) {
      error = GetLastError();
      ret = OB_INVALID_ARGUMENT;
      break;
    }
    joined[directory_bytes] = '\\';
    memcpy(joined + directory_bytes + 1, relative, bytes);
    joined[directory_bytes + bytes + 1] = '\0';
    ret = assign(joined, directory_bytes + bytes + 1);
    error = win32_error_;
  } while (false);
  if (joined != nullptr) { allocator_.free(joined); }
  if (directory_path != nullptr) { allocator_.free(directory_path); }
  if (ret != OB_SUCCESS) { reset(); }
  win32_error_ = error;
  return ret;
}

int WindowsFilePath::error_to_errno(int result) const
{
  if (result == OB_SUCCESS) { return 0; }
  if (result == OB_SIZE_OVERFLOW) { return ENAMETOOLONG; }
  if (result == OB_ALLOCATE_MEMORY_FAILED) { return ENOMEM; }
  if (result == OB_INVALID_ARGUMENT) { return EINVAL; }
  switch (win32_error_) {
    case ERROR_INVALID_HANDLE: return EBADF;
    case ERROR_DIRECTORY: return ENOTDIR;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND: return ENOENT;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS: return EEXIST;
    case ERROR_ACCESS_DENIED: return EACCES;
    default: return EIO;
  }
}

int WindowsFilePath::assign_input(const char *path, int64_t bytes, int64_t limit)
{
  int ret = OB_SUCCESS;
  if (path == nullptr || bytes <= 0 || limit <= 0 || limit > FILE_PATH_UNITS) {
    return OB_INVALID_ARGUMENT;
  } else if (bytes > 4 * 32767) {
    return OB_SIZE_OVERFLOW;
  } else if (memchr(path, '\0', bytes) != nullptr) {
    return OB_INVALID_ARGUMENT;
  }
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path,
      static_cast<int>(bytes), nullptr, 0);
  if (count == 0) {
    win32_error_ = GetLastError();
    return OB_INVALID_ARGUMENT;
  } else if (count >= 32767) {
    return OB_SIZE_OVERFLOW;
  }
  wchar_t *input = static_cast<wchar_t *>(allocator_.alloc((count + 1) * sizeof(wchar_t)));
  wchar_t *absolute = nullptr;
  if (input == nullptr) {
    return OB_ALLOCATE_MEMORY_FAILED;
  }
  if (count != MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path,
      static_cast<int>(bytes), input, count)) {
    win32_error_ = GetLastError();
    ret = OB_INVALID_ARGUMENT;
  } else {
    input[count] = L'\0';
    for (int i = 0; i < count; ++i) { if (input[i] == L'/') { input[i] = L'\\'; } }
    wchar_t *normal = input;
    int64_t input_length = count;
    const bool extended = count >= 4 && 0 == wcsncmp(input, L"\\\\?\\", 4);
    if (extended) { normal += 4; input_length -= 4; }
    const bool drive = input_length >= 2 && is_drive_letter(normal[0]) && normal[1] == L':';
    if ((input_length >= 2 && normal[0] == L'\\' && normal[1] == L'\\') ||
        (extended && !(drive && input_length >= 3 && normal[2] == L'\\'))) {
      ret = OB_INVALID_ARGUMENT; // No UNC, device namespace or pipe adaptation.
    } else if (OB_SUCCESS != (ret = validate_components(normal, input_length, drive ? 2 : 0))) {
    } else {
      // Reject spelling that GetFullPathNameW would silently trim before calling it.
      const DWORD capacity = GetFullPathNameW(normal, 0, nullptr, nullptr);
      if (capacity == 0) {
        win32_error_ = GetLastError();
        ret = OB_IO_ERROR;
      } else if (capacity >= 32767) {
        ret = OB_SIZE_OVERFLOW;
      } else if (nullptr == (absolute = static_cast<wchar_t *>(
          allocator_.alloc((static_cast<int64_t>(capacity) + 4) * sizeof(wchar_t))))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        const DWORD actual = GetFullPathNameW(normal, capacity, absolute + 4, nullptr);
        if (actual == 0) {
          win32_error_ = GetLastError();
          ret = OB_IO_ERROR;
        } else if (actual >= capacity) {
          ret = OB_SIZE_OVERFLOW; // No truncation or retry against changing cwd.
        } else {
          int64_t length = actual;
          while (length > 3 && absolute[4 + length - 1] == L'\\') { --length; }
          absolute[4 + length] = L'\0';
          if (length < 3 || !is_drive_letter(absolute[4]) || absolute[5] != L':' ||
              absolute[6] != L'\\') {
            ret = OB_INVALID_ARGUMENT;
          } else if (length > limit) {
            ret = OB_SIZE_OVERFLOW;
          } else if (OB_SUCCESS != (ret = validate_components(absolute + 4, length, 3))) {
          } else {
            const int utf8_bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                absolute + 4, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
            if (utf8_bytes == 0) {
              win32_error_ = GetLastError();
              ret = OB_INVALID_ARGUMENT;
            } else if (nullptr == (utf8_ = static_cast<char *>(allocator_.alloc(utf8_bytes + 1)))) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
            } else if (utf8_bytes != WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                absolute + 4, static_cast<int>(length), utf8_, utf8_bytes, nullptr, nullptr)) {
              win32_error_ = GetLastError();
              ret = OB_INVALID_ARGUMENT;
            } else {
              utf8_[utf8_bytes] = '\0';
              memcpy(absolute, L"\\\\?\\", 4 * sizeof(wchar_t));
              wide_ = absolute;
              absolute = nullptr;
              units_ = length;
            }
          }
        }
      }
    }
  }
  allocator_.free(input);
  if (absolute != nullptr) { allocator_.free(absolute); }
  if (ret != OB_SUCCESS && utf8_ != nullptr) { allocator_.free(utf8_); utf8_ = nullptr; }
  return ret;
}

int WindowsFilePath::create_one_directory()
{
  int ret = OB_SUCCESS;
  if (!CreateDirectoryW(wide_, nullptr)) {
    const DWORD error = GetLastError();
    if (error != ERROR_ALREADY_EXISTS) {
      win32_error_ = error;
      ret = OB_IO_ERROR;
    } else {
      const DWORD attrs = GetFileAttributesW(wide_);
      if (attrs == INVALID_FILE_ATTRIBUTES) {
        win32_error_ = GetLastError();
        ret = OB_IO_ERROR;
      } else if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        win32_error_ = error;
        ret = OB_ENTRY_EXIST;
      }
    }
  }
  return ret;
}

int WindowsFilePath::create_directory(bool recursive)
{
  int ret = OB_SUCCESS;
  win32_error_ = 0;
  if (wide_ == nullptr) { return OB_NOT_INIT; }
  // Skip the extended prefix and the drive root. Always restore the owned path.
  for (int64_t i = 7; recursive && i < units_ + 4 && ret == OB_SUCCESS; ++i) {
    if (wide_[i] == L'\\') {
      wide_[i] = L'\0';
      ret = create_one_directory();
      wide_[i] = L'\\';
    }
  }
  if (ret == OB_SUCCESS) { ret = create_one_directory(); }
  return ret;
}

int WindowsFilePath::get_info(WIN32_FILE_ATTRIBUTE_DATA &info)
{
  int ret = OB_SUCCESS;
  win32_error_ = 0;
  info = {};
  if (wide_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if (!GetFileAttributesExW(wide_, GetFileExInfoStandard, &info)) {
    win32_error_ = GetLastError();
    ret = win32_error_ == ERROR_FILE_NOT_FOUND || win32_error_ == ERROR_PATH_NOT_FOUND
        ? OB_FILE_NOT_EXIST : OB_IO_ERROR;
  }
  return ret;
}

int WindowsFilePath::check_mode(int mode, bool &allowed)
{
  int ret = OB_SUCCESS;
  allowed = false;
  win32_error_ = 0;
  if (wide_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if (_waccess(wide_, mode) == 0) {
    allowed = true;
  } else {
    const int error = errno;
    unsigned long dos_error = 0;
    _get_doserrno(&dos_error);
    win32_error_ = dos_error;
    errno = error;
    // Preserve CRT attribute checks; this is not an ACL authorization promise.
    if (error != EACCES && error != ENOENT) { ret = OB_IO_ERROR; }
  }
  return ret;
}

int WindowsFilePath::open(int flags, int mode, int &fd)
{
  int ret = OB_SUCCESS;
  fd = -1;
  win32_error_ = 0;
  if (wide_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if ((fd = _wopen(wide_, flags | _O_BINARY, mode)) < 0) {
    const int error = errno;
    unsigned long dos_error = 0;
    _get_doserrno(&dos_error);
    win32_error_ = dos_error;
    errno = error;
    ret = OB_IO_ERROR;
  }
  return ret;
}

int WindowsFilePath::delete_file()
{
  int ret = OB_SUCCESS;
  win32_error_ = 0;
  if (wide_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if (!DeleteFileW(wide_)) {
    win32_error_ = GetLastError();
    ret = win32_error_ == ERROR_FILE_NOT_FOUND || win32_error_ == ERROR_PATH_NOT_FOUND
        ? OB_FILE_NOT_EXIST : OB_IO_ERROR;
  }
  return ret;
}

int WindowsFilePath::delete_directory()
{
  int ret = OB_SUCCESS;
  win32_error_ = 0;
  if (wide_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if (!RemoveDirectoryW(wide_)) {
    win32_error_ = GetLastError();
    ret = win32_error_ == ERROR_FILE_NOT_FOUND || win32_error_ == ERROR_PATH_NOT_FOUND
        ? OB_FILE_NOT_EXIST : OB_IO_ERROR;
  }
  return ret;
}

int WindowsFilePath::get_disk_space(int64_t &total, int64_t &available)
{
  int ret = OB_SUCCESS;
  total = available = 0;
  win32_error_ = 0;
  ULARGE_INTEGER free_bytes = {}, total_bytes = {}, unused = {};
  if (wide_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if (!GetDiskFreeSpaceExW(wide_, &free_bytes, &total_bytes, &unused)) {
    win32_error_ = GetLastError();
    ret = OB_IO_ERROR;
  } else if (free_bytes.QuadPart > INT64_MAX || total_bytes.QuadPart > INT64_MAX) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    total = static_cast<int64_t>(total_bytes.QuadPart);
    available = static_cast<int64_t>(free_bytes.QuadPart);
  }
  return ret;
}

WindowsDirectoryIterator::WindowsDirectoryIterator(ObIAllocator &allocator)
  : allocator_(allocator), directory_(allocator), find_(INVALID_HANDLE_VALUE),
    entry_(), first_(false), opened_(false), win32_error_(0)
{}

WindowsDirectoryIterator::~WindowsDirectoryIterator() { close(); }

void WindowsDirectoryIterator::close()
{
  if (find_ != INVALID_HANDLE_VALUE) { FindClose(find_); find_ = INVALID_HANDLE_VALUE; }
  first_ = opened_ = false;
}

int WindowsDirectoryIterator::open(const WindowsFilePath &directory)
{
  close();
  win32_error_ = 0;
  int ret = directory_.assign(directory.utf8());
  WIN32_FILE_ATTRIBUTE_DATA info = {};
  wchar_t *pattern = nullptr;
  if (ret != OB_SUCCESS) {
    win32_error_ = directory_.win32_error();
  } else if (OB_SUCCESS != (ret = directory_.get_info(info))) {
    win32_error_ = directory_.win32_error();
  } else if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    ret = OB_FILE_NOT_EXIST;
  } else if (nullptr == (pattern = static_cast<wchar_t *>(
      allocator_.alloc((directory_.length() + 7) * sizeof(wchar_t))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    int64_t length = directory_.length() + 4;
    memcpy(pattern, directory_.wide(), length * sizeof(wchar_t));
    if (pattern[length - 1] != L'\\') { pattern[length++] = L'\\'; }
    pattern[length++] = L'*';
    pattern[length] = L'\0';
    find_ = FindFirstFileW(pattern, &entry_);
    if (find_ == INVALID_HANDLE_VALUE) {
      const DWORD error = GetLastError();
      if (error != ERROR_FILE_NOT_FOUND) { win32_error_ = error; ret = OB_IO_ERROR; }
    } else {
      first_ = true;
    }
    opened_ = ret == OB_SUCCESS;
  }
  if (pattern != nullptr) { allocator_.free(pattern); }
  return ret;
}

int WindowsDirectoryIterator::next(WindowsFilePath &child, DWORD &attributes)
{
  int ret = OB_SUCCESS;
  win32_error_ = 0;
  attributes = 0;
  bool found = false;
  while (ret == OB_SUCCESS && !found) {
    if (!opened_) {
      ret = OB_NOT_INIT;
    } else if (find_ == INVALID_HANDLE_VALUE) {
      ret = OB_ITER_END;
    } else if (!first_ && !FindNextFileW(find_, &entry_)) {
      const DWORD error = GetLastError();
      if (error == ERROR_NO_MORE_FILES) {
        FindClose(find_);
        find_ = INVALID_HANDLE_VALUE;
        ret = OB_ITER_END;
      } else {
        win32_error_ = error;
        ret = OB_IO_ERROR;
      }
    } else {
      first_ = false;
      found = wcscmp(entry_.cFileName, L".") != 0 && wcscmp(entry_.cFileName, L"..") != 0;
    }
  }
  if (ret == OB_SUCCESS) {
    const int name_size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        entry_.cFileName, -1, nullptr, 0, nullptr, nullptr);
    char *joined = nullptr;
    const int64_t base_size = strlen(directory_.utf8());
    if (name_size == 0) {
      win32_error_ = GetLastError();
      ret = OB_INVALID_ARGUMENT;
    } else if (nullptr == (joined = static_cast<char *>(allocator_.alloc(base_size + 1 + name_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      memcpy(joined, directory_.utf8(), base_size);
      joined[base_size] = '\\';
      if (name_size != WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
          entry_.cFileName, -1, joined + base_size + 1, name_size, nullptr, nullptr)) {
        win32_error_ = GetLastError();
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_SUCCESS != (ret = child.assign(joined))) {
        win32_error_ = child.win32_error();
      } else {
        attributes = entry_.dwFileAttributes;
      }
    }
    if (joined != nullptr) { allocator_.free(joined); }
  }
  return ret;
}

int WindowsFilePath::remove_tree(bool temporary_only)
{
  struct Frame {
    explicit Frame(ObIAllocator &allocator)
      : path(allocator), iterator(allocator), parent(nullptr), remove_self(false) {}
    WindowsFilePath path;
    WindowsDirectoryIterator iterator;
    Frame *parent;
    bool remove_self;
  };
  int ret = OB_SUCCESS;
  WIN32_FILE_ATTRIBUTE_DATA info = {};
  Frame *top = nullptr;
  win32_error_ = 0;
  auto push = [&](const WindowsFilePath &path, bool remove_self) {
    int result = OB_SUCCESS;
    void *memory = allocator_.alloc(sizeof(Frame));
    if (memory == nullptr) {
      result = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      Frame *frame = new(memory) Frame(allocator_);
      if (OB_SUCCESS != (result = frame->path.assign(path.utf8()))) {
        win32_error_ = frame->path.win32_error();
      } else if (OB_SUCCESS != (result = frame->iterator.open(frame->path))) {
        win32_error_ = frame->iterator.win32_error();
      } else {
        frame->parent = top;
        frame->remove_self = remove_self;
        top = frame;
      }
      if (result != OB_SUCCESS) { frame->~Frame(); allocator_.free(frame); }
    }
    return result;
  };
  auto pop = [&]() {
    Frame *parent = top->parent;
    top->~Frame();
    allocator_.free(top);
    top = parent;
  };
  if (OB_SUCCESS != (ret = get_info(info))) {
  } else if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    ret = OB_FILE_NOT_EXIST;
  } else if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    if (!temporary_only) { ret = delete_directory(); }
  } else if (OB_SUCCESS != (ret = push(*this, !temporary_only))) {
  } else {
    WindowsFilePath child(allocator_);
    while (ret == OB_SUCCESS && top != nullptr) {
      DWORD attributes = 0;
      ret = top->iterator.next(child, attributes);
      if (ret == OB_ITER_END) {
        ret = OB_SUCCESS;
        if (top->remove_self) {
          ret = top->path.delete_directory();
          win32_error_ = top->path.win32_error();
        }
        pop();
      } else if (ret != OB_SUCCESS) {
        win32_error_ = top->iterator.win32_error();
      } else {
        const char *name = child.utf8();
        for (const char *cursor = name; *cursor != '\0'; ++cursor) {
          if (*cursor == '/' || *cursor == '\\') { name = cursor + 1; }
        }
        const bool remove_child = top->remove_self || strstr(name, ".tmp") != nullptr;
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
          ret = push(child, remove_child);
        } else if (remove_child) {
          ret = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? child.delete_directory() : child.delete_file();
          win32_error_ = child.win32_error();
        }
      }
    }
  }
  while (top != nullptr) { pop(); }
  return ret;
}
} // namespace common
} // namespace oceanbase
#endif // _WIN32
