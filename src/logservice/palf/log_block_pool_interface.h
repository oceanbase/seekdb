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
#include "rust/file_io.h"
#include "share/log/palf/log_define.h"                           // block_id_t
#include "log_block_header.h"                     // LogBlockHeader
namespace oceanbase
{
namespace palf
{

class ILogBlockPool
{
public:
  virtual int create_and_publish_block_at(const file_io::DirectoryHandle &directory,
                              const char *temporary_name, const char *block_path,
                              const int64_t block_size) = 0;
  virtual int remove_block_at(const file_io::DirectoryHandle &directory,
                              const char *block_path, bool required = false) = 0;
  virtual int rename_block_at(const file_io::DirectoryHandle &directory, const char *old_name, const char *new_name) = 0;
  virtual int create_directory_at(const file_io::DirectoryHandle &directory, const char *name, uint32_t mode) = 0;
  virtual int rename_directory_at(const file_io::DirectoryHandle &directory, const char *old_name, const char *new_name) = 0;
  virtual int remove_directory_at(const file_io::DirectoryHandle &directory, const char *name) = 0;
  virtual int get_disk_usage(int64_t &bytes) = 0;
  virtual int set_capacity(int64_t bytes) = 0;
};

int remove_directory_rec(const char *path, ILogBlockPool *log_block_pool);
int remove_tmp_file_or_directory_at(const char *path, ILogBlockPool *log_block_pool);
int remove_tmp_file_or_directory_at(const file_io::DirectoryHandle &directory, const char *path, ILogBlockPool *log_block_pool);
int delete_directory_rec(const char *path);
int delete_tmp_file_or_directory_at(const char *path);
} // namespace palf
} // namespace oceanbase
#endif
