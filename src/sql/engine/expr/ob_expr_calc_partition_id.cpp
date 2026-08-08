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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_expr_calc_partition_id.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_cmp_func.h"
#include "sql/engine/expr/ob_expr_func_part_hash.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

OB_SERIALIZE_MEMBER(CalcPartitionBaseInfo,
                    ref_table_id_,
                    related_table_ids_,
                    part_level_,
                    part_type_,
                    subpart_type_,
                    part_num_,
                    subpart_num_,
                    partition_id_calc_type_,
                    calc_id_type_);

int CalcPartitionBaseInfo::deep_copy(common::ObIAllocator &allocator,
                                     const ObExprOperatorType type,
                                     ObIExprExtraInfo *&copied_info) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObExprExtraInfoFactory::alloc(allocator, type,
                                            copied_info))) {
    LOG_WARN("failed to alloc extra info", K(ret));
  } else {
    CalcPartitionBaseInfo *base_info = static_cast<CalcPartitionBaseInfo*>(copied_info);
    if (OB_FAIL(base_info->related_table_ids_.assign(related_table_ids_))) {
      LOG_WARN("assign related table ids failed", K(ret));
    } else {
      base_info->ref_table_id_ = ref_table_id_;
      base_info->part_level_ = part_level_;
      base_info->part_type_ = part_type_;
      base_info->subpart_type_ = subpart_type_;
      base_info->part_num_ = part_num_;
      base_info->subpart_num_ = subpart_num_;
      base_info->partition_id_calc_type_ = partition_id_calc_type_;
      base_info->calc_id_type_ = calc_id_type_;
    }
  }
  return ret;
}

int ObExprCalcPartitionBase::calc_result_typeN(ObExprResType &type,
                                               ObExprResType *types_array,
                                               int64_t param_num,
                                               common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(types_array);
  UNUSED(param_num);
  UNUSED(type_ctx);
  if (CALC_PARTITION_TABLET_ID == get_calc_id_type()) {
    type.set_binary();
    type.set_length(sizeof(uint64_t) * 2);
  } else {
    type.set_int();
    type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].precision_);
    type.set_scale(DEFAULT_SCALE_FOR_INTEGER);
  }
  return OB_SUCCESS;
}

