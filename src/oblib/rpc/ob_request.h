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

#ifndef _OCEABASE_RPC_OB_REQUEST_H_
#define _OCEABASE_RPC_OB_REQUEST_H_

#include "lib/oblog/ob_log.h"
#include "lib/net/ob_addr.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/time/ob_time_utility.h"
#include "lib/queue/ob_link.h"
#include "lib/hash/ob_fixed_hash2.h"
#include "lib/profile/ob_trace_id.h"
#include "rpc/ob_packet.h"
#include "rpc/ob_lock_wait_node.h"
#include "rpc/ob_reusable_mem.h"

namespace oceanbase
{
namespace obmysql
{
class ObSqlSockSession;
}
namespace rpc
{

using common::ObAddr;
typedef common::ObCurTraceId::TraceId TraceId;
extern common::ObAddr g_server_self_addr;
class ObSqlRequestOperator;
class ObRequest: public common::ObLink
{
public:
  friend class ObSqlRequestOperator;
  enum Type { OB_MYSQL, OB_TASK, OB_SQL_TASK, OB_DAS_PARALLEL_TASK };
  enum Stat {
      OB_REQUEST_MYSQL_DELIVER            = 0,
      OB_REQUEST_RUNTIME_RECEIVED         = 1,
      OB_REQUEST_WORKER_PROCESSOR_RUN     = 2,
      OB_REQUEST_QHANDLER_PROCESSOR_RUN   = 3,
      OB_REQUEST_SQL_PROCESSOR_RUN        = 4,
      OB_REQUEST_MPQUERY_PROCESS          = 5,
      OB_REQUEST_FINISH_SQL               = 6,
  };
public:
  explicit ObRequest(Type type)
      : handling_state_(-1), nio_request_generation_(0), type_(type),
        handle_ctx_(NULL), group_id_(0), pkt_(NULL),
        connection_phase_(ConnectionPhaseEnum::CPE_CONNECTED),
        recv_timestamp_(0), enqueue_timestamp_(0),
        request_arrival_time_(0), traverse_index_(0), recv_mts_(), arrival_push_diff_(0),
        push_pop_diff_(0), pop_process_start_diff_(0),
        process_start_end_diff_(0), process_end_response_diff_(0),
        trace_id_(), discard_flag_(false), retry_times_(0)
  {
  }
  virtual ~ObRequest() {}  // not guaranteed to call

  uint64_t get_nio_request_generation() const { return nio_request_generation_; }
  void set_nio_request_generation(uint64_t generation) { nio_request_generation_ = generation; }
  void set_server_handle_context(obmysql::ObSqlSockSession *ctx) { handle_ctx_ = ctx; }
  obmysql::ObSqlSockSession *get_server_handle_context() const { return handle_ctx_; }
  Type get_type() const { return type_; }
  void set_type(const Type &type) { type_ = type; }

  int32_t get_group_id() const { return group_id_; }
  void set_group_id(const int32_t &group_id) { group_id_ = group_id; }
  void set_packet(const ObPacket *pkt);
  const ObPacket &get_packet() const;
  int64_t get_receive_timestamp() const;
  common::ObMonotonicTs get_receive_mts() const;
  void set_receive_timestamp(const int64_t recv_timestamp);
  void set_enqueue_timestamp(const int64_t enqueue_timestamp);
  void set_request_arrival_time(const int64_t now);
  void set_arrival_push_diff(const int64_t now);
  void set_push_pop_diff(const int64_t now);
  void set_pop_process_start_diff(const int64_t now);
  void set_process_start_end_diff(const int64_t now);
  void set_process_end_response_diff(const int64_t now);
  void set_discard_flag(const bool discard_flag);
  void set_retry_times(const int32_t retry_times);
  int64_t get_enqueue_timestamp() const;
  int64_t get_request_arrival_time() const;
  int32_t get_arrival_push_diff() const;
  int32_t get_push_pop_diff() const;
  int32_t get_pop_process_start_diff() const;
  int32_t get_process_start_end_diff() const;
  int32_t get_process_end_response_diff() const;
  int64_t get_traverse_index() const { return traverse_index_; }
  bool get_discard_flag() const;
  int32_t get_retry_times() const;
  void set_connection_phase(ConnectionPhaseEnum connection_phase) { connection_phase_ = connection_phase; }
  bool is_in_connected_phase() const { return ConnectionPhaseEnum:: CPE_CONNECTED == connection_phase_; }
  void on_process_begin() { reusable_mem_.reuse(); }

  TraceId generate_trace_id(const ObAddr &addr);
  const TraceId &get_trace_id() const { return trace_id_; }
  void reset_trace_id() { trace_id_.reset(); }
  int set_trace_point(int trace_point = 0);
  int set_traverse_index(int64_t index);

