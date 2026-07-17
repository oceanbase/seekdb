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

#ifndef OCEANBASE_OBMYSQL_OB_SQL_SOCK_PROCESSOR_H_
#define OCEANBASE_OBMYSQL_OB_SQL_SOCK_PROCESSOR_H_
#include "rpc/obmysql/ob_mysql_protocol_processor.h"
#include "rpc/obmysql/ob_mysql_compress_protocol_processor.h"
#include "rpc/obmysql/ob_2_0_protocol_processor.h"

namespace oceanbase
{
namespace rpc { class ObRequest; } // end of namespace rpc
namespace rpc { namespace frame { class ObReqTranslator; } }

namespace obmysql
{
class ObSqlNio;
class ObSqlSockSession;
class ObSqlSockProcessor
{
public:
  ObSqlSockProcessor():
      mysql_processor_(), compress_processor_(), ob_2_0_processor_() {}
  ~ObSqlSockProcessor() {}
  int decode_sql_packet(ObICSMemPool& mem_pool, ObSqlSockSession& sess, void* read_handle, rpc::ObPacket*& pkt);
  int build_sql_req(ObSqlSockSession& sess, rpc::ObPacket* pkt, rpc::ObRequest*& sql_req);
private:
  ObVirtualCSProtocolProcessor *get_protocol_processor(common::ObCSProtocolType type);
private:
  ObMysqlProtocolProcessor mysql_processor_;
  ObMysqlCompressProtocolProcessor compress_processor_;
  Ob20ProtocolProcessor ob_2_0_processor_;
};

}; // end namespace obmysql
}; // end namespace oceanbase

#endif /* OCEANBASE_OBMYSQL_OB_SQL_SOCK_PROCESSOR_H_ */
