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

// Keep the low-level ASH module independent from the SQL packet implementation.
#include "rpc/obmysql/ob_mysql_packet.h"
namespace oceanbase
{
const char *ob_ash_mysql_cmd_name(int32_t mysql_cmd)
{
  return obmysql::ObMySQLPacket::get_mysql_cmd_name(static_cast<obmysql::ObMySQLCmd>(mysql_cmd));
}
}