int ObExprCalcPartitionBase::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                   const ObRawExpr &raw_expr,
                                   ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  ObTableID ref_table_id = reinterpret_cast<ObTableID>(raw_expr.get_ref_table_id());
  CalcPartitionBaseInfo *calc_part_info = NULL;
  const ObTableSchema *table_schema = NULL;
  if (OB_ISNULL(expr_cg_ctx.schema_guard_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (0 == ref_table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ref table id", K(ref_table_id), K(ret));
  } else if (OB_FAIL(expr_cg_ctx.schema_guard_->get_table_schema( ref_table_id, table_schema))) {
    LOG_WARN("fail to get table schema", K(ref_table_id), K(ret));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("Table not exist", K(ref_table_id), K(ret));
  } else if (OB_FAIL(init_calc_part_info(expr_cg_ctx.allocator_,
                                         *table_schema,
                                         raw_expr.get_partition_id_calc_type(),
                                         calc_part_info))) {
    LOG_WARN("fail to init tl expr info", K(ret));
  } else if (OB_ISNULL(calc_part_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fail to init tl expr info", K(ret), K(calc_part_info));
  } else {
    rt_expr.extra_info_ = calc_part_info;
    int64_t param_cnt = raw_expr.get_param_count();
    if (0 == param_cnt) {
      OB_ASSERT(PARTITION_LEVEL_ZERO == calc_part_info->part_level_);
      rt_expr.eval_func_ = ObExprCalcPartitionBase::calc_no_partition_location;
    } else if (1 == param_cnt) {
      OB_ASSERT(1 == rt_expr.arg_cnt_);
      OB_ASSERT(PARTITION_LEVEL_ONE == calc_part_info->part_level_);
      rt_expr.eval_func_ = ObExprCalcPartitionBase::calc_partition_level_one;
    } else if (2 == param_cnt) {
      OB_ASSERT(PARTITION_LEVEL_TWO == calc_part_info->part_level_);
      rt_expr.eval_func_ = ObExprCalcPartitionBase::calc_partition_level_two;
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid param cnt", K(ret), K(param_cnt));
    }
  }
  return ret;
}

int ObExprCalcPartitionBase::init_calc_part_info(ObIAllocator *allocator,
                                                 const ObTableSchema &table_schema,
                                                 PartitionIdCalcType calc_type,
                                                 CalcPartitionBaseInfo *&calc_part_info) const
{
  int ret = OB_SUCCESS;
  calc_part_info = NULL;
  CK(OB_NOT_NULL(allocator));
  if (OB_SUCC(ret)) {
    void *buf = allocator->alloc(sizeof(CalcPartitionBaseInfo));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory", K(ret));
    } else {
      calc_part_info = new(buf) CalcPartitionBaseInfo(*allocator, get_type());
      calc_part_info->ref_table_id_ = table_schema.get_table_id();
      calc_part_info->part_level_ = table_schema.get_part_level();
      calc_part_info->part_type_ = table_schema.get_part_option().get_part_func_type();
      calc_part_info->subpart_type_ = table_schema.get_sub_part_option().get_sub_part_func_type();
      calc_part_info->part_num_ = table_schema.get_first_part_num();
      calc_part_info->subpart_num_ = OB_INVALID_ID; // Currently not used, if used, need to consider heterogeneous number of secondary partitions
      calc_part_info->partition_id_calc_type_ = calc_type;
      calc_part_info->calc_id_type_ = get_calc_id_type();
      LOG_DEBUG("table location expr info", KPC(calc_part_info), K(ret));
    }
  }

  return ret;
}

int ObExprCalcPartitionBase::calc_no_partition_location(const ObExpr &expr,
                                                        ObEvalCtx &ctx,
                                                        ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  UNUSED(expr);
  ObDASTabletMapper tablet_mapper;
  ObSEArray<ObTabletID, 1> tablet_ids;
  ObSEArray<ObObjectID, 1> partition_ids;
  CalcPartitionBaseInfo *calc_part_info = reinterpret_cast<CalcPartitionBaseInfo *>(expr.extra_info_);
  if (OB_FAIL(ctx.exec_ctx_.get_das_ctx().get_das_tablet_mapper(calc_part_info->ref_table_id_,
                                                                tablet_mapper,
                                                                &calc_part_info->related_table_ids_))) {
    LOG_WARN("get das tablet mapper failed", K(ret), K(calc_part_info));
  } else if (OB_FAIL(tablet_mapper.get_non_partition_tablet_id(tablet_ids, partition_ids))) {
    LOG_WARN("fail to get non partition tablet id", K(ret));
  } else {
    if (CALC_TABLET_ID == calc_part_info->calc_id_type_) {
      if (0 == tablet_ids.count()) {
        res_datum.set_int(ObTabletID::INVALID_TABLET_ID);
      } else {
        res_datum.set_int(tablet_ids.at(0).id());
      }
    } else if (CALC_PARTITION_ID == calc_part_info->calc_id_type_) {
      if (0 == partition_ids.count()) {
        res_datum.set_int(OB_INVALID_ID);
      } else {
        res_datum.set_int(partition_ids.at(0));
      }
    } else if (CALC_PARTITION_TABLET_ID == calc_part_info->calc_id_type_) {
      if (OB_FAIL(concat_part_and_tablet_id(expr, ctx, res_datum,
                    (0 == partition_ids.count()) ? OB_INVALID_ID : partition_ids.at(0),
                    (0 == tablet_ids.count()) ? OB_INVALID_ID : tablet_ids.at(0).id()))) {
        LOG_WARN("fail to concat partition id and tablet id", K(ret));
      }
    }
  }

  return ret;
}

int ObExprCalcPartitionBase::calc_partition_level_one(const ObExpr &expr,
                                                      ObEvalCtx &ctx,
                                                      ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  CalcPartitionBaseInfo *calc_part_info = reinterpret_cast<CalcPartitionBaseInfo *>(expr.extra_info_);
  ObTabletID tablet_id(ObTabletID::INVALID_TABLET_ID);
  ObObjectID partition_id = OB_INVALID_ID;
  OZ (calc_partition_id(*expr.args_[0],
                        ctx,
                        *calc_part_info,
                        OB_INVALID_ID, /*first_part_id*/
                        tablet_id,
                        partition_id));
  if (OB_SUCC(ret)) {
    if (CALC_TABLET_ID == calc_part_info->calc_id_type_) {
      res_datum.set_int(tablet_id.id());
    } else if (CALC_PARTITION_ID == calc_part_info->calc_id_type_) {
      res_datum.set_int(partition_id);
    } else if (CALC_PARTITION_TABLET_ID == calc_part_info->calc_id_type_) {
      if (OB_FAIL(concat_part_and_tablet_id(expr, ctx, res_datum, partition_id, tablet_id.id()))) {
        LOG_WARN("fail to concat partition id and tablet id", K(ret));
      }
    }
  }
  return ret;
}

int ObExprCalcPartitionBase::calc_partition_level_two(const ObExpr &expr,
                                                      ObEvalCtx &ctx,
                                                      ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  OB_ASSERT(2 == expr.arg_cnt_);
  OB_ASSERT(nullptr != expr.extra_info_);
  CalcPartitionBaseInfo *calc_part_info = reinterpret_cast<CalcPartitionBaseInfo *>(expr.extra_info_);
  PartitionIdCalcType calc_type = calc_part_info->partition_id_calc_type_;
  ObObjectID first_part_id = OB_INVALID_ID;
  ObTabletID tablet_id(ObTabletID::INVALID_TABLET_ID);
  ObObjectID partition_id = OB_INVALID_ID;
  if (CALC_IGNORE_FIRST_PART == calc_type) {
    int64_t first_part_id = OB_INVALID_ID;
    if (OB_FAIL(get_first_part_id(ctx.exec_ctx_, expr, first_part_id))) {
      LOG_WARN("get first part id failed", K(ret));
    } else if (OB_FAIL(calc_partition_id(*expr.args_[1],
                                        ctx,
                                        *calc_part_info,
                                        first_part_id,
                                        tablet_id,
                                        partition_id))) {
      LOG_WARN("fail to calc partitoin id", K(ret));
    }
  } else if (CALC_IGNORE_SUB_PART == calc_type) {
    if (OB_FAIL(calc_partition_id(*expr.args_[0],
                                  ctx,
                                  *calc_part_info,
                                  OB_INVALID_ID, /*first_part_id*/
                                  tablet_id,
                                  partition_id))) {
      LOG_WARN("fail to calc partitoin id", K(ret));
    } else {
      // FIXME @YISHEN
      tablet_id = ObTabletID(partition_id);
    }
  } else if (OB_FAIL(calc_partition_id(*expr.args_[0],
                                       ctx,
                                       *calc_part_info,
                                       OB_INVALID_ID, /*first_part_id*/
                                       tablet_id,
                                       first_part_id))) {
    LOG_WARN("fail to calc partitoin id", K(ret));
  } else {
    if (OB_INVALID_ID == first_part_id) {
      // do nothing
    } else {
      if (OB_FAIL(calc_partition_id(*expr.args_[1],
                                    ctx,
                                    *calc_part_info,
                                    first_part_id,
                                    tablet_id,
                                    partition_id))) {
        LOG_WARN("fail to calc partitoin id", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (CALC_TABLET_ID == calc_part_info->calc_id_type_) {
      res_datum.set_int(tablet_id.id());
    } else if (CALC_PARTITION_ID == calc_part_info->calc_id_type_) {
      res_datum.set_int(partition_id);
    } else if (CALC_PARTITION_TABLET_ID == calc_part_info->calc_id_type_) {
      if (OB_FAIL(concat_part_and_tablet_id(expr, ctx, res_datum, partition_id, tablet_id.id()))) {
        LOG_WARN("fail to concat partition id and tablet id", K(ret));
      }
    }
  }

  return ret;
}

int ObExprCalcPartitionBase::concat_part_and_tablet_id(const ObExpr &expr,
                                                       ObEvalCtx &ctx,
                                                       ObDatum &res_datum,
                                                       uint64_t partition_id,
                                                       uint64_t tablet_id)
{
  int ret = OB_SUCCESS;
  uint64_t buf_len = sizeof(uint64_t) * 2;
  uint64_t *buf = reinterpret_cast<uint64_t *>(expr.get_str_res_mem(ctx, buf_len));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else {
    buf[0] = partition_id;
    buf[1] = tablet_id;
    res_datum.set_string(reinterpret_cast<char *>(buf), buf_len);
  }
  return ret;
}

int ObExprCalcPartitionBase::extract_part_and_tablet_id(const ObDatum &part_datum,
                                                        ObObjectID &part_id,
                                                        ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  const ObString &part_str = part_datum.get_string();
  if (part_str.length() < sizeof(uint64_t) * 2) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the partition string need 16 byte at least", K(ret));
  } else {
    const uint64_t *id_array = reinterpret_cast<const uint64_t*>(part_str.ptr());
    part_id = id_array[0];
    tablet_id = id_array[1];
  }
  return ret;
}

int ObExprCalcPartitionBase::calc_part_and_tablet_id(const ObExpr *calc_part_id,
                                                     ObEvalCtx &eval_ctx,
                                                     ObObjectID &partition_id,
                                                     ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObDatum *partition_id_datum = NULL;
  if (OB_ISNULL(calc_part_id) || !calc_part_id->datum_meta_.is_binary()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("calc part id is invalid", K(ret), KPC(calc_part_id));
  } else if (OB_FAIL(calc_part_id->eval(eval_ctx, partition_id_datum))) {
    LOG_WARN("calc part id expr failed", K(ret));
  } else if (OB_FAIL(extract_part_and_tablet_id(*partition_id_datum, partition_id, tablet_id))) {
    LOG_WARN("extract part and tablet id failed", K(ret));
  } else if (ObExprCalcPartitionId::NONE_PARTITION_ID == partition_id) {
    ret = OB_NO_PARTITION_FOR_GIVEN_VALUE;
    LOG_DEBUG("no partition matched", K(ret), KPC(calc_part_id), KPC(partition_id_datum));
  }
  return ret;
}

int ObExprCalcPartitionBase::calc_part_and_subpart_and_tablet_id(const ObExpr *calc_part_id,
                                                     ObEvalCtx &eval_ctx,
                                                     ObObjectID &partition_id,
                                                     ObObjectID &first_partition_id,
                                                     ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObDatum *partition_id_datum = NULL;
  if (OB_FAIL(calc_part_and_tablet_id(calc_part_id, eval_ctx, partition_id, tablet_id))) {
    LOG_WARN("failed to calc_part_and_tablet_id", K(ret));
  } else {
    // get first partition_id from table schema by partition_id
    CalcPartitionBaseInfo *calc_part_info = NULL;
    calc_part_info = reinterpret_cast<CalcPartitionBaseInfo *>(calc_part_id->extra_info_);
    if (OB_ISNULL(calc_part_info)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (calc_part_info->part_level_ == PARTITION_LEVEL_TWO &&
               calc_part_info->calc_id_type_ == CALC_PARTITION_TABLET_ID){
      // if table is part level two, only get partition_id(sub_partitionid)
      // need get first partition id from table schema here
      const ObTableSchema *table_schema = NULL;
      const ObPartition *part = NULL;
      const ObSubPartition *subpart = nullptr;
      if (OB_ISNULL(eval_ctx.exec_ctx_.get_sql_ctx())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null sql_ctx", K(ret));
      } else if (OB_FAIL(eval_ctx.exec_ctx_.get_sql_ctx()->schema_guard_->get_table_schema( calc_part_info->ref_table_id_, table_schema))) {
        LOG_WARN("get table schema failed", K(ret));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null table_schema", K(ret));
      } else if (OB_FAIL(table_schema->get_subpartition_by_sub_part_id(partition_id, part, subpart))) {
        LOG_WARN("fail to get partition", K(ret), K(partition_id));
      } else if (OB_ISNULL(part)) {
        ret = OB_ENTRY_NOT_EXIST;
        LOG_WARN("fail to get partition", K(ret), K(partition_id));
      } else {
        first_partition_id = part->get_part_id();
      }
    }
  }
  return ret;
}

int ObExprCalcPartitionBase::build_row(ObEvalCtx &ctx,
                                       ObIAllocator &allocator,
                                       const ObExpr &expr,
                                       ObNewRow &row)
{
  int ret = OB_SUCCESS;
  OB_ASSERT(T_OP_ROW == expr.type_);
  OB_ASSERT(expr.arg_cnt_ > 0);
  //TODO shengle Here the memory of the first allocated cells_ can be placed into expr_ctx,
  // Reuse for optimization;
  if (OB_ISNULL(row.cells_ = static_cast<ObObj *>(
                allocator.alloc(sizeof(ObObj) * expr.arg_cnt_)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", K(ret));
  } else {
    for (int64_t i = 0; i < expr.arg_cnt_; i++) {
      new (&row.cells_[i]) ObObj();
    }
    row.count_ = expr.arg_cnt_;
    row.projector_size_ = 0;
    row.projector_ = NULL;
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; i++) {
      ObExpr *col_expr = expr.args_[i];
      ObDatum &col_datum = col_expr->locate_expr_datum(ctx);
      if (OB_FAIL(col_datum.to_obj(row.cells_[i],
                                   col_expr->obj_meta_,
                                   col_expr->obj_datum_map_))) {
        LOG_WARN("convert datum to obj failed", K(ret));
      }
    }
  }

  return ret;
}

int ObExprCalcPartitionBase::calc_partition_id(const ObExpr &part_expr,
                                               ObEvalCtx &ctx,
                                               const CalcPartitionBaseInfo &calc_part_info,
                                               ObObjectID first_part_id,
                                               ObTabletID &tablet_id,
                                               ObObjectID &partition_id)
{
  int ret = OB_SUCCESS;
  ObSqlCtx *sql_ctx = ctx.exec_ctx_.get_sql_ctx();
  tablet_id.reset();
  partition_id = OB_INVALID_ID;
  ObPartitionLevel part_level = (OB_INVALID_ID == first_part_id)
                                ? PARTITION_LEVEL_ONE : PARTITION_LEVEL_TWO;
  ObPartitionFuncType part_type = (PARTITION_LEVEL_ONE == part_level)
                                  ? calc_part_info.part_type_ : calc_part_info.subpart_type_;
  ObDASTabletMapper tablet_mapper;
  if (OB_FAIL(ctx.exec_ctx_.get_das_ctx().get_das_tablet_mapper(calc_part_info.ref_table_id_,
                                                                tablet_mapper,
                                                                &calc_part_info.related_table_ids_))) {
    LOG_WARN("get das tablet mapper failed", K(ret), K(calc_part_info));
  } else if (T_OP_ROW == part_expr.type_) {
    ObDatum *tmp_datum = NULL;
    // Here we pre-calculate the expr child value, rather than calling eval directly in build row,
    // is to avoid eval calculation using reset tmp alloc, affecting the allocation of row below
    // cell memory usage for reset tmp alloc
    for (int64_t i = 0; OB_SUCC(ret) && i < part_expr.arg_cnt_; i++) {
      if (OB_FAIL(part_expr.args_[i]->eval(ctx, tmp_datum))) {
        LOG_WARN("fail to eval part expr", K(ret), K(part_expr));
      }
    }
    if (OB_SUCC(ret)) {
      ObNewRow row;
      ObEvalCtx::TempAllocGuard alloc_guard(ctx);
      ObIAllocator &allocator = alloc_guard.get_allocator();
      if (OB_FAIL(build_row(ctx, allocator, part_expr, row))) {
        LOG_WARN("fail to build row", K(ret));
      } else if (OB_FAIL(tablet_mapper.get_tablet_and_object_id(
                                               part_level,
                                               first_part_id,
                                               row,
                                               tablet_id,
                                               partition_id))) {
        LOG_WARN("Failed to get part id", K(ret), K(row));
      }
    }
  } else { // not list/range columns
    ObObj func_value;
    ObObj result;
    ObDatum *datum = NULL;
    if (OB_FAIL(part_expr.eval(ctx, datum))) {
      LOG_WARN("part expr evaluate failed", K(ret));
    } else if (OB_FAIL(datum->to_obj(func_value,
                                     part_expr.obj_meta_,
                                     part_expr.obj_datum_map_))) {
      LOG_WARN("convert datum to obj failed", K(ret));
    } else if (func_value.is_outrow_lob()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "outrow lob as partition key");
      LOG_WARN("outrow lob as partition key is not supported", K(ret));
    } else {
      result = func_value;
      if (PARTITION_FUNC_TYPE_HASH == part_type) {
        if (OB_FAIL(ObExprFuncPartHash::calc_value_for_mysql(func_value, result,
                    func_value.get_type()))) {
          LOG_WARN("Failed to calc hash value mysql mode", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        ObSEArray<ObTabletID, 1> tablet_ids;
        ObSEArray<ObObjectID, 1> partition_ids;
        // Here you can also uniformly use the above ObNewRow interface, and put calc_value_for_mysql
        // Use datum to implement, temporarily keep consistent with the previous method
        ObRowkey rowkey(const_cast<ObObj*>(&result), 1);
        ObNewRange range;
        if (OB_FAIL(range.build_range(calc_part_info.ref_table_id_, rowkey))) {
          LOG_WARN("Failed to build range", K(ret));
        } else if (OB_FAIL(tablet_mapper.get_tablet_and_object_id(
                                          part_level,
                                          first_part_id,
                                          range,
                                          tablet_ids,
                                          partition_ids))) {
          LOG_WARN("Failed to get part id", K(ret));
        } else if (partition_ids.count() != 0 && partition_ids.count() != 1) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid partition cnt", K(ret), K(part_expr), K(partition_ids), K(range), K(rowkey));
        } else {
          if (OB_SUCC(ret) && 1 == partition_ids.count()) {
            partition_id = partition_ids.at(0);
            if (1 == tablet_ids.count()) {
              tablet_id = tablet_ids.at(0);
            }
          }
        }
      }
    }
  }

  return ret;
}

//calc partition id
ObExprCalcPartitionId::ObExprCalcPartitionId(ObIAllocator &alloc)
    : ObExprCalcPartitionBase(alloc,
                              T_FUN_SYS_CALC_PARTITION_ID,
                              N_CALC_PARTITION_ID,
                              PARAM_NUM_UNKNOWN,
                              NOT_ROW_DIMENSION)
{
}

ObExprCalcPartitionId::~ObExprCalcPartitionId()
{
}

//calc tablet id
ObExprCalcTabletId::ObExprCalcTabletId(ObIAllocator &alloc)
    : ObExprCalcPartitionBase(alloc,
                              T_FUN_SYS_CALC_TABLET_ID,
                              N_CALC_TABLET_ID,
                              PARAM_NUM_UNKNOWN,
                               NOT_ROW_DIMENSION)
{
}

ObExprCalcTabletId::~ObExprCalcTabletId()
{
}

//calc partition id and tablet id
ObExprCalcPartitionTabletId::ObExprCalcPartitionTabletId(ObIAllocator &alloc)
    : ObExprCalcPartitionBase(alloc,
                              T_FUN_SYS_CALC_PARTITION_TABLET_ID,
                              N_CALC_PARTITION_TABLET_ID,
                              PARAM_NUM_UNKNOWN,
                              NOT_ROW_DIMENSION)
{
}

ObExprCalcPartitionTabletId::~ObExprCalcPartitionTabletId()
{
}

bool PartValKey::operator==(const PartValKey &other) const
{
  int res = true;
  cmp_func_(datum_, other.datum_, res, datum_access_ctx_);
  return res == 0;
}

int PartValKey::hash(uint64_t &hash_val, uint64_t seed) const
{
  return hash_func_(datum_, seed, hash_val, datum_access_ctx_);
}

bool RangePartCmp::operator()(const ObDatum &l, const RangePartition &r) {
  int cmp_ret = 0;
  bool res = false;

  if (r.is_max_range_part()) {
    res = true;
  } else if (l.is_null()) {
    // In part calc, MySQL treats null values as infinitely small.
    res = true;
  } else {
    ret_ = cmp_func_(l, r.datum_, cmp_ret, datum_access_ctx_);
    res = cmp_ret < 0;
  }
  return res;
}

int ObExprCalcPartitionBase::ObExprCalcPartCtx::init_calc_range_partition_base_info(
                                                const share::schema::ObTableSchema &table_schema,
                                                const ObExpr &part_expr,
                                                common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  const int64_t part_num = table_schema.get_partition_num();
  ObPartition * const* part_array = table_schema.get_part_array();
  range_partitions_.set_allocator(&allocator);
  if (OB_FAIL(range_partitions_.prepare_allocate(part_num))) {
    LOG_WARN("Fail to prepare_allocate", K(ret), K(part_num));
  }
  ObDatum tmp_datum;
  char buf[OBJ_DATUM_MAX_RES_SIZE];
  tmp_datum.ptr_ = buf;
  for (int i = 0; OB_SUCC(ret) && i < part_num; ++i) {
    if (part_array[i]->get_high_bound_val().is_max_row()) {
      range_partitions_.at(i).set_max_range_part();
    } else if (OB_FAIL(tmp_datum.from_obj(
            *(part_array[i]->get_high_bound_val().get_obj_ptr())))) {
      LOG_WARN("Fail to from obj", K(ret), K(i));
    } else if (OB_FAIL(range_partitions_.at(i).datum_.deep_copy(tmp_datum,
                                        allocator))) {
      LOG_WARN("failed to deep copy datum");
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(part_cmp_.cmp_func_ =
                      ObExprCmpFuncsHelper::get_datum_expr_cmp_func(
                          part_expr.datum_meta_.type_,
                          part_expr.datum_meta_.type_,
                          part_expr.datum_meta_.scale_,
                          part_expr.datum_meta_.scale_,
                          part_expr.datum_meta_.precision_,
                          part_expr.datum_meta_.precision_,
                          part_expr.datum_meta_.cs_type_,
                          part_expr.obj_meta_.has_lob_header()))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cmp_func is null", K(ret), K(part_expr.datum_meta_));
    } else {
      part_cmp_.datum_access_ctx_ = datum_access_ctx_;
    }
  }
  return ret;
}

int ObExprCalcPartitionBase::ObExprCalcPartCtx::init_calc_list_partition_base_info(
                                            const share::schema::ObTableSchema &table_schema,
                                            const ObExpr &part_expr,
                                            common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  const int64_t part_num = table_schema.get_partition_num();
  ObPartition * const* part_array = table_schema.get_part_array();
  int64_t list_val_cnt = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < part_num; ++i) {
    const ObIArray<common::ObNewRow> &list_row_values = 
            part_array[i]->get_list_row_values();
    list_val_cnt += list_row_values.count();
    // calc default value position
    if (list_row_values.count() == 1
    && list_row_values.at(0).get_count() >= 1
    && list_row_values.at(0).get_cell(0).is_max_value()) {  
      default_list_part_idx_ = i;
    }
  }
  if (OB_SUCC(ret)) {
    ObMemAttr list_part_map_attr("LISTPART");
    if (OB_FAIL(list_part_map_.create(list_val_cnt * 2,
                                  list_part_map_attr, list_part_map_attr))) {
      LOG_WARN("create interm_res hash table failed", K(ret));
    } else {
      ObDatum list_part_datum;
      char buf[OBJ_DATUM_MAX_RES_SIZE];
      list_part_datum.ptr_ = buf;
      for (int64_t i = 0; OB_SUCC(ret) && i < part_num; ++i) {
        if (i == default_list_part_idx_) {
          continue;
        }
        const ObIArray<common::ObNewRow> &list_row_values = 
              part_array[i]->get_list_row_values();
        for (int64_t j = 0; OB_SUCC(ret) && j < list_row_values.count(); ++j) {
          ObObj *list_part_obj = list_row_values.at(j).cells_;
          if (OB_FAIL(list_part_datum.from_obj(*list_part_obj))) {
            LOG_WARN("Fail to from obj", K(ret), K(i));
          } else {
            PartValKey list_part_row;
            if (OB_FAIL(list_part_row.datum_.deep_copy(list_part_datum,
                                        allocator))) {
              LOG_WARN("failed to deep copy datum");
            } else {
              list_part_row.hash_func_ = part_expr.basic_funcs_->murmur_hash_v2_;
              list_part_row.cmp_func_ = part_expr.basic_funcs_->null_first_cmp_;
              list_part_row.datum_access_ctx_ = datum_access_ctx_;
              if (OB_FAIL(list_part_map_.set_refactored(list_part_row, i))) {
                LOG_WARN("Fail to set_refactored", K(ret), K(i), K(j));
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObExprCalcPartitionBase::get_first_part_id(ObExecContext &ctx, const ObExpr &expr, int64_t &first_part_id)
{
  int ret = OB_SUCCESS;
  first_part_id = OB_INVALID_ID;
  uint64_t expr_ctx_id = static_cast<uint64_t>(expr.expr_ctx_id_);
  if (ObExpr::INVALID_EXP_CTX_ID == expr_ctx_id) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("partition expression context is missing", K(ret), K(expr_ctx_id));
  } else {
    ObExprCalcPartCtx *calc_part_ctx = NULL;
    if (OB_ISNULL(calc_part_ctx = static_cast<ObExprCalcPartCtx *>(ctx.get_expr_op_ctx(expr_ctx_id)))
        && OB_FAIL(ctx.create_expr_op_ctx(expr_ctx_id, calc_part_ctx))) {
      LOG_WARN("create expr op ctx failed", K(ret));
    } else {
      first_part_id = calc_part_ctx->first_part_id_;
    }
  }
  return ret;
}

int ObExprCalcPartitionBase::set_first_part_id(ObExecContext &ctx, const ObExpr &expr, const int64_t first_part_id)
{
  int ret = OB_SUCCESS;
  uint64_t expr_ctx_id = static_cast<uint64_t>(expr.expr_ctx_id_);
  if (ObExpr::INVALID_EXP_CTX_ID == expr_ctx_id) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("partition expression context is missing", K(ret), K(expr_ctx_id));
  } else {
    ObExprCalcPartCtx *calc_part_ctx = NULL;
    if (OB_ISNULL(calc_part_ctx = static_cast<ObExprCalcPartCtx *>(ctx.get_expr_op_ctx(expr_ctx_id)))
        && OB_FAIL(ctx.create_expr_op_ctx(expr_ctx_id, calc_part_ctx))) {
      LOG_WARN("create expr op ctx failed", K(ret));
    } else {
      calc_part_ctx->first_part_id_ = first_part_id;
    }
  }
  return ret;
}

}
}
