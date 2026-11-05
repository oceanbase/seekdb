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

#ifndef _OMPK_AUTH_SWITCH_H_
#define _OMPK_AUTH_SWITCH_H_

#include "lib/string/ob_string.h"
#include "rpc/obmysql/ob_mysql_packet.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase
{
namespace obmysql
{

class OMPKAuthSwitch : public ObMySQLPacket
{
public:
  OMPKAuthSwitch();
  virtual ~OMPKAuthSwitch() {}

  // serialize all data into thread buffer not include packet header
  // Attention!! before called serialize or get_serialize_size, must set capability
  virtual int serialize(char *buffer, const int64_t length, int64_t &pos) const;
  virtual int64_t get_serialize_size() const;

  // shadow copy
  void set_plugin_name(const common::ObString &plugin_name) { plugin_name_ = plugin_name; }
  void set_scramble(const common::ObString &scramble) { scramble_ = scramble; }

  inline const common::ObString &get_plugin_name() const { return plugin_name_; }
  inline const common::ObString &get_scramble() const { return scramble_; };
  inline ObMySQLPacketType get_mysql_packet_type() { return ObMySQLPacketType::PKT_AUTH_SWITCH; }

  virtual int64_t to_string(char *buf, const int64_t buf_len) const;
private:
  DISALLOW_COPY_AND_ASSIGN(OMPKAuthSwitch);

  uint8_t status_;    // always 0xfe
  common::ObString plugin_name_;
  common::ObString scramble_;
};

} // end namespace obmysql
} // end namespace oceanbase
#endif /* _OMPK_AUTH_SWITCH_H_ */
