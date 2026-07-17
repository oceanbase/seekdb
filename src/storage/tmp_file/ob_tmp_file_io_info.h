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

#ifndef OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_IO_INFO_H_
#define OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_IO_INFO_H_

#include "storage/tmp_file/ob_tmp_file_global.h"
#include "share/io/ob_io_define.h"

namespace oceanbase
{
namespace tmp_file
{
struct ObTmpFileIOInfo final
{
  ObTmpFileIOInfo();
  ~ObTmpFileIOInfo();
  void reset();
  bool is_valid() const;
  TO_STRING_KV(K(fd_), K(dir_id_), KP(buf_), K(size_), K(disable_page_cache_), K(disable_block_cache_),
               K(prefetch_), K(io_timeout_ms_), K(io_desc_));

  int64_t fd_;
  int64_t dir_id_;
  char *buf_;
  int64_t size_;
  bool disable_page_cache_;
  bool disable_block_cache_;
  bool prefetch_;
  common::ObIOFlag io_desc_;
  int64_t io_timeout_ms_;
};

}  // end namespace tmp_file
}  // end namespace oceanbase
#endif // OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_IO_INFO_H_
