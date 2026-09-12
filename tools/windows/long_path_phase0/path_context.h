// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Phase 0 component only. Production integration must use repository ownership
// and allocation conventions after the complete consumer inventory is reviewed.
#ifndef SEEKDB_PHASE0_PATH_CONTEXT_H_
#define SEEKDB_PHASE0_PATH_CONTEXT_H_

#include <windows.h>
#include <algorithm>
#include <climits>
#include <string>
#include <utility>
#include <vector>

namespace seekdb_phase0 {
struct PathResult {
  int code = 0;
  DWORD win32 = 0; // Only an immediately captured failing Win32 call supplies it.
  explicit operator bool() const { return code == 0; }
};
constexpr size_t kBaseUnits = 2048;
constexpr size_t kFileUnits = 4096;
inline PathResult invalid_path() { return {-4002, 0}; }
inline PathResult oversized_path() { return {-4019, 0}; }
inline PathResult failed_path_api() { return {-4000, GetLastError()}; }

inline PathResult path_to_wide(const std::string &input, std::wstring &output)
{
  if (input.empty() || input.find('\0') != std::string::npos) return invalid_path();
  if (input.size() > INT_MAX) return oversized_path();
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
      input.data(), static_cast<int>(input.size()), nullptr, 0);
  if (count == 0) return {-4002, GetLastError()};
  std::wstring value(count, L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
      static_cast<int>(input.size()), value.data(), count) != count) return failed_path_api();
  output.swap(value);
  return {};
}
inline PathResult path_to_utf8(const std::wstring &input, std::string &output)
{
  if (input.empty() || input.find(L'\0') != std::wstring::npos) return invalid_path();
  if (input.size() > INT_MAX) return oversized_path();
  const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
      static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
  if (count == 0) return {-4002, GetLastError()};
  std::string value(count, '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
      static_cast<int>(input.size()), value.data(), count, nullptr, nullptr) != count) {
    return failed_path_api();
  }
  output.swap(value);
  return {};
}
inline bool drive_letter(wchar_t c)
{
  return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}
