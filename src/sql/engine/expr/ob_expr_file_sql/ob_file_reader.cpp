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
#include "ob_file_reader.h"
#include <fstream>
#include <sys/stat.h>
#include "lib/utility/ob_macro_utils.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

int ObFileReader::read_file(const ObString& path,
                            ObIAllocator& allocator,
                            ObString& content)
{
  int ret = OB_SUCCESS;

#ifdef __linux__
  std::ifstream file(path.ptr(), std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    ret = OB_FILE_NOT_EXIST;
    LOG_WARN("Failed to open file", K(ret), K(path));
  } else {
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    char* buffer = static_cast<char*>(allocator.alloc(size + 1));
    if (OB_ISNULL(buffer)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("Failed to allocate memory for file content", K(ret), K(size));
    } else {
      if (file.read(buffer, size)) {
        buffer[size] = '\0';
        content.assign_ptr(buffer, static_cast<int32_t>(size));
      } else {
        ret = OB_IO_ERROR;
        LOG_WARN("Failed to read file content", K(ret), K(path));
      }
    }
    file.close();
  }
#else
  // Windows implementation would go here
  ret = OB_NOT_SUPPORTED;
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "file reading on Windows");
#endif

  return ret;
}

int ObFileReader::validate_path(const ObString& path)
{
  int ret = OB_SUCCESS;

  // Check for path traversal attacks
  if (path.find("..") != -1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Path contains '..' which is not allowed", K(path));
  }

  // Check for absolute paths (optional security measure)
  // Uncomment if you want to restrict to relative paths only
  // if (path[0] == '/' || (path.length() > 1 && path[1] == ':')) {
  //   ret = OB_INVALID_ARGUMENT;
  //   LOG_WARN("Absolute paths are not allowed", K(path));
  // }

  return ret;
}

int ObFileReader::check_file_permissions(const ObString& path)
{
  int ret = OB_SUCCESS;

#ifdef __linux__
  struct stat st;
  if (stat(path.ptr(), &st) != 0) {
    ret = OB_FILE_NOT_EXIST;
    LOG_WARN("File does not exist or cannot be accessed", K(ret), K(path));
  } else if (!S_ISREG(st.st_mode)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Path is not a regular file", K(path));
  } else if ((st.st_mode & S_IRUSR) == 0) {
    ret = OB_PRIV_CHECK_FAIL;
    LOG_WARN("File is not readable", K(path));
  }
#else
  // Windows implementation would use GetFileAttributes
  ret = OB_NOT_SUPPORTED;
#endif

  return ret;
}

int ObFileReader::get_file_size(const ObString& path, int64_t& size)
{
  int ret = OB_SUCCESS;

#ifdef __linux__
  struct stat st;
  if (stat(path.ptr(), &st) != 0) {
    ret = OB_FILE_NOT_EXIST;
    LOG_WARN("Failed to get file stats", K(ret), K(path));
  } else {
    size = st.st_size;
  }
#else
  ret = OB_NOT_SUPPORTED;
#endif

  return ret;
}

} // namespace sql
} // namespace oceanbase
