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

#ifndef OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_IO_DEFINE_H_
#define OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_IO_DEFINE_H_

#include "storage/tmp_file/ob_shared_nothing_tmp_file.h"
#include "storage/tmp_file/ob_tmp_file_io_ctx.h"

namespace oceanbase
{
namespace tmp_file
{
class ObTmpFileIOInfo;

class ObTmpFileIOHandle final
{
public:
  ObTmpFileIOHandle();
  ~ObTmpFileIOHandle();
  int init_write(const uint64_t tenant_id, const ObTmpFileIOInfo &io_info);
  int init_read(const uint64_t tenant_id, const ObTmpFileIOInfo &io_info);
  int init_pread(const uint64_t tenant_id, const ObTmpFileIOInfo &io_info, const int64_t read_offset);
  int wait();
  void reset();
  bool is_valid() const;

  TO_STRING_KV(K(is_inited_), K(tenant_id_), K(fd_), K(ctx_),
               KP(buf_), K(update_offset_in_file_),
               K(buf_size_), K(done_size_),
               K(read_offset_in_file_));
public:
  OB_INLINE char *get_buffer() { return buf_; }
  OB_INLINE int64_t get_done_size() const { return done_size_; }
  OB_INLINE int64_t get_buf_size() const { return buf_size_; }
  OB_INLINE ObTmpFileIOCtx &get_io_ctx() { return ctx_; }
  OB_INLINE bool is_finished() const { return done_size_ == buf_size_; }
private:
  int handle_finished_ctx_(ObTmpFileIOCtx &ctx);

private:
  bool is_inited_;
  uint64_t tenant_id_;
  int64_t fd_;
  ObTmpFileIOCtx ctx_;
  char *buf_;
  bool update_offset_in_file_;
  int64_t buf_size_; // excepted total read or write size
  int64_t done_size_;   // has finished read or write size
  int64_t read_offset_in_file_; // records the beginning read offset for current read ctx

  DISALLOW_COPY_AND_ASSIGN(ObTmpFileIOHandle);
};

}  // end namespace tmp_file
}  // end namespace oceanbase
#endif // OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_IO_DEFINE_H_