inline bool drive_absolute(const std::wstring &value)
{
  return value.size() >= 3 && drive_letter(value[0]) && value[1] == L':' && value[2] == L'\\';
}
inline bool reserved_component(const std::wstring &component)
{
  std::wstring stem = component.substr(0, component.find(L'.'));
  // DOS device recognition also ignores spaces preceding the extension.
  while (!stem.empty() && stem.back() == L' ') stem.pop_back();
  for (auto &c : stem) if (c >= L'a' && c <= L'z') c -= L'a' - L'A';
  if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
      stem == L"CONIN$" || stem == L"CONOUT$") return true;
  if (stem.size() == 4 && (stem.substr(0, 3) == L"COM" || stem.substr(0, 3) == L"LPT")) {
    const wchar_t digit = stem[3];
    return (digit >= L'1' && digit <= L'9') || digit == L'\u00b9' || digit == L'\u00b2' || digit == L'\u00b3';
  }
  return false;
}
inline PathResult validate_components(const std::wstring &value, size_t start)
{
  while (start < value.size()) {
    const size_t end = value.find(L'\\', start);
    const std::wstring part = value.substr(start, end == std::wstring::npos ? end : end - start);
    if (part.size() > 255) return oversized_path();
    if (!part.empty() && part != L"." && part != L"..") {
      if (part.back() == L' ' || part.back() == L'.' || reserved_component(part)) return invalid_path();
      for (const wchar_t c : part) {
        if (c < 32 || c == L'<' || c == L'>' || c == L':' || c == L'"' ||
            c == L'|' || c == L'?' || c == L'*') return invalid_path();
      }
    }
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
  return {};
}
// Lexical resolution only: does not create directories or change process cwd.
inline PathResult normalize_absolute(const std::string &input, size_t limit, std::wstring &output)
{
  std::wstring raw;
  PathResult result = path_to_wide(input, raw);
  if (!result) return result;
  std::replace(raw.begin(), raw.end(), L'/', L'\\');
  if (raw.compare(0, 4, L"\\\\?\\") == 0) raw.erase(0, 4);
  if (!drive_absolute(raw)) return invalid_path(); // UNC/device/drive-relative are not database roots.
  if (raw.size() >= 32767) return oversized_path();
  result = validate_components(raw, 3);
  if (!result) return result; // Validate BEFORE Win32 can trim malformed names.
  const DWORD count = GetFullPathNameW(raw.c_str(), 0, nullptr, nullptr);
  if (!count) return failed_path_api();
  std::vector<wchar_t> buffer(count);
  const DWORD length = GetFullPathNameW(raw.c_str(), count, buffer.data(), nullptr);
  if (!length) return failed_path_api();
  if (length >= count) return oversized_path();
  std::wstring value(buffer.data(), length);
  while (value.size() > 3 && value.back() == L'\\') value.pop_back();
  if (!drive_absolute(value)) return invalid_path();
  if (value.size() > limit) return oversized_path();
  result = validate_components(value, 3);
  if (!result) return result;
  output.swap(value);
  return {};
}
inline std::wstring extended_path(const std::wstring &normalized)
{
  return L"\\\\?\\" + normalized; // Only accepts the successful normalized result.
}

class PathContext {
public:
  // Initialize once before consumers run. Failed initialization changes nothing.
  PathResult initialize(const std::string &absolute_base)
  {
    if (!base_.empty()) return invalid_path();
    std::wstring base;
    PathResult result = normalize_absolute(absolute_base, kBaseUnits, base);
    if (!result) return result;
    const DWORD count = GetCurrentDirectoryW(0, nullptr);
    if (!count) return failed_path_api();
    std::vector<wchar_t> buffer(count);
    const DWORD length = GetCurrentDirectoryW(count, buffer.data());
    if (!length) return failed_path_api();
    if (length >= count) return oversized_path();
    std::wstring original(buffer.data(), length);
    base_.swap(base);
    original_.swap(original);
    return {};
  }
  const std::wstring &base() const { return base_; }
  const std::wstring &original_cwd() const { return original_; }
  PathResult resolve_instance(const std::string &input, std::wstring &output) const
  {
    if (base_.empty()) return invalid_path();
    std::wstring value;
    PathResult result = path_to_wide(input, value);
    if (!result) return result;
    std::replace(value.begin(), value.end(), L'/', L'\\');
    // Existing absolute values are never joined twice. Reject ambiguous rooted
    // or drive-relative configuration here; CLI fixes those before consumers.
    if (drive_absolute(value) || value.compare(0, 4, L"\\\\?\\") == 0) {
      return normalize_absolute(input, kFileUnits, output);
    }
    if (value[0] == L'\\' || value.find(L':') != std::wstring::npos) return invalid_path();
    std::string joined;
    result = path_to_utf8(base_ + L"\\" + value, joined);
    return result ? normalize_absolute(joined, kFileUnits, output) : result;
  }
private:
  std::wstring base_;
  std::wstring original_;
};

// Only called while freezing CLI options, before workers or filesystem changes.
// Tilde expansion belongs to the existing parser and precedes this boundary.
inline PathResult resolve_startup_path(const std::string &input,
    const std::wstring &original_cwd, size_t limit, std::wstring &output)
{
  std::wstring raw;
  PathResult result = path_to_wide(input, raw);
  if (!result) return result;
  std::replace(raw.begin(), raw.end(), L'/', L'\\');
  if (drive_absolute(raw) || raw.compare(0, 4, L"\\\\?\\") == 0) {
    return normalize_absolute(input, limit, output);
  }
  if (!drive_absolute(original_cwd) || raw.compare(0, 2, L"\\\\") == 0) return invalid_path();
  if (raw.size() >= 32767) return oversized_path();
  const bool drive_relative = raw.size() >= 2 && drive_letter(raw[0]) && raw[1] == L':';
  result = validate_components(raw, drive_relative ? 2 : 0);
  if (!result) return result; // Do not let GetFullPathNameW trim invalid components.
  std::wstring absolute;
  if (drive_relative) {
    if (CompareStringOrdinal(raw.data(), 2, original_cwd.data(), 2, TRUE) == CSTR_EQUAL) {
      absolute = original_cwd + L"\\" + raw.substr(2);
    } else {
      // Preserve Windows' other-drive cwd semantics at this startup instant.
      // The resulting absolute value is owned and passed unchanged to children.
      const DWORD count = GetFullPathNameW(raw.c_str(), 0, nullptr, nullptr);
      if (!count) return failed_path_api();
      std::vector<wchar_t> buffer(count);
      const DWORD length = GetFullPathNameW(raw.c_str(), count, buffer.data(), nullptr);
      if (!length) return failed_path_api();
      if (length >= count) return oversized_path();
      absolute.assign(buffer.data(), length);
    }
  } else if (raw[0] == L'\\') {
    absolute = original_cwd.substr(0, 2) + raw;
  } else {
    absolute = original_cwd + L"\\" + raw;
  }
  std::string encoded;
  result = path_to_utf8(absolute, encoded);
  return result ? normalize_absolute(encoded, limit, output) : result;
}

class StartupPaths {
public:
  // Empty optional data/redo values retain the parser's "not specified" state.
  // All values are frozen transactionally: an invalid later option publishes
  // neither the earlier base nor any partially resolved configuration.
  PathResult initialize(const std::string &base, const std::string &data = {},
      const std::string &redo = {})
  {
    if (!instance_.base().empty()) return invalid_path();
    const DWORD count = GetCurrentDirectoryW(0, nullptr);
    if (!count) return failed_path_api();
    std::vector<wchar_t> buffer(count);
    const DWORD length = GetCurrentDirectoryW(count, buffer.data());
    if (!length) return failed_path_api();
    if (length >= count) return oversized_path();
    const std::wstring original(buffer.data(), length);
    std::wstring resolved_base, resolved_data, resolved_redo;
    PathResult result = resolve_startup_path(base.empty() ? "." : base, original, kBaseUnits, resolved_base);
    if (result && !data.empty()) result = resolve_startup_path(data, original, kFileUnits, resolved_data);
    if (result && !redo.empty()) result = resolve_startup_path(redo, original, kFileUnits, resolved_redo);
    if (!result) return result;
    std::string encoded;
    result = path_to_utf8(resolved_base, encoded);
    if (!result) return result;
    PathContext instance;
    result = instance.initialize(encoded);
    if (!result) return result;
    if (instance.original_cwd() != original) return invalid_path();
    instance_ = std::move(instance);
    data_.swap(resolved_data);
    redo_.swap(resolved_redo);
    return {};
  }
  const PathContext &instance() const { return instance_; }
  const std::wstring &data() const { return data_; }
  const std::wstring &redo() const { return redo_; }
private:
  PathContext instance_;
  std::wstring data_;
  std::wstring redo_;
};
// CreateProcessW command-line quoting for one CRT argument, including empty
// arguments and trailing backslashes. Option insertion belongs to the parser.
inline std::wstring quote_argument(const std::wstring &input)
{
  std::wstring result(1, L'"');
  size_t slashes = 0;
  for (const wchar_t c : input) {
    if (c == L'\\') { ++slashes; continue; }
    result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    slashes = 0;
    result.push_back(c);
  }
  result.append(slashes * 2, L'\\');
  result.push_back(L'"');
  return result;
}
} // namespace seekdb_phase0
#endif
