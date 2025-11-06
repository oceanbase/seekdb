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

#include "ob_end_trans_cb_packet_param.h"
#include "sql/ob_result_set.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{
ObEndTransCbPacketParam &ObEndTransCbPacketParam::operator=(const ObEndTransCbPacketParam& other)
{
  MEMCPY(message_, other.message_, MSG_SIZE);
  affected_rows_ = other.affected_rows_;
  last_insert_id_to_client_ = other.last_insert_id_to_client_;
  is_partition_hit_ = other.is_partition_hit_;
  trace_id_.set(other.trace_id_);
  is_valid_ = other.is_valid_;
  return *this;
}

const ObEndTransCbPacketParam &ObEndTransCbPacketParam::fill(ObResultSet &rs,
                                                             ObSQLSessionInfo &session,
                                                             const ObCurTraceId::TraceId &trace_id)
{
  MEMCPY(message_, rs.get_message(), MSG_SIZE); // TODO: optimize out
  // oracle ANONYMOUS_BLOCK affect rows always return 1
  affected_rows_ = stmt::T_ANONYMOUS_BLOCK == rs.get_stmt_type() 
                    ? 1 : rs.get_affected_rows();
  // The commit asynchronous callback logic needs
  // to trigger the update logic of affected row first.
  if (session.is_session_sync_support()) {
    session.set_affected_rows_is_changed(affected_rows_);
  }
  session.set_affected_rows(affected_rows_);
  last_insert_id_to_client_ = rs.get_last_insert_id_to_client();
  is_partition_hit_ = session.partition_hit().get_bool();
  trace_id_.set(trace_id);
  is_valid_ = true;
  return *this;
}

const ObEndTransCbPacketParam &ObEndTransCbPacketParam::fill(const char *message,
                                                             int64_t affected_rows,
                                                             uint64_t last_insert_id_to_client,
                                                             bool is_partition_hit,
                                                             const ObCurTraceId::TraceId &trace_id)
{
  MEMCPY(message_, message, strlen(message));
  affected_rows_ = affected_rows;
  last_insert_id_to_client_ = last_insert_id_to_client;
  is_partition_hit_ = is_partition_hit;
  trace_id_.set(trace_id);
  is_valid_ = true;
  return *this;
}

}/* ns sql*/
}/* ns oceanbase */


