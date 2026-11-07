/*
 * Copyright (c) 2025 OceanBase.
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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/ob_glob_reader.h"

#include <dirent.h>
#include <glob.h>
#include <string.h>
#include <sys/stat.h>

namespace oceanbase
{
using namespace common;
namespace sql
{

namespace
{

/**
 * @brief Split a pattern into the components that are separated by a slash.
 */
void split_pattern(const char *pattern, std::vector<std::string> &components)
{
  const char *begin = pattern;
  const char *cursor = pattern;
  while (true) {
    if ('/' == *cursor || '\0' == *cursor) {
      if (cursor > begin) {
        components.push_back(std::string(begin, cursor - begin));
      }
      if ('\0' == *cursor) {
        break;
      }
      begin = cursor + 1;
    }
    ++cursor;
  }
}

/**
 * @brief The directories of a directory, the "." and the ".." are skipped.
 */
int list_dirs(const std::string &dir, std::vector<std::string> &dirs)
{
  int ret = OB_SUCCESS;
  DIR *handle = opendir(dir.c_str());
  if (NULL == handle) {
    ret = OB_FILE_NOT_EXIST;
  } else {
    struct dirent *entry = NULL;
    while (NULL != (entry = readdir(handle))) {
      const char *name = entry->d_name;
      if (0 == strcmp(name, ".") || 0 == strcmp(name, "..")) {
        continue;
      }
      const std::string path = dir + "/" + name;
      struct stat file_stat;
      if (0 == ::stat(path.c_str(), &file_stat) && S_ISDIR(file_stat.st_mode)) {
        dirs.push_back(path);
      }
    }
    closedir(handle);
  }
  return ret;
}

/**
 * @brief The entries of a directory that match a pattern.
 *
 * The pattern is the one of the shell, the "glob()" function of the platform
 * does the matching.
 */
int list_matches(const std::string &dir,
                 const std::string &pattern,
                 std::vector<std::string> &matches)
{
  int ret = OB_SUCCESS;
  const std::string full_pattern = dir + "/" + pattern;
  glob_t glob_result;
  MEMSET(&glob_result, 0, sizeof(glob_result));
  const int glob_ret = ::glob(full_pattern.c_str(), GLOB_NOSORT, NULL, &glob_result);
  if (0 != glob_ret) {
    // no match is not an error, the pattern simply matches nothing
    globfree(&glob_result);
  } else {
    for (int64_t i = 0; i < static_cast<int64_t>(glob_result.gl_pathc); ++i) {
      matches.push_back(std::string(glob_result.gl_pathv[i]));
    }
    globfree(&glob_result);
  }
  return ret;
}

} // end anonymous namespace

ObGlobFileScanReader::ObGlobFileScanReader(const ObFileScanOptions &options,
                                           const common::ObIArray<ObFileColumnDef> &columns)
  : options_(options),
    columns_(),
    perm_alloc_(),
    files_(),
    file_idx_(0),
    is_opened_(false)
{
  columns_.reserve(columns.count());
  for (int64_t i = 0; i < columns.count(); ++i) {
    columns_.push_back(columns.at(i));
  }
}

ObGlobFileScanReader::~ObGlobFileScanReader()
{
}

int ObGlobFileScanReader::open()
{
  int ret = OB_SUCCESS;
  file_idx_ = 0;
  files_.clear();
  if (OB_ISNULL(options_.path_.ptr()) || options_.path_.length() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the path of the glob pattern is empty", K(ret));
  } else if (OB_FAIL(expand_pattern_(options_.path_.ptr(), files_))) {
    LOG_WARN("cannot expand the glob pattern", K(ret), K(options_.path_));
  } else {
    is_opened_ = true;
  }
  return ret;
}

int ObGlobFileScanReader::expand_pattern_(const char *pattern, std::vector<std::string> &files)
{
  int ret = OB_SUCCESS;
  std::vector<std::string> components;
  split_pattern(pattern, components);
  if (components.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the glob pattern is empty", K(ret));
  } else {
    std::string root = "/";
    const bool is_absolute = ('/' == pattern[0]);
    ret = expand_dir_(root, components, 0, files);
    if (OB_SUCC(ret) && !is_absolute) {
      // the pattern is relative to the working directory of the process
      for (int64_t i = 0; i < static_cast<int64_t>(files.size()); ++i) {
        files[i] = files[i].substr(1);
      }
    }
  }
  return ret;
}

int ObGlobFileScanReader::expand_dir_(const std::string &dir,
                                      const std::vector<std::string> &components,
                                      const int64_t index,
                                      std::vector<std::string> &files)
{
  int ret = OB_SUCCESS;
  if (index >= static_cast<int64_t>(components.size())) {
    struct stat file_stat;
    MEMSET(&file_stat, 0, sizeof(file_stat));
    if (0 != ::stat(dir.c_str(), &file_stat)) {
      // the entry does not exist any more, it is skipped
    } else if (!S_ISDIR(file_stat.st_mode)) {
      // only the files are returned, the directories are not
      files.push_back(dir);
    }
  } else {
    const std::string &component = components[static_cast<size_t>(index)];
    if ("**" == component) {
      // the "**" matches zero or more directory levels
      if (OB_FAIL(expand_dir_(dir, components, index + 1, files))) {
      } else {
        std::vector<std::string> dirs;
        if (OB_SUCC(list_dirs(dir, dirs))) {
          for (int64_t i = 0; OB_SUCC(ret) && i < static_cast<int64_t>(dirs.size()); ++i) {
            if (OB_FAIL(expand_dir_(dirs[static_cast<size_t>(i)], components, index, files))) {
            }
          }
        }
      }
    } else {
      std::vector<std::string> matches;
      if (OB_FAIL(list_matches(dir, component, matches))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < static_cast<int64_t>(matches.size()); ++i) {
          if (OB_FAIL(expand_dir_(matches[static_cast<size_t>(i)], components, index + 1, files))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObGlobFileScanReader::next_row()
{
  int ret = OB_SUCCESS;
  if (!is_opened_) {
    ret = OB_NOT_INIT;
  } else if (file_idx_ >= static_cast<int64_t>(files_.size())) {
    ret = OB_ITER_END;
  } else {
    const std::string &file = files_[static_cast<size_t>(file_idx_)];
    ++file_idx_;
    char *buf = NULL;
    if (OB_ISNULL(buf = static_cast<char *>(perm_alloc_.alloc(file.size() + 1)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(buf, file.c_str(), file.size());
      buf[file.size()] = '\0';
      ObString path(static_cast<int32_t>(file.size()), buf);
      ObObj value;
      value.set_varchar(path);
      value.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
      row_values_.reset();
      if (OB_FAIL(row_values_.push_back(value))) {
        LOG_WARN("cannot push the value of the row", K(ret));
      }
    }
  }
  return ret;
}

int ObGlobFileScanReader::close()
{
  is_opened_ = false;
  file_idx_ = 0;
  files_.clear();
  return OB_SUCCESS;
}

int ObGlobFileScanReader::infer_schema(common::ObIAllocator &allocator,
                                       common::ObIArray<ObFileColumnDef> &columns)
{
  int ret = OB_SUCCESS;
  ObFileColumnDef column;
  char *buf = NULL;
  if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(5)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(buf, "file", 5);
    column.name_.assign_ptr(buf, 4);
    column.type_ = ObVarcharType;
    column.nullable_ = false;
    column.precision_ = 1024;
    column.scale_ = 0;
    if (OB_FAIL(columns.push_back(column))) {
      LOG_WARN("cannot push the column of the glob reader", K(ret));
    }
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
