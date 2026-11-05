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

#ifndef _OBMP_INIT_DB_H
#define _OBMP_INIT_DB_H
#include "observer/mysql/obmp_base.h"
#include "rpc/obmysql/ob_mysql_packet.h"
namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace observer
{
class ObMPInitDB: public ObMPBase
{
public:
  static const obmysql::ObMySQLCmd COM = obmysql::COM_INIT_DB;
public:
  explicit ObMPInitDB(const ObGlobalContext &gctx)
      :ObMPBase(gctx),
       db_name_()
  {}
  virtual ~ObMPInitDB() {}
protected:
  int process();
  int deserialize();
private:
  int do_process(sql::ObSQLSessionInfo *session);
private:
  int get_catalog_id_(sql::ObSQLSessionInfo &session, ObSchemaGetterGuard &schema_guard, uint64_t &catalog_id);
  common::ObString catalog_name_;
  common::ObString db_name_;
  char db_name_conv_buf[common::OB_MAX_DATABASE_NAME_BUF_LENGTH];
};

} // end namespace observer
} // end namespace oceanbase

#endif /* _OBMP_INIT_DB_H */
