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

#ifndef _OMPK_STRING_H_
#define _OMPK_STRING_H_

#include "rpc/obmysql/ob_mysql_packet.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace obmysql
{

class OMPKString
    : public ObMySQLPacket
{
public:
  explicit OMPKString(const common::ObString &str)
      : str_(str)
  {}

  virtual ~OMPKString() {};
  inline const common::ObString &get_string() const { return str_; }
  inline ObMySQLPacketType get_mysql_packet_type() { return ObMySQLPacketType::PKT_STR; }

  VIRTUAL_TO_STRING_KV(K_(str));

private:
  DISALLOW_COPY_AND_ASSIGN(OMPKString);
  common::ObString str_;
}; // end of class OMPKString

} // end of namespace obmysql
} // end of namespace oceanbase


#endif /* _OMPK_STRING_H_ */
