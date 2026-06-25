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

#ifndef OCEANBASE_SHARE_TABLE_OB_SRS_IMPORTER_H_
#define OCEANBASE_SHARE_TABLE_OB_SRS_IMPORTER_H_

#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "share/ob_module_data_arg.h"

namespace oceanbase
{
namespace table
{

class ObSRSImporter
{
public:
  explicit ObSRSImporter(sql::ObExecContext& exec_ctx)
      : exec_ctx_(exec_ctx), affected_rows_(0)
  {}
  virtual ~ObSRSImporter() {}
  int exec_op(table::ObModuleDataArg op_arg);
  OB_INLINE int64_t get_affected_rows() { return affected_rows_; }
  static int get_srs_cnt(ObCommonSqlProxy *sql_proxy, int64_t &srs_cnt);

private:
  int import_srs_info(const ObString &file_path);

  sql::ObExecContext& exec_ctx_;
  int64_t affected_rows_;
};

}  // namespace table
}  // namespace oceanbase
#endif
