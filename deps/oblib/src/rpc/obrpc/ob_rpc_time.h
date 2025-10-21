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

#ifndef OCEANBASE_RPC_OBRPC_OB_RPC_TIME_
#define OCEANBASE_RPC_OBRPC_OB_RPC_TIME_
#include <cstdint>

namespace oceanbase
{
namespace obrpc
{
struct ObRpcCostTime
{
public:
  static const uint8_t RPC_COST_TIME_SIZE = 40;
public:
  ObRpcCostTime() { memset(this, 0, sizeof(*this)); }
  ~ObRpcCostTime() {}
  static inline int64_t get_encoded_size() { return RPC_COST_TIME_SIZE; }

  int32_t len_;
  int32_t arrival_push_diff_;
  int32_t push_pop_diff_;
  int32_t pop_process_start_diff_;
  int32_t process_start_end_diff_;
  int32_t process_end_response_diff_;
  uint64_t packet_id_;
  int64_t request_arrival_time_;

  NEED_SERIALIZE_AND_DESERIALIZE;

  TO_STRING_KV(K_(len), K_(arrival_push_diff),
      K_(push_pop_diff), K_(pop_process_start_diff), K_(process_start_end_diff),
      K_(process_end_response_diff), K_(packet_id), K_(request_arrival_time));
};

} // end of namespace rpc
} // end of namespace oceanbase
#endif
