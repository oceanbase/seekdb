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

#pragma once
// Isolated SEEK-1243 diagnostic: value-only identity, never a scheduler/TLS hook.
#include <cstdint>

#if defined(_WIN32) && !defined(OB_SEEK1243_DROP_DIAGNOSTIC)
#define OB_SEEK1243_DROP_DIAGNOSTIC 1
#endif

namespace oceanbase { namespace lib {
struct Seek1243DropKey {
  bool active = false;
  bool pending_query_entry = false;
  bool request_key_available = false;
  uint64_t request_generation = 0;
  int64_t request_recv_ts = 0;
  uint64_t request_trace[4] = {0, 0, 0, 0};
  uint64_t trace[4] = {0, 0, 0, 0};
  uint64_t session_id = 0;
  int64_t inner_tx_id = 0;
  int64_t observed_inner_tx_id = 0;
  uint64_t drop_table_id = 0;
  int64_t drop_table_type = -1;
  uint64_t stage_table_id = 0;
  int64_t stage_table_type = -1;
  uint64_t span = 0;
  int64_t span_owner_tid = 0;
  uint64_t parent_span = 0;
  int64_t parent_span_owner_tid = 0;
  bool execution_id_valid = false;
  int64_t execution_id = 0;
  int64_t retry_cnt = -1;
  int64_t ctx_abs_deadline_us = 0;
  int64_t query_abs_timeout_us = 0;
  bool query_deadline_selected = false;
  int64_t worker_timeout_ts = 0;
  void copy_request(const Seek1243DropKey &from)
  {
    request_key_available = from.request_key_available;
    request_generation = from.request_generation;
    request_recv_ts = from.request_recv_ts;
    for (int i = 0; i < 4; ++i) { request_trace[i] = from.request_trace[i]; }
  }
};
}} // namespace oceanbase::lib
