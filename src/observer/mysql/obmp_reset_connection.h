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

#ifndef OCEANBASE_OBSERVER_OBMP_RESET_CONNECTION
#define OCEANBASE_OBSERVER_OBMP_RESET_CONNECTION

#include "lib/string/ob_string.h"
#include "observer/mysql/obmp_base.h"
#include "rpc/obmysql/ob_mysql_packet.h"
#include "sql/parser/parse_node.h"
namespace oceanbase
{
namespace sql
{
class ObBasicSessionInfo;
}
namespace observer
{
class ObMPResetConnection : public ObMPBase
{
public:
  static const obmysql::ObMySQLCmd COM = obmysql::COM_RESET_CONNECTION;
  explicit ObMPResetConnection(const ObGlobalContext &gctx)
      :ObMPBase(gctx),
      pkt_()
  {
  }

  virtual ~ObMPResetConnection() {}

protected:
  int process();
  virtual int deserialize()
  { return common::OB_SUCCESS; }

private:

private:
  obmysql::ObMySQLRawPacket pkt_;
  DISALLOW_COPY_AND_ASSIGN(ObMPResetConnection);
};// end of class

} // end of namespace observer
} // end of namespace oceanbase

#endif /* OCEANBASE_OBSERVER_OBMP_RESET_CONNECTION */
