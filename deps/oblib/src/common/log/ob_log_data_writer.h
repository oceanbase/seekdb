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

#ifndef OCEANBASE_COMMON_OB_LOG_DATA_WRITER_
#define OCEANBASE_COMMON_OB_LOG_DATA_WRITER_

#include "lib/ob_define.h"
#include "common/log/ob_log_cursor.h"

namespace oceanbase
{
namespace common
{

int myfallocate(int fd, int mode, off_t offset, off_t len);

class MinAvailFileIdGetter
{
public:
  MinAvailFileIdGetter() {}
  virtual ~MinAvailFileIdGetter() {}
  virtual int64_t get() = 0;
};

class ObLogDataWriter
{
public:
  static const int OPEN_FLAG = O_WRONLY | O_DIRECT;
  static const int OPEN_MODE = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  static const int CREATE_FLAG = OPEN_FLAG | O_CREAT;
  class AppendBuffer
  {
  public:
    static const int64_t DEFAULT_BUF_SIZE = 1 << 22;
    AppendBuffer();
    ~AppendBuffer();
    int flush(int fd);
  private:
    int64_t file_pos_;
    char *buf_;
    int64_t buf_end_;
    int64_t buf_limit_;
  };
public:
  ObLogDataWriter();
  ~ObLogDataWriter();
  inline int64_t get_file_size() const { return file_size_; }
protected:
  int reuse(const char *pool_file, const char *fname);
  const char *select_pool_file(char *fname, const int64_t limit);
private:
  AppendBuffer write_buffer_;
  const char *log_dir_;
  int64_t file_size_;
  ObLogCursor end_cursor_;
  int64_t log_sync_type_;
  int fd_;
  int64_t cur_file_id_;
  int64_t num_file_to_add_;
  int64_t min_file_id_;
  int64_t min_avail_file_id_;
  MinAvailFileIdGetter *min_avail_file_id_getter_;
};
} // end namespace common
} // end namespace oceanbase

#endif /* OCEANBASE_COMMON_OB_LOG_DATA_WRITER_ */
