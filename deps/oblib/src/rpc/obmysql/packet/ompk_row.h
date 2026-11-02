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

#ifndef _OMPK_ROW_H_
#define _OMPK_ROW_H_

#include "rpc/obmysql/ob_mysql_packet.h"
#include "rpc/obmysql/ob_mysql_row.h"

namespace oceanbase
{
namespace obmysql
{

class OMPKRow : public ObMySQLPacket
{
public:
  explicit OMPKRow(const ObMySQLRow &row);
  virtual ~OMPKRow() { }
  inline ObMySQLPacketType get_mysql_packet_type() { return ObMySQLPacketType::PKT_ROW; }

  virtual int serialize(char *buffer, int64_t len, int64_t &pos) const;
private:
  DISALLOW_COPY_AND_ASSIGN(OMPKRow);
  const ObMySQLRow &row_;
};

} // end of namespace obmysql
} // end of namespace oceanbase

#endif /* _OMPK_ROW_H_ */
