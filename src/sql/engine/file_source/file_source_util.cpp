/**
 * Copyright (c) 2026 OceanBase
 *
 * OceanBase is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * OceanBase is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with OceanBase. If not, see <http://www.gnu.org/licenses/>.
 */

#include "sql/engine/file_source/file_source_util.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#include <dirent.h>

namespace oceanbase {
namespace sql {
namespace file_source {

namespace {

const char *const kNullFilePriv = "NULL";

bool is_separator(char c) { return '/' == c || '\\' == c; }

#if defined(_WIN32)
/// Split the drive prefix ("C:") of a windows path from the rest of the path.
/// The drive prefix is only recognized on windows, on linux a colon is an
/// ordinary character of a file name.
bool split_drive_prefix(const std::string &path, std::string &drive, std::string &rest)
{
  drive.clear();
  rest = path;
  if (path.size() >= 2 && ':' == path[1]
      && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))) {
    drive = path.substr(0, 2);
    rest = path.substr(2);
  }
  return !drive.empty();
}
#endif

/// Check whether the text contains a wildcard character.
bool has_wildcard(const std::string &text)
{
  return text.find('*') != std::string::npos || text.find('?') != std::string::npos;
}

/// Case insensitive comparison against a lower case literal.
bool equals_ignore_case(const std::string &text, const char *literal)
{
  const size_t len = ::strlen(literal);
  if (text.size() != len) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    char a = text[i];
    if (a >= 'A' && a <= 'Z') {
      a = static_cast<char>(a - 'A' + 'a');
    }
    char b = literal[i];
    if (b >= 'A' && b <= 'Z') {
      b = static_cast<char>(b - 'A' + 'a');
    }
    if (a != b) {
      return false;
    }
  }
  return true;
}

void split_path(const std::string &path, std::vector<std::string> &parts, bool &absolute)
{
  absolute = !path.empty() && is_separator(path[0]);
  parts.clear();
  size_t i = 0;
  while (i < path.size()) {
    while (i < path.size() && is_separator(path[i])) {
      ++i;
    }
    size_t begin = i;
    while (i < path.size() && !is_separator(path[i])) {
      ++i;
    }
    if (i > begin) {
      parts.push_back(path.substr(begin, i - begin));
    }
  }
}

bool get_cwd(std::string &cwd)
{
  char buffer[4096];
#if defined(_WIN32)
  if (NULL == _getcwd(buffer, sizeof(buffer))) {
    return false;
  }
#else
  if (NULL == getcwd(buffer, sizeof(buffer))) {
    return false;
  }
#endif
  cwd = buffer;
  return true;
}

/// Read the entries of a directory, returns false when the directory cannot
/// be opened.
bool list_directory(const std::string &path, std::vector<std::string> &entries)
{
  entries.clear();
  DIR *dir = opendir(path.c_str());
  if (NULL == dir) {
    return false;
  }
  struct dirent *entry = NULL;
  while (NULL != (entry = readdir(dir))) {
    const std::string name = entry->d_name;
    if ("." != name && ".." != name) {
      entries.push_back(name);
    }
  }
  closedir(dir);
  return true;
}

/// Recursively expand the directory components of a pattern.
void expand_directory(const std::vector<std::string> &components,
                       size_t index,
                       const std::string &current,
                       std::vector<std::string> &results)
{
  if (index >= components.size()) {
    results.push_back(current);
    return;
  }
  const std::string &component = components[index];
  if (!has_wildcard(component)) {
    const std::string next = current + "/" + component;
    expand_directory(components, index + 1, next, results);
    return;
  }
  std::vector<std::string> entries;
  if (!list_directory(current, entries)) {
    return;
  }
  std::sort(entries.begin(), entries.end());
  for (size_t i = 0; i < entries.size(); ++i) {
    if (match_glob_pattern(component, entries[i])) {
      const std::string next = current + "/" + entries[i];
      expand_directory(components, index + 1, next, results);
    }
  }
}

}  // namespace

