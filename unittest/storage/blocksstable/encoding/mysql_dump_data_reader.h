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

#ifndef OCEANBASE_ENCODING_MYSQL_DUMP_DATA_READER_H_
#define OCEANBASE_ENCODING_MYSQL_DUMP_DATA_READER_H_

#include <vector>
#include <string>
#include <fstream>
#include "lib/string/ob_string.h"

namespace oceanbase
{
class MysqlDumpDataReader
{
public:
  MysqlDumpDataReader();
  virtual ~MysqlDumpDataReader() = default;
  int init(const char *file);
  // SQLs which not insert stmt
  const std::vector<std::string> &schema_sql() { return schema_sqls_; }
  int next_data(std::vector<common::ObString> &data);
  int reset();

private:
  int next_sql();
  int parse_insert_sql();

private:
  std::string cur_sql_;
  std::string cur_line_;
  std::vector<std::string> schema_sqls_;
  std::ifstream stream_;
  int64_t value_cnt_;
  int64_t data_index_;
  std::vector<common::ObString> datas_;
};
} // end namespace oceanbase

#endif // OCEANBASE_ENCODING_MYSQL_DUMP_DATA_READER_H_
