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

#ifndef OCEANBASE_QUERY_PROTOCOL_OB_CLIENT_PROTOCOL_H_
#define OCEANBASE_QUERY_PROTOCOL_OB_CLIENT_PROTOCOL_H_

#include "lib/container/ob_iarray.h"
#include "lib/ob_define.h"

namespace oceanbase
{
namespace obmysql
{
class ObICSMemPool;
class ObMySQLField;
class ObMySQLPacket;
}
namespace sql
{
class ObResultSet;
class ObSQLSessionInfo;

class ObIClientPacketChannel
{
public:
  virtual ~ObIClientPacketChannel() = default;
  virtual int wait_packet(obmysql::ObICSMemPool &mem_pool,
                          int64_t timeout_us,
                          obmysql::ObMySQLPacket *&packet) = 0;
  virtual int release_packet(obmysql::ObMySQLPacket *packet) = 0;
  virtual int response_packet(obmysql::ObMySQLPacket &packet) = 0;
  virtual int response_resultset_metadata(
      const common::ObIArray<obmysql::ObMySQLField> &fields,
      bool include_result_header,
      uint8_t eof_field_count,
      uint16_t warnings,
      uint16_t status_flags) = 0;
  virtual int flush_buffer(bool is_last) = 0;
};

class ObIQueryResultSender
{
public:
  virtual ~ObIQueryResultSender() = default;
  virtual int response_query_result(ObResultSet &result,
                                    bool is_ps_protocol,
                                    bool has_more_result,
                                    bool &can_retry,
                                    int64_t fetch_limit = common::OB_INVALID_COUNT) = 0;
  virtual int send_eof_packet(bool has_more_result) = 0;
  virtual ObIClientPacketChannel &get_packet_sender() = 0;
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_QUERY_PROTOCOL_OB_CLIENT_PROTOCOL_H_