  ObLockWaitNode &get_lock_wait_node() { return lock_wait_node_; }
  bool is_retry_on_lock() const { return lock_wait_node_.try_lock_times_ > 0;}
  VIRTUAL_TO_STRING_KV("packet", pkt_, "type", type_, "group", group_id_, "connection_phase", connection_phase_, K(recv_timestamp_), K(enqueue_timestamp_), K(request_arrival_time_), K(trace_id_));

  ObLockWaitNode lock_wait_node_;
  mutable ObReusableMem reusable_mem_;
public:
  int32_t handling_state_;
protected:
  uint64_t nio_request_generation_;
  Type type_;
  obmysql::ObSqlSockSession *handle_ctx_;
  int32_t group_id_;
  const ObPacket *pkt_;
  ConnectionPhaseEnum connection_phase_;
  int64_t recv_timestamp_;
  int64_t enqueue_timestamp_;
  int64_t request_arrival_time_;
  int64_t traverse_index_;
  // only used by transaction
  common::ObMonotonicTs recv_mts_;
  int32_t arrival_push_diff_;
  int32_t push_pop_diff_;
  int32_t pop_process_start_diff_;
  int32_t process_start_end_diff_;
  int32_t process_end_response_diff_;

  mutable TraceId trace_id_;
  bool discard_flag_;
  int32_t retry_times_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRequest);
}; // end of class ObRequest

inline void ObRequest::set_packet(const ObPacket *pkt)
{
  pkt_ = pkt;
}

inline const ObPacket &ObRequest::get_packet() const
{
  return *pkt_;
}

inline int64_t ObRequest::get_receive_timestamp() const
{
  return recv_timestamp_;
}

inline common::ObMonotonicTs ObRequest::get_receive_mts() const
{
  return recv_mts_;
}

inline void ObRequest::set_receive_timestamp(const int64_t recv_timestamp)
{
  recv_timestamp_ = recv_timestamp;
  // used by transaction
  recv_mts_ = ObMonotonicTs::current_time();
}

inline int64_t ObRequest::get_enqueue_timestamp() const
{
  return enqueue_timestamp_;
}

inline void ObRequest::set_enqueue_timestamp(const int64_t enqueue_timestamp)
{
  enqueue_timestamp_ = enqueue_timestamp;
}

inline int64_t ObRequest::get_request_arrival_time() const
{
  return request_arrival_time_;
}

inline void ObRequest::set_request_arrival_time(const int64_t request_arrival_time)
{
  request_arrival_time_ = request_arrival_time;
}

inline int32_t ObRequest::get_arrival_push_diff() const
{
  return arrival_push_diff_;
}

inline void ObRequest::set_arrival_push_diff(const int64_t now)
{
  arrival_push_diff_ = (int32_t)(now - request_arrival_time_);
}

inline int32_t ObRequest::get_push_pop_diff() const
{
  return push_pop_diff_;
}

inline void ObRequest::set_push_pop_diff(const int64_t now)
{
  push_pop_diff_ = (int32_t)(now - request_arrival_time_ - arrival_push_diff_);
}

inline int32_t ObRequest::get_pop_process_start_diff() const
{
  return pop_process_start_diff_;
}

inline void ObRequest::set_pop_process_start_diff(const int64_t now)
{
  pop_process_start_diff_ = (int32_t)(now - request_arrival_time_ - arrival_push_diff_ - push_pop_diff_);
}

inline int32_t ObRequest::get_process_start_end_diff() const
{
  return process_start_end_diff_;
}

inline void ObRequest::set_process_start_end_diff(const int64_t now)
{
  process_start_end_diff_ = (int32_t)(now - request_arrival_time_ - arrival_push_diff_
    - push_pop_diff_ - pop_process_start_diff_);
}

inline int32_t ObRequest::get_process_end_response_diff() const
{
  return process_end_response_diff_;
}

inline void ObRequest::set_process_end_response_diff(const int64_t now)
{
  process_end_response_diff_ = (int32_t)(now - request_arrival_time_ - arrival_push_diff_
    - push_pop_diff_ - pop_process_start_diff_ - process_start_end_diff_);
}

inline bool ObRequest::get_discard_flag() const
{
  return discard_flag_;
}

inline void ObRequest::set_discard_flag(const bool discard_flag)
{
  discard_flag_ = discard_flag;
}

inline int32_t ObRequest::get_retry_times() const
{
  return retry_times_;
}

inline void ObRequest::set_retry_times(const int32_t retry_times)
{
  retry_times_ = retry_times;
}
inline TraceId ObRequest::generate_trace_id(const ObAddr &addr)
{
  if (trace_id_.is_invalid()) {
    trace_id_.init(addr);
  }
  return trace_id_;
}

void on_translate_fail(ObRequest* req, int ret);
} // end of namespace rp
} // end of namespace oceanbase

#endif /* _OCEABASE_RPC_OB_REQUEST_H_ */
