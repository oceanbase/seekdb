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

#ifndef OCEANBASE_DICT_SERVICE_DATA_DICTIONARY_SQL_CLIENT_
#define OCEANBASE_DICT_SERVICE_DATA_DICTIONARY_SQL_CLIENT_

#include "lib/mysqlclient/ob_mysql_result.h"    // ObMySQLResult
#include "logservice/palf/lsn.h"                // LSN
#include "share/scn.h"                // SCN
#include "share/ob_ls_id.h"                     // ObLSArray

namespace oceanbase
{
namespace common
{
 class ObMySQLProxy;
}
namespace datadict
{

class ObDataDictSqlClient
{
public:
  ObDataDictSqlClient();
  ~ObDataDictSqlClient() { destroy(); }
public:
  int init(common::ObMySQLProxy *sql_client);
  void destroy();
public:
  int get_ls_info(
      const uint64_t tenant_id,
      const share::SCN &snapshot_scn,
      share::ObLSArray &ls_array);
  int get_schema_version(
      const uint64_t tenant_id,
      const share::SCN &snapshot_scn,
      int64_t &schema_version);
public:
  int report_data_dict_persist_info(
      const uint64_t tenant_id,
      const share::SCN &snapshot_scn,
      const palf::LSN &start_lsn,
      const palf::LSN &end_lsn);
private:
  static const char *query_ls_info_sql_format;
  static const char *query_tenant_schema_version_sql_format;
  static const char *report_data_dict_persist_info_sql_format;
private:
  int parse_record_from_row_(
      common::sqlclient::ObMySQLResult &result,
      int64_t &record_count,
      int64_t &schema_version);
private:
  bool is_inited_;
  common::ObMySQLProxy *sql_proxy_;
};

} // namespace datadict
} // namespace oceanbase
#endif
