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

#ifndef OCEANBASE_PALF_LOG_CACHE_
#define OCEANBASE_PALF_LOG_CACHE_

#include <cstdint>                                       // int64_t
#include "log_reader_utils.h"                            // ReadBuf
#include "lsn.h"
#include "log_storage_interface.h"                       // LogIteratorInfo

namespace oceanbase
{
namespace palf
{
class LSN;
class IPalfHandleImpl;

class LogHotCache
{
public:
  LogHotCache();
  ~LogHotCache();
  void destroy();
  void reset();
  int init(IPalfHandleImpl *palf_handle_impl);
  int read(const LSN &read_begin_lsn,
           const int64_t in_read_size,
           char *buf,
           int64_t &out_read_size) const;
private:
  IPalfHandleImpl *palf_handle_impl_;
  mutable int64_t read_size_;
  mutable int64_t hit_count_;
  mutable int64_t read_count_;
  mutable int64_t last_print_time_;
  bool is_inited_;
};

class LogCache
{
public:
  LogCache();
  ~LogCache();
  void destroy();
  int init(IPalfHandleImpl *palf_handle_impl);
  bool is_inited() const;           
  int read(const LSN &lsn,
           const int64_t in_read_size,
           ReadBuf &read_buf,
           int64_t &out_read_size,
           LogIOContext &io_ctx);
  TO_STRING_KV(K(is_inited_));
private:
  int read_hot_cache_(const LSN &read_begin_lsn,
                      const int64_t in_read_size,
                      char *buf,
                      int64_t &out_read_size);
private:
  LogHotCache hot_cache_;
  bool is_inited_;
};

} // end namespace palf
} // end namespace oceanbase

#endif // OCEANBASE_LOGSERVICE_LOG_CACHE_