bool match_glob_pattern(const std::string &pattern, const std::string &text)
{
  size_t p = 0;
  size_t t = 0;
  size_t star = std::string::npos;
  size_t match = 0;
  while (t < text.size()) {
    if (p < pattern.size() && ('?' == pattern[p] || pattern[p] == text[t])) {
      ++p;
      ++t;
    } else if (p < pattern.size() && '*' == pattern[p]) {
      star = p++;
      match = t;
    } else if (star != std::string::npos) {
      p = star + 1;
      t = ++match;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && '*' == pattern[p]) {
    ++p;
  }
  return p == pattern.size();
}

bool normalize_path(const std::string &path, std::string &result)
{
  std::string drive;
  std::string body = path;
#if defined(_WIN32)
  split_drive_prefix(path, drive, body);
#endif
  std::vector<std::string> parts;
  bool absolute = false;
  split_path(body, parts, absolute);
  std::vector<std::string> stack;
  for (size_t i = 0; i < parts.size(); ++i) {
    const std::string &part = parts[i];
    if ("." == part) {
      continue;
    }
    if (".." == part) {
      if (!stack.empty() && ".." != stack.back()) {
        stack.pop_back();
        continue;
      }
      if (absolute) {
        continue;
      }
      stack.push_back(part);
      continue;
    }
    stack.push_back(part);
  }
  std::string normalized;
  if (absolute || !drive.empty()) {
    normalized = "/";
  }
  for (size_t i = 0; i < stack.size(); ++i) {
    if (i > 0) {
      normalized += "/";
    }
    normalized += stack[i];
  }
  if (!drive.empty()) {
    normalized = drive + normalized;
  }
  if (normalized.empty()) {
    normalized = ".";
  }
  result = normalized;
  return true;
}

bool absolute_path(const std::string &path, std::string &result)
{
  if (path.empty()) {
    return false;
  }
  if (is_separator(path[0])) {
    return normalize_path(path, result);
  }
#if defined(_WIN32)
  std::string drive;
  std::string body;
  if (split_drive_prefix(path, drive, body)) {
    // a windows path with a drive prefix is already absolute
    return normalize_path(path, result);
  }
#endif
  std::string cwd;
  if (!get_cwd(cwd)) {
    return false;
  }
  return normalize_path(cwd + "/" + path, result);
}

bool check_secure_path(const std::string &secure_file_priv, const std::string &path, std::string &error)
{
  if (secure_file_priv.empty() || equals_ignore_case(secure_file_priv, kNullFilePriv)) {
    error = "Access denied, please set suitable variable 'secure_file_priv' first";
    return false;
  }
  std::string normalized_priv;
  std::string normalized_path;
  if (!normalize_path(secure_file_priv, normalized_priv) || !normalize_path(path, normalized_path)) {
    error = "Access denied, invalid file path";
    return false;
  }
  if (!normalized_path.empty() && normalized_path[0] != '/') {
    std::string absolute;
    if (!absolute_path(path, absolute)) {
      error = "Access denied, invalid file path";
      return false;
    }
    normalized_path = absolute;
  }
  if (!normalized_priv.empty() && normalized_priv[0] != '/') {
    std::string absolute;
    if (!absolute_path(secure_file_priv, absolute)) {
      error = "Access denied, invalid secure_file_priv";
      return false;
    }
    normalized_priv = absolute;
  }
  // the root directory allows every absolute path
  if ("/" == normalized_priv) {
    return true;
  }
  if (0 == normalized_path.compare(0, normalized_priv.size(), normalized_priv)) {
    if (normalized_path.size() == normalized_priv.size()) {
      return true;
    }
    if ('/' == normalized_path[normalized_priv.size()]) {
      return true;
    }
  }
  error = "Access denied for file path, please check the 'secure_file_priv' setting";
  return false;
}

bool get_file_signature(const std::string &path, FileSignature &signature)
{
  struct stat st;
  if (0 != ::stat(path.c_str(), &st)) {
    return false;
  }
  signature.size = static_cast<int64_t>(st.st_size);
  signature.mtime_sec = static_cast<int64_t>(st.st_mtime);
#if defined(__APPLE__)
  signature.mtime_nsec = static_cast<int64_t>(st.st_mtimespec.tv_nsec);
#elif defined(_WIN32)
  signature.mtime_nsec = 0;
#else
  signature.mtime_nsec = static_cast<int64_t>(st.st_mtim.tv_nsec);
#endif
  return true;
}

bool is_regular_file(const std::string &path)
{
  struct stat st;
  if (0 != ::stat(path.c_str(), &st)) {
    return false;
  }
  return 0 != (st.st_mode & S_IFREG);
}

std::string file_extension(const std::string &path)
{
  const size_t slash = path.find_last_of('/');
  const size_t begin = (std::string::npos == slash) ? 0 : slash + 1;
  const size_t pos = path.find_last_of('.');
  if (std::string::npos == pos || pos < begin || pos + 1 >= path.size()) {
    return std::string();
  }
  std::string ext = path.substr(pos + 1);
  for (size_t i = 0; i < ext.size(); ++i) {
    ext[i] = static_cast<char>(::tolower(ext[i]));
  }
  return ext;
}

FileFormat detect_format_by_extension(const std::string &path)
{
  const std::string ext = file_extension(path);
  if (ext.empty()) {
    return FileFormat::AUTO;
  }
  if ("csv" == ext) {
    return FileFormat::CSV;
  }
  if ("json" == ext) {
    return FileFormat::JSON;
  }
  if ("jsonl" == ext || "ndjson" == ext) {
    return FileFormat::JSONL;
  }
  if ("parquet" == ext) {
    return FileFormat::PARQUET;
  }
  return FileFormat::AUTO;
}

bool glob_files(const std::string &pattern, std::vector<std::string> &files)
{
  files.clear();
  if (pattern.empty()) {
    return false;
  }
  std::string normalized;
  if (!normalize_path(pattern, normalized)) {
    return false;
  }
  std::string drive;
  std::string body = normalized;
#if defined(_WIN32)
  split_drive_prefix(normalized, drive, body);
#endif
  std::vector<std::string> parts;
  bool absolute = false;
  split_path(body, parts, absolute);
  if (parts.empty()) {
    return false;
  }
  const std::string file_pattern = parts.back();
  parts.pop_back();
  std::vector<std::string> dirs;
  const std::string root = drive.empty() ? (absolute ? "/" : ".") : (drive + "/");
  expand_directory(parts, 0, root, dirs);
  for (size_t i = 0; i < dirs.size(); ++i) {
    const std::string &dir = dirs[i];
    if (!has_wildcard(file_pattern)) {
      const std::string candidate = dir + "/" + file_pattern;
      if (is_regular_file(candidate)) {
        files.push_back(candidate);
      }
      continue;
    }
    std::vector<std::string> entries;
    if (!list_directory(dir, entries)) {
      continue;
    }
    for (size_t j = 0; j < entries.size(); ++j) {
      if (match_glob_pattern(file_pattern, entries[j])) {
        const std::string candidate = dir + "/" + entries[j];
        if (is_regular_file(candidate)) {
          files.push_back(candidate);
        }
      }
    }
  }
  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());
  return true;
}

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
