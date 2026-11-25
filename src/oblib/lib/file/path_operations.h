/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace oceanbase
{
namespace common
{
class ObSqlString;
namespace path_operations
{
int fsync_dir(const char *path);
int is_exists(const char *path, bool &result);
int is_directory(const char *path, bool &result);
int create_directory(const char *path);
int create_full_path(const char *path);
int delete_file(const char *path);
int delete_directory(const char *path);
int get_file_size(const char *path, int64_t &size);
int symlink(const char *target, const char *link);
int unlink_symlink(const char *path);
int get_disk_space(const char *path, int64_t &total, int64_t &available);
int resolve_path(const char *path, char *output, size_t capacity);
int to_absolute_path(ObSqlString &path);
}
}
}
