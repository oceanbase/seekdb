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
#pragma once

#include <string>
#include <vector>

namespace oceanbase
{
namespace sql
{
namespace file_sql
{

// Outcome of a path check. The SQL layer maps these codes to seekdb errors,
// the file modules must never open a path on their own.
enum class PathCode
{
  OK,
  EMPTY_PATH,
  NOT_FOUND,
  NOT_REGULAR_FILE,
  ACCESS_DENIED,
  IO_ERROR,
};

// Resolve `path` into an absolute canonical path. Symbolic links are
// resolved, so a link inside the allowed directory that points outside of it
// is reported by its target and is rejected by check_secure_path().
// A path that does not exist is reported as NOT_FOUND.
PathCode canonicalize_path(const std::string &path, std::string &canonical);

// Resolve `path` lexically, without touching the file system. Used for glob
// patterns, whose matches do not exist yet at check time.
bool lexical_canonicalize_path(const std::string &path, std::string &canonical);

// True when `canonical_path` is `secure_dir` itself or is located below it.
// Both arguments must be canonical absolute paths.
bool path_is_under(const std::string &secure_dir, const std::string &canonical_path);

// The single entry point used by the SQL layer. It canonicalizes `path` and
// verifies that the result stays inside `secure_file_priv`. `canonical` is
// filled with the path that may be handed to the file modules.
PathCode check_secure_path(const std::string &secure_file_priv,
                            const std::string &path,
                            std::string &canonical);

// Expand a glob pattern into the matching files. Every match is canonicalized
// and must stay inside `secure_dir`, otherwise the whole expansion fails.
PathCode expand_glob_pattern(const std::string &pattern,
                              const std::string &secure_dir,
                              std::vector<std::string> &files);

const char *path_code_message(PathCode code);

} // namespace file_sql
} // namespace sql
} // namespace oceanbase
