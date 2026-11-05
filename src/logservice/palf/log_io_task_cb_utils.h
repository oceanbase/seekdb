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

#ifndef LOGSERVICE_LOG_IO_TASK_CB_UTILS_
#define LOGSERVICE_LOG_IO_TASK_CB_UTILS_
#include "lib/ob_define.h"
#include "lib/oblog/ob_log_print_kv.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"               // TO_STRING_KV
#include "log_group_entry_header.h"
#include "share/scn.h"
#include "share/log/palf/lsn.h"
#include "share/log/palf/palf_base_info.h"

namespace oceanbase
{
namespace palf
{
struct FlushLogCbCtx
{
  FlushLogCbCtx();
  ~FlushLogCbCtx();
  bool is_valid() const { return true == lsn_.is_valid() && true == scn_.is_valid(); }
  void reset();
  FlushLogCbCtx &operator=(const FlushLogCbCtx &flush_log_cb_ctx);
  TO_STRING_KV(K_(log_id), K_(scn), K_(lsn), K_(total_len), K_(begin_ts));
  int64_t log_id_;
  share::SCN scn_;
  LSN lsn_;
  int64_t total_len_;
  int64_t begin_ts_;
};

struct TruncatePrefixBlocksCbCtx {
  TruncatePrefixBlocksCbCtx(const LSN &lsn);
  TruncatePrefixBlocksCbCtx();
  ~TruncatePrefixBlocksCbCtx();
  bool is_valid() const { return true == lsn_.is_valid();}
  void reset();
  TruncatePrefixBlocksCbCtx& operator=(const TruncatePrefixBlocksCbCtx& truncate_prefix_blocks_ctx);
  TO_STRING_KV(K_(lsn));
  LSN lsn_;
};

}
}

#endif
