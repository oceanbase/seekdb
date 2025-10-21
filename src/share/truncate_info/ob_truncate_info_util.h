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
#ifndef OB_SHARE_TRUNCATE_INFO_TRUNCATE_EXPR_UTIL_H_
#define OB_SHARE_TRUNCATE_INFO_TRUNCATE_EXPR_UTIL_H_
#include "share/schema/ob_schema_struct.h"
#include "src/share/ob_rpc_struct.h"
namespace oceanbase
{
namespace share
{

struct ObTruncateInfoUtil final
{
  static bool could_write_truncate_info_part_type(
    const obrpc::ObAlterTableArg::AlterPartitionType &alter_type,
    const schema::ObPartitionFuncType input_part_type,
    const schema::ObPartitionFuncType input_subpart_type)
  {
    bool bret = false;
    if (obrpc::ObAlterTableArg::DROP_PARTITION == alter_type ||
        obrpc::ObAlterTableArg::TRUNCATE_PARTITION == alter_type) {
      bret = could_write_truncate_info_part_type(input_part_type);
    } else if (obrpc::ObAlterTableArg::DROP_SUB_PARTITION == alter_type ||
        obrpc::ObAlterTableArg::TRUNCATE_SUB_PARTITION == alter_type) {
      bret = could_write_truncate_info_part_type(input_part_type)
             && could_write_truncate_info_part_type(input_subpart_type);
    }
    return bret;
  }
private:
  static bool could_write_truncate_info_part_type(
    const schema::ObPartitionFuncType input_part_type)
  {
    return (schema::PARTITION_FUNC_TYPE_RANGE == input_part_type
      || schema::PARTITION_FUNC_TYPE_RANGE_COLUMNS == input_part_type
      || schema::PARTITION_FUNC_TYPE_LIST == input_part_type
      || schema::PARTITION_FUNC_TYPE_LIST_COLUMNS == input_part_type);
  }
};

} // namespace share
} // namespace oceanbase

#endif // OB_SHARE_TRUNCATE_INFO_TRUNCATE_EXPR_UTIL_H_
