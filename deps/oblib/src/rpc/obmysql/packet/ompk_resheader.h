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

#ifndef _OMPK_RESHEADER_H_
#define _OMPK_RESHEADER_H_

#include "lib/ob_define.h"
#include "rpc/obmysql/ob_mysql_packet.h"

namespace oceanbase
{
namespace obmysql
{

class OMPKResheader : public ObMySQLPacket
{
public:
  OMPKResheader();
  virtual ~OMPKResheader();

  virtual int serialize(char *buffer, int64_t len, int64_t &pos) const;
  virtual int64_t get_serialize_size() const;

  inline void set_field_count(uint64_t count)
  {
    field_count_ = count;
  }

  //for test
  inline uint64_t get_field_count() const
  {
    return field_count_;
  }
  inline ObMySQLPacketType get_mysql_packet_type() { return ObMySQLPacketType::PKT_RESHEAD; }

private:
  DISALLOW_COPY_AND_ASSIGN(OMPKResheader);

  uint64_t field_count_;
};

} // end of namespace obmysql
} // end of namespace oceanbase

#endif /* _OMPK_RESHEADER_H_ */
