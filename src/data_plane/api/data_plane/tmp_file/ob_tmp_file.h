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

#ifndef OCEANBASE_DATA_PLANE_API_TMP_FILE_OB_TMP_FILE_H_
#define OCEANBASE_DATA_PLANE_API_TMP_FILE_OB_TMP_FILE_H_

#include <cstdint>
#include "lib/utility/ob_print_utils.h"
#include "share/io/ob_io_define.h"

namespace oceanbase
{
namespace data_plane
{

// Query-facing value object.  The data-plane implementation translates this
// DTO to its own tmp-file request type at the boundary.
struct ObTmpFileIOInfo final
{
  ObTmpFileIOInfo();
  ~ObTmpFileIOInfo() = default;
  void reset();
  bool is_valid() const;

  TO_STRING_KV(K(fd_), K(dir_id_), KP(buf_), K(size_),
               K(disable_page_cache_), K(disable_block_cache_), K(prefetch_),
               K(io_timeout_ms_), K(io_desc_));

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

class ObTmpFileAccess;

// Stable query-side handle.  Its asynchronous state remains owned by the
// tmp-file implementation and is deliberately hidden behind impl_.
class ObTmpFileIOHandle final
{
public:
  ObTmpFileIOHandle();
  ~ObTmpFileIOHandle();
  void reset();
  int wait();
  bool is_valid() const;
  char *get_buffer();
  int64_t get_done_size() const;
  int64_t get_buf_size() const;
  bool is_finished() const;

  TO_STRING_KV(KP_(impl));

private:
  friend class ObTmpFileAccess;
  ObTmpFileIOHandle(const ObTmpFileIOHandle &) = delete;
  ObTmpFileIOHandle &operator=(const ObTmpFileIOHandle &) = delete;
  void *impl_;
};

int tmp_file_alloc_dir(int64_t &dir_id);
int tmp_file_open(int64_t &fd, int64_t dir_id, const char *label = nullptr);
int tmp_file_remove(int64_t fd);
int tmp_file_aio_read(const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle);
int tmp_file_aio_pread(const ObTmpFileIOInfo &io_info,
                       int64_t offset,
                       ObTmpFileIOHandle &io_handle);
int tmp_file_pread(const ObTmpFileIOInfo &io_info,
                   int64_t offset,
                   ObTmpFileIOHandle &io_handle);
int tmp_file_aio_write(const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle);
int tmp_file_write(const ObTmpFileIOInfo &io_info);
int tmp_file_truncate(int64_t fd, int64_t offset);
int tmp_file_seal(int64_t fd);
int tmp_file_get_size(int64_t fd, int64_t &file_size);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TMP_FILE_OB_TMP_FILE_H_
