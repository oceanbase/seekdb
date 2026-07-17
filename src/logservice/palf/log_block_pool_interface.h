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

#ifndef OCEANBASE_LOGSERVICE_LOG_BLOCK_POOL_INTERFACE_
#define OCEANBASE_LOGSERVICE_LOG_BLOCK_POOL_INTERFACE_
#include <stdint.h>
#include "lib/file/file_directory_utils.h"        // FileDirectoryUtils
#include "log_define.h"                           // block_id_t
#include "log_block_header.h"                     // LogBlockHeader
namespace oceanbase
{
namespace palf
{

class ILogBlockPool
{
public:
  virtual int create_block_at(const palf::FileDesc &dir_fd,
                              const char *block_path,
                              const int64_t block_size) = 0;
  virtual int remove_block_at(const palf::FileDesc &dir_fd,
                              const char *block_path) = 0;
};

int is_block_used_for_palf(const int fd, const char *path, bool &result);
int remove_file_at(const char *dir, const char *path, ILogBlockPool *log_block_pool);
int remove_directory_rec(const char *path, ILogBlockPool *log_block_pool);
int remove_tmp_file_or_directory_at(const char *path, ILogBlockPool *log_block_pool);
} // namespace palf
} // namespace oceanbase
#endif
