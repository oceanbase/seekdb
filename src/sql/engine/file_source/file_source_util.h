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

#pragma once

#include "sql/engine/file_source/file_source_def.h"

#include <string>
#include <vector>

namespace oceanbase {
namespace sql {
namespace file_source {

/// Utilities shared by the file source layer: path handling, the
/// `secure_file_priv` check, file identity and glob discovery.

/// Normalize a path lexically: unify the separators, drop `.` components and
/// resolve `..` components. The result is absolute when `path` is absolute.
bool normalize_path(const std::string &path, std::string &result);

/// Make the path absolute using the current working directory.
bool absolute_path(const std::string &path, std::string &result);

/// Check the path against the `secure_file_priv` restriction.
///
/// The semantics follow the MySQL/OceanBase convention: an empty or `NULL`
/// value denies every access, otherwise the path has to be located below the
/// configured directory. Path traversal attempts (`..`) are rejected because
/// the path is normalized before the comparison.
bool check_secure_path(const std::string &secure_file_priv, const std::string &path, std::string &error);

/// Collect the size and modification time of a file.
bool get_file_signature(const std::string &path, FileSignature &signature);

/// Check whether the path exists and is a regular file.
bool is_regular_file(const std::string &path);

/// Return the lowercase extension of the path, without the dot.
///
/// The extension is the suffix of the last component of the path, an empty
/// string is returned when the path does not have an extension.
std::string file_extension(const std::string &path);

/// Detect the file format from the file extension.
///
/// Only the extensions of the supported formats are recognized, every other
/// extension is reported as `FileFormat::AUTO`.
FileFormat detect_format_by_extension(const std::string &path);

/// Expand a glob pattern (`*` and `?` are supported inside path components)
/// into the list of matching files. The result is sorted by path.
bool glob_files(const std::string &pattern, std::vector<std::string> &files);

/// Match a single path component against a pattern with `*` and `?`.
bool match_glob_pattern(const std::string &pattern, const std::string &text);

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
