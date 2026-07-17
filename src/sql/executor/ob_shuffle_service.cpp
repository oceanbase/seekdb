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

#define USING_LOG_PREFIX SQL_EXE
#include "ob_shuffle_service.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_func_part_hash.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::observer;
namespace oceanbase
{
namespace sql
{
// Q: Why does the key partition processing in ObShuffleService always take a separate path?
//
// Answer: Implementation reason. Theoretically, the two can be unified.
//     In 2017, the author of this logic did not make an effort to integrate the two, resulting in two separate branches, leading to the current situation.
//
//     key partition depends on the key expression performing calculations on multiple columns, then taking the modulus of the result,
//     hash partition performs a calculation on a hash function, then takes the modulus of the result.
//
//     part_func, subpart_func these two parameters are specifically for key partitioning, use them to calculate out
//     a value, and then call the hash function to calculate the final part id.
//
//     For hash partitioning, only repart_columns, repart_sub_columns need to be passed in,
//     Its calculation hash value expression is a fixed function, the function's parameter is specified by repart_columns
//
//




int ObShuffleService::get_part_id(ObExecContext &exec_ctx,
                                  const share::schema::ObTableSchema &table_schema,
                                  const common::ObNewRow &row,
                                  const ObSqlExpression &part_func,
                                  const ObIArray<ObTransmitRepartColumn> &repart_columns,
                                  int64_t &part_id)
{
  int ret = OB_SUCCESS;
  if (table_schema.is_key_part()) {
    if (OB_FAIL(get_key_part_id(exec_ctx, table_schema, row, part_func, part_id))) {
      LOG_WARN("get key part id failed");
    }
  } else if (table_schema.is_list_part() ||
             table_schema.is_range_part() ||
             table_schema.is_hash_part()) {
    if (OB_FAIL(get_non_key_partition_part_id(exec_ctx, table_schema, row,
                                              repart_columns, part_id))) {
      LOG_WARN("failed to get non key partition part id", K(ret));
    }
  } else {
    ret = OB_NOT_IMPLEMENT;
    LOG_WARN("this type of partition is not implement", K(ret));
  }
  return ret;
}

int ObShuffleService::get_key_part_id(ObExecContext &exec_ctx,
                                      const share::schema::ObTableSchema &table_schema,
                                      const common::ObNewRow &row,
                                      const ObSqlExpression &part_func,
                                      int64_t &part_id)
{
  int ret = OB_SUCCESS;
  UNUSED(exec_ctx);
  int64_t calc_result = 0;
  ObObj func_result;
  int64_t part_count = table_schema.get_part_option().get_part_num();
  // First-level partition
  if (part_count <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the part num can not be null", K(part_count), K(part_func), K(ret));
  } else if (OB_FAIL(part_func.calc(expr_ctx_, row, func_result))) {
    LOG_WARN("Failed to calc hash expr", K(ret), K(row));
  } else if (OB_FAIL(func_result.get_int(calc_result))) {
    LOG_WARN("Fail to get int64 from result", K(func_result), K(ret));
  } else if (calc_result < 0) {
    ret =  OB_INVALID_ARGUMENT;
    LOG_WARN("Input arguments is invalid", K(calc_result), K(part_count), K(ret));
  } else if (OB_FAIL(ObPartitionUtils::calc_hash_part_idx(calc_result, part_count, part_id))) {
    LOG_WARN("calc_hash_part_idx failed", K(ret));
  }
  return ret;
}

int ObShuffleService::get_non_key_partition_part_id(ObExecContext &exec_ctx,
                                                    const ObTableSchema &table_schema,
                                                    const ObNewRow &row,
                                                    const ObIArray<ObTransmitRepartColumn> &repart_columns,
                                                    int64_t &part_id)
{
  return OB_NOT_SUPPORTED;
}

int ObShuffleService::get_subpart_id(ObExecContext &exec_ctx,
                                     const share::schema::ObTableSchema &table_schema,
                                     const common::ObNewRow &row,
                                     int64_t part_id,
                                     const ObSqlExpression &subpart_func,
                                     const ObIArray<ObTransmitRepartColumn> &repart_sub_columns,
                                     int64_t &subpart_id)
{
  int ret = OB_SUCCESS;
  if (table_schema.is_key_subpart()) {
    if (OB_FAIL(get_key_subpart_id(exec_ctx, table_schema, row,
                                   part_id, subpart_func, subpart_id))) {
      LOG_WARN("get key subpart id failed");
    }
  } else if (table_schema.is_list_subpart()
             || table_schema.is_range_subpart()
             || table_schema.is_hash_subpart()) {
    if (OB_FAIL(get_non_key_subpart_id(exec_ctx, table_schema, row, part_id,
                                       repart_sub_columns, subpart_id))) {
      LOG_WARN("get range or hash subpart id failed");
    }
  } else {
    ret = OB_NOT_IMPLEMENT;
    LOG_WARN("we only support the range, range column, key, hash repartition exe", K(ret));
  }
  return ret;
}
// FIXME: support non-template secondary partitioning
int ObShuffleService::get_key_subpart_id(ObExecContext &exec_ctx,
                                         const ObTableSchema &table_schema,
                                         const ObNewRow &row,
                                         int64_t part_id,
                                         const ObSqlExpression &subpart_func,
                                         int64_t &subpart_id)
{
  return OB_NOT_SUPPORTED;
}
//FIXME:Here and 22x implementation are inconsistent, need to check it out
int ObShuffleService::get_non_key_subpart_id(ObExecContext &exec_ctx,
                                             const ObTableSchema &table_schema,
                                             const ObNewRow &row,
                                             int64_t part_id,
                                             const ObIArray<ObTransmitRepartColumn> &repart_sub_columns,
                                             int64_t &subpart_id)
{
  return OB_NOT_SUPPORTED;
}


int ObShuffleService::init_expr_ctx(ObExecContext &exec_ctx)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *my_session = NULL;
  const ObTimeZoneInfo *tz_info = NULL;
  int64_t tz_offset = 0;
  if (nullptr != expr_ctx_.exec_ctx_) {
    //Has been inited, do nothing.
  } else if (OB_ISNULL(my_session = exec_ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL", K(ret));
  } else if (OB_ISNULL(tz_info = get_timezone_info(my_session))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get tz info pointer failed", K(ret));
  } else if (OB_FAIL(get_tz_offset(tz_info, tz_offset))) {
    LOG_WARN("get tz offset failed", K(ret));
  } else {
    expr_ctx_.cast_mode_ = CM_WARN_ON_FAIL;
    expr_ctx_.exec_ctx_ = &exec_ctx;
    expr_ctx_.calc_buf_ = &exec_ctx.get_allocator();
    expr_ctx_.phy_plan_ctx_ = exec_ctx.get_physical_plan_ctx();
    expr_ctx_.my_session_ = my_session;
    expr_ctx_.tz_offset_ = tz_offset;
    EXPR_SET_CAST_CTX_MODE(expr_ctx_);
  }
  return ret;
}

}
}

