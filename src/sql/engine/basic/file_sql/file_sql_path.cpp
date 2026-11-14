/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "file_sql_path.h"

#include <dirent.h>
#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace oceanbase
{
namespace sql
{
namespace file_sql
{

namespace
{

// Split `path` into its directory part and its file name part. The directory
// part keeps its trailing separator so that it can be concatenated again.
void split_dir_and_name(const std::string &path, std::string &dir, std::string &name)
{
  const std::string::size_type pos = path.find_last_of('/');
  if (pos == std::string::npos) {
    dir = "./";
    name = path;
  } else {
    dir = path.substr(0, pos + 1);
    name = path.substr(pos + 1);
  }
}

// Resolve the directory of `path` with realpath(). The file name itself is
// kept verbatim because it may be a glob pattern.
bool canonicalize_dir(const std::string &path, std::string &canonical_dir)
{
  std::string dir;
  std::string name;
  split_dir_and_name(path, dir, name);
  char buf[PATH_MAX];
  if (NULL == ::realpath(dir.c_str(), buf)) {
    return false;
  }
  canonical_dir = buf;
  if (canonical_dir.empty() || canonical_dir[canonical_dir.size() - 1] != '/') {
    canonical_dir += '/';
  }
  canonical_dir += name;
  return true;
}

// Canonicalize a glob pattern. The part in front of the first component that
// contains a wildcard has to exist, it is resolved with realpath(). The rest
// of the pattern is kept verbatim so that the wildcard still matches.
bool canonicalize_glob_pattern(const std::string &pattern, std::string &canonical_pattern)
{
  std::string::size_type wildcard_pos = std::string::npos;
  std::string::size_type start = 0;
  while (start <= pattern.size()) {
    const std::string::size_type end = pattern.find('/', start);
    const std::string component = pattern.substr(start, (end == std::string::npos) ? std::string::npos : end - start);
    if (component.find_first_of("*?[") != std::string::npos) {
      wildcard_pos = start;
      break;
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  if (std::string::npos == wildcard_pos) {
    return canonicalize_dir(pattern, canonical_pattern);
  }
  std::string prefix = pattern.substr(0, wildcard_pos);
  std::string suffix = pattern.substr(wildcard_pos);
  std::string dir = prefix;
  while (!dir.empty() && '/' == dir[dir.size() - 1]) {
    dir.erase(dir.size() - 1);
  }
  if (dir.empty()) {
    dir = ".";
  }
  char buf[PATH_MAX];
  if (NULL == ::realpath(dir.c_str(), buf)) {
    return false;
  }
  canonical_pattern = buf;
  if (canonical_pattern.empty() || '/' != canonical_pattern[canonical_pattern.size() - 1]) {
    canonical_pattern += '/';
  }
  canonical_pattern += suffix;
  return true;
}

} // namespace

PathCode canonicalize_path(const std::string &path, std::string &canonical)
{
  if (path.empty()) {
    return PathCode::EMPTY_PATH;
  }
  char buf[PATH_MAX];
  if (NULL == ::realpath(path.c_str(), buf)) {
    return (ENOENT == errno) ? PathCode::NOT_FOUND : PathCode::IO_ERROR;
  }
  canonical = buf;
  struct stat st;
  if (0 != ::stat(canonical.c_str(), &st)) {
    return PathCode::IO_ERROR;
  }
  if (!S_ISREG(st.st_mode)) {
    return PathCode::NOT_REGULAR_FILE;
  }
  return PathCode::OK;
}

bool lexical_canonicalize_path(const std::string &path, std::string &canonical)
{
  if (path.empty()) {
    return false;
  }
  const bool absolute = ('/' == path[0]);
  std::vector<std::string> parts;
  std::string::size_type pos = 0;
  while (pos < path.size()) {
    const std::string::size_type next = path.find('/', pos);
    const std::string part = path.substr(pos, (next == std::string::npos) ? std::string::npos : next - pos);
    if (part.empty() || "." == part) {
      // skip
    } else if (".." == part) {
      if (!parts.empty() && ".." != parts.back()) {
        parts.pop_back();
      } else if (!absolute) {
        parts.push_back(part);
      }
    } else {
      parts.push_back(part);
    }
    if (next == std::string::npos) {
      break;
    }
    pos = next + 1;
  }
  std::string result;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (absolute || i > 0) {
      result += '/';
    }
    result += parts[i];
  }
  if (result.empty()) {
    result = absolute ? "/" : ".";
  }
  canonical = result;
  return true;
}

bool path_is_under(const std::string &secure_dir, const std::string &canonical_path)
{
  if (secure_dir.empty() || canonical_path.empty()) {
    return false;
  }
  std::string dir = secure_dir;
  while (dir.size() > 1 && '/' == dir[dir.size() - 1]) {
    dir.erase(dir.size() - 1);
  }
  if (canonical_path == dir) {
    return true;
  }
  if ("/" == dir) {
    // The root directory holds every path.
    return true;
  }
  if (canonical_path.size() <= dir.size()) {
    return false;
  }
  if (0 != canonical_path.compare(0, dir.size(), dir)) {
    return false;
  }
  // The boundary must be a separator, otherwise "/tmp/secure_evil" would be
  // accepted for the directory "/tmp/secure".
  return '/' == canonical_path[dir.size()];
}

PathCode check_secure_path(const std::string &secure_file_priv,
                            const std::string &path,
                            std::string &canonical)
{
  // A path is only published when it passed the check, a failed check must
  // never leave a usable path behind.
  canonical.clear();
  if (path.empty()) {
    return PathCode::EMPTY_PATH;
  }
  if (secure_file_priv.empty() || "NULL" == secure_file_priv) {
    return PathCode::ACCESS_DENIED;
  }
  std::string secure_dir;
  if (!canonicalize_dir(secure_file_priv, secure_dir)) {
    // An unusable secure_file_priv must not open the whole file system.
    return PathCode::ACCESS_DENIED;
  }
  std::string resolved;
  const PathCode code = canonicalize_path(path, resolved);
  if (PathCode::OK != code) {
    return code;
  }
  if (!path_is_under(secure_dir, resolved)) {
    return PathCode::ACCESS_DENIED;
  }
  canonical = resolved;
  return PathCode::OK;
}

PathCode expand_glob_pattern(const std::string &pattern,
                              const std::string &secure_dir,
                              std::vector<std::string> &files)
{
  files.clear();
  if (pattern.empty()) {
    return PathCode::EMPTY_PATH;
  }
  std::string canonical_pattern;
  if (!canonicalize_glob_pattern(pattern, canonical_pattern)) {
    return PathCode::NOT_FOUND;
  }
  glob_t glob_result;
  memset(&glob_result, 0, sizeof(glob_result));
  const int rc = ::glob(canonical_pattern.c_str(), GLOB_NOSORT, nullptr, &glob_result);
  if (0 != rc) {
    if (GLOB_NOMATCH == rc) {
      return PathCode::NOT_FOUND;
    }
    return PathCode::IO_ERROR;
  }
  PathCode result = PathCode::OK;
  for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
    std::string canonical;
    const PathCode code = canonicalize_path(glob_result.gl_pathv[i], canonical);
    if (PathCode::OK != code) {
      result = code;
      break;
    }
    if (!path_is_under(secure_dir, canonical)) {
      result = PathCode::ACCESS_DENIED;
      break;
    }
    files.push_back(canonical);
  }
  globfree(&glob_result);
  if (PathCode::OK != result) {
    files.clear();
  }
  return result;
}

const char *path_code_message(PathCode code)
{
  switch (code) {
  case PathCode::OK:
    return "ok";
  case PathCode::EMPTY_PATH:
    return "the file path is empty";
  case PathCode::NOT_FOUND:
    return "the file does not exist";
  case PathCode::NOT_REGULAR_FILE:
    return "the path is not a regular file";
  case PathCode::ACCESS_DENIED:
    return "access denied, the path is outside of secure_file_priv";
  case PathCode::IO_ERROR:
    return "the file cannot be read";
  default:
    return "unknown path error";
  }
}

} // namespace file_sql
} // namespace sql
} // namespace oceanbase
