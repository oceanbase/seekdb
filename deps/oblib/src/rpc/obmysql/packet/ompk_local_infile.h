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

#ifndef _OMPK_LOCAL_INFILE_H_
#define _OMPK_LOCAL_INFILE_H_

#include "rpc/obmysql/ob_mysql_packet.h"
#include "lib/ob_define.h"

namespace oceanbase
{
namespace obmysql
{

// In the MySQL client/server protocol, server send a `local infile` message to client
// to specific the file name to load.
// format:
// int<1>      | packet type  | 0xFB: LOCAL INFILE
// string<EOF> | filename     | the path to the file the client shall send
// The notation is "string<EOF>" Strings whose length will be calculated by the packet remaining length.
class OMPKLocalInfile : public ObMySQLRawPacket
{
public:
  OMPKLocalInfile();
  virtual ~OMPKLocalInfile();

  virtual int serialize(char *buffer, int64_t len, int64_t &pos) const override;
  virtual int64_t get_serialize_size() const override;

  virtual int64_t to_string(char *buf, const int64_t buf_len) const override;

  void set_filename(const ObString &filename);

  inline ObMySQLPacketType get_mysql_packet_type() override { return ObMySQLPacketType::PKT_FILENAME; }

private:
  DISALLOW_COPY_AND_ASSIGN(OMPKLocalInfile);
  int8_t   packet_type_;
  ObString filename_;
};

} // end of namespace obmysql
} // end of namespace oceanbase

#endif /* _OMPK_LOCAL_INFILE_H_ */
