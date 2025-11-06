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

#ifndef OCEANBASE_SHARE_OB_BALANCE_DEFINE_H_
#define OCEANBASE_SHARE_OB_BALANCE_DEFINE_H_

#include "share/ob_common_id.h"             // ObCommonID
#include "share/schema/ob_table_schema.h"   // ObSimpleTableSchemaV2

namespace oceanbase
{
namespace share
{
typedef ObCommonID ObBalanceJobID;
typedef ObCommonID ObBalanceTaskID;
typedef ObCommonID ObTransferTaskID;
typedef ObCommonID ObTransferPartitionTaskID;

// check Tables that need balance by RS
//
// 1. USER TABLE: user created table, need balance
// 2. GLOBAL INDEX: global index is distributed independently from the main table, need balance
// 3. TMP TABLE: temp table is created by user, need balance
bool need_balance_table(const schema::ObSimpleTableSchemaV2 &table_schema);
bool check_if_need_balance_table(
    const schema::ObSimpleTableSchemaV2 &table_schema,
    const char *&table_type_str);
}
}

#endif /* !OCEANBASE_SHARE_OB_BALANCE_DEFINE_H_ */
