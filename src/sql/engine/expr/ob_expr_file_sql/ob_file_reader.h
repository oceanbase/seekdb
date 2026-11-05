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

#ifndef OCEANBASE_SQL_OB_FILE_READER_H_
#define OCEANBASE_SQL_OB_FILE_READER_H_

#include "lib/string/ob_string.h"
#include "lib/alloc/ob_iallocator.h"

namespace oceanbase
{
namespace sql
{

class ObFileReader
{
public:
  // Read entire file content into memory
  static int read_file(const common::ObString& path,
                       common::ObIAllocator& allocator,
                       common::ObString& content);

  // Validate file path (security check)
  static int validate_path(const common::ObString& path);

  // Check file permissions
  static int check_file_permissions(const common::ObString& path);

  // Get file size
  static int get_file_size(const common::ObString& path, int64_t& size);

private:
  DISALLOW_COPY_AND_ASSIGN(ObFileReader);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_OB_FILE_READER_H_
