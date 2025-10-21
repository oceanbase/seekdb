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
#define USING_LOG_PREFIX SHARE

#include "ob_balance_define.h"

namespace oceanbase
{
namespace share
{
bool need_balance_table(const schema::ObSimpleTableSchemaV2 &table_schema)
{
  bool need_balance = false;
  const char* table_type_str = NULL;
  need_balance = check_if_need_balance_table(table_schema, table_type_str);
  return need_balance;
}

bool check_if_need_balance_table(
    const schema::ObSimpleTableSchemaV2 &table_schema,
    const char *&table_type_str)
{
  bool need_balance = false;
  if (table_schema.is_broadcast_table() || table_schema.is_duplicate_table()) {
    table_type_str = "DUPLICATE TABLE";
  } else if (table_schema.is_index_table() && !table_schema.is_global_index_table()) {
    table_type_str = "LOCAL INDEX";
  } else {
    table_type_str = ob_table_type_str(table_schema.get_table_type());
  }
  need_balance = table_schema.is_user_table()
      || table_schema.is_global_index_table()
      || table_schema.is_tmp_table();
  return need_balance;
}

}
}
