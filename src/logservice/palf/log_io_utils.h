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

#ifndef OCEANBASE_LOGSERVICE_LOG_IO_UTILS_
#define OCEANBASE_LOGSERVICE_LOG_IO_UTILS_
#include <string>
#include "rust/file_io.h"
#include "lib/string/ob_string.h"
#include "share/log/palf/log_define.h"
namespace oceanbase
{
namespace palf
{

class ObBaseDirFunctor
{
public:
  virtual int func(const common::ObString &name) = 0;
};

int scan_dir(const char *dir_name, ObBaseDirFunctor &functor);

int scan_dir(const file_io::DirectoryHandle &directory, const char *path, ObBaseDirFunctor &functor);
int directory_error(const file_io::FsError &error, const char *path = nullptr);
int next_directory_entry(const file_io::DirectoryHandle &directory, const char *path,
                         file_io::DirectoryCursor &cursor,
                         file_io::DirectoryStep &entry,
                         file_io::ObservationResult &observation);
int open_parent_directory(const char *path, file_io::DirectoryHandle &parent, std::string &leaf);
int entry_path(const char *parent, const common::ObString &name, char *output, size_t capacity);

int parse_block_name(const common::ObString &name, block_id_t &id);

class TrimLogDirectoryFunctor : public ObBaseDirFunctor
{
public:
  TrimLogDirectoryFunctor(const char *dir, ILogBlockPool *log_block_pool)
    : dir_(dir),
      min_block_id_(LOG_INVALID_BLOCK_ID),
      max_block_id_(LOG_INVALID_BLOCK_ID),
      log_block_pool_(log_block_pool)
  {
  }
  virtual ~TrimLogDirectoryFunctor() = default;

  int func(const common::ObString &name) override final;
  block_id_t get_min_block_id() const { return min_block_id_; }
  block_id_t get_max_block_id() const { return max_block_id_; }
private:
  const char *dir_;
  block_id_t min_block_id_;
  block_id_t max_block_id_;
  ILogBlockPool *log_block_pool_;

  DISALLOW_COPY_AND_ASSIGN(TrimLogDirectoryFunctor);
};

} // end namespace palf
} // end namespace oceanbase
#endif
