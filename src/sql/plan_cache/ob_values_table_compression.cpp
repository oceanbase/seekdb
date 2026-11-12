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
 
#define USING_LOG_PREFIX SQL_PC
#include "rust/config.h"
#include <vector>
#include "sql/plan_cache/ob_values_table_compression.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/engine/expr/ob_expr_version.h"

using namespace oceanbase::common;

namespace oceanbase
{

namespace sql
{
bool ObValuesTableCompression::is_support_compress_values_table(const ObString &stmt)
{
  return peg::classify_statement(
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(stmt.ptr()), stmt.length()),
      peg::SqlMode{}).supports_values_table;
}

int ObValuesTableCompression::add_raw_array_params(ObIAllocator &allocator,
                                                   ObPlanCacheCtx &pc_ctx,
                                                   const ObFastParserResult &fp_result,
                                                   const int64_t begin_param,
                                                   const int64_t row_count,
                                                   const int64_t param_count)
{
  int ret = OB_SUCCESS;
  if (begin_param + row_count * param_count > fp_result.raw_params_.count() ||
      row_count <= 0) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < param_count; ++i) {
      void *buf = nullptr;
      ObArrayPCParam *params_array = nullptr;
      if (OB_ISNULL(buf = allocator.alloc(sizeof(ObArrayPCParam)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        params_array = new(buf) ObArrayPCParam(allocator);
        params_array->set_capacity(row_count);
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < row_count; ++j) {
        int64_t param_idx = begin_param + j * param_count + i;
        if (OB_FAIL(params_array->push_back(fp_result.raw_params_.at(param_idx)))) {
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(pc_ctx.fp_result_.array_params_.push_back(params_array))) {
      }
    }
  }
  return ret;
}

int ObValuesTableCompression::try_batch_exec_params(ObIAllocator &allocator,
                                                    ObPlanCacheCtx &pc_ctx,
                                                    ObSQLSessionInfo &session_info,
                                                    ObFastParserResult &fp_result)
{
  int ret = OB_SUCCESS;
  bool can_fold_params = false;
  int64_t old_sql_pos = 0;
  int64_t new_sql_pos = 0;
  int64_t last_raw_param_idx = 0;
  int64_t total_delta_len = 0;
  ObString new_no_param_sql;
  ObSEArray<ObPCParam*, 4> temp_store;
  std::vector<peg::RestoreParameter> parameters;
  rust::Vec<peg::RestoredParameter> positions;
  ObPhysicalPlanCtx *phy_ctx = nullptr;
  const auto keep_parameters = [&](int64_t begin, int64_t end) {
    int ret = OB_SUCCESS;
    for (int64_t i = begin; OB_SUCC(ret) && i < end; ++i) {
      const ObPCParam *parameter = fp_result.raw_params_.at(i);
      if (OB_ISNULL(parameter) || OB_ISNULL(parameter->literal_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(temp_store.push_back(fp_result.raw_params_.at(i)))) {
      } else {
        const ObString &source = parameter->literal_->source;
        parameters.push_back({static_cast<size_t>(parameter->sql_position_),
            static_cast<size_t>(total_delta_len),
            rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(source.ptr()), source.length())});
      }
    }
    return ret;
  };
  if (pc_ctx.sql_ctx_.handle_batched_multi_stmt() ||
      session_info.is_inner() ||
      session_info.get_is_in_retry() ||
      fp_result.values_tokens_.empty() ||
      !config::_enable_values_table_folding()) {
  } else if (!is_support_compress_values_table(pc_ctx.raw_sql_)) {
  } else if (OB_ISNULL(phy_ctx = pc_ctx.exec_ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < fp_result.values_tokens_.count(); ++i) {
      bool is_valid = false;
      int64_t values_token_pos = fp_result.values_tokens_.at(i).no_param_sql_pos_;
      int64_t param_idx = fp_result.values_tokens_.at(i).param_idx_;
      int64_t batch_count = 0;
      int64_t param_count = 0;
      int64_t delta_len = 0;
      if (OB_FAIL(ObValuesTableCompression::parser_values_row_str(allocator, fp_result.pc_key_.name_,
                                            values_token_pos, new_no_param_sql, old_sql_pos,
                                            new_sql_pos, batch_count, param_count, delta_len,
                                            is_valid))) {
      } else if (!is_valid || param_count <= 0 || batch_count <= 1 || delta_len <= 0) {
      } else if (OB_FAIL(add_raw_array_params(allocator, pc_ctx, fp_result, param_idx, batch_count,
                                              param_count))) {
      } else if (OB_FAIL(keep_parameters(last_raw_param_idx, param_idx + param_count))) {
      } else if (OB_FAIL(phy_ctx->get_array_param_groups().push_back(
                     ObArrayParamGroup(batch_count, param_count, temp_store.count() - param_count)))) {
      } else {
        total_delta_len += delta_len;
        can_fold_params = true;
        last_raw_param_idx = param_idx + param_count * batch_count;
      }
    }
    if (OB_SUCC(ret) && can_fold_params) {
      if (OB_FAIL(keep_parameters(last_raw_param_idx, fp_result.raw_params_.count()))) {
      } else {
        auto restored = peg::restore_parameter_sql(
            rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(new_no_param_sql.ptr()),
                                      new_no_param_sql.length()),
            rust::Slice<const peg::RestoreParameter>(parameters.data(), parameters.size()), false);
        if (restored.error != peg::ParameterError::None) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(ob_write_string(allocator,
                       ObString(restored.sql.size(), reinterpret_cast<const char *>(restored.sql.data())),
                       pc_ctx.new_raw_sql_))) {
        } else {
          positions = std::move(restored.parameters);
        }
      }
    }
    if (ret != OB_SUCCESS) {
      phy_ctx->get_array_param_groups().reset();
      pc_ctx.fp_result_.array_params_.reset();
      pc_ctx.new_raw_sql_.reset();
      can_fold_params = false;
      ret = OB_SUCCESS;
    } else if (can_fold_params) {
      fp_result.pc_key_.name_ = new_no_param_sql;
      fp_result.raw_params_.reset();
      for (int64_t i = 0; i < temp_store.count(); i++) {
        temp_store.at(i)->sql_position_ = positions[i].sql_offset;
        temp_store.at(i)->source_offset_ = positions[i].source_offset;
      }
      if (OB_FAIL(fp_result.raw_params_.assign(temp_store))) {
      }
    }
  }
  return ret;
}

/* for choose plan, will resolve all params */
int ObValuesTableCompression::resolve_params_for_values_clause(ObPlanCacheCtx &pc_ctx,
                                                  const stmt::StmtType stmt_type,
                                                  const ObIArray<NotParamInfo> &not_param_info,
                                                  const ObIArray<ObCharsetType> &param_charset_type,
                                                  const ObBitSet<> &neg_param_index,
                                                  const ObBitSet<> &not_param_index,
                                                  const ObBitSet<> &must_be_positive_idx,
                                                  const ObBitSet<> &formalize_prec_idx,
                                                  ParamStore *&ab_params)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = pc_ctx.exec_ctx_.get_my_session();
  ObPhysicalPlanCtx *phy_ctx = pc_ctx.exec_ctx_.get_physical_plan_ctx();
  ObObjParam obj_param;
  ObObjParam array_param;
  void *ptr = NULL;
  int64_t raw_param_cnt = pc_ctx.fp_result_.raw_params_.count();
  int64_t raw_idx = 0;  // idx in pc_ctx.fp_result_.raw_params_
  int64_t array_param_idx = 0;  // idx in pc_ctx.fp_result_.array_params_
  int64_t not_param_cnt = 0;
  bool is_param = false;
  bool enable_decimal_int = false;
  bool enable_mysql_compatible_dates = false;
  if (OB_UNLIKELY(!pc_ctx.exec_ctx_.has_dynamic_values_table()) || OB_ISNULL(session) ||
      OB_ISNULL(phy_ctx) || OB_UNLIKELY(param_charset_type.count() != raw_param_cnt) ||
      OB_ISNULL(ab_params)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ab_params->reserve(raw_param_cnt))) {
  } else if (OB_FAIL(ObSQLUtils::check_enable_decimalint(session, enable_decimal_int))) {
  } else if (OB_FAIL(ObSQLUtils::check_enable_mysql_compatible_dates(session, false,
                          enable_mysql_compatible_dates))) {
  } else {
    ParamStore &phy_param_store = phy_ctx->get_param_store_for_update();
    ObIArray<ObArrayParamGroup> &array_param_groups = phy_ctx->get_array_param_groups();
    for (int64_t i = 0; OB_SUCC(ret) && i < array_param_groups.count(); ++i) {
      int64_t param_num = array_param_groups.at(i).column_count_;
      int64_t batch_num = array_param_groups.at(i).row_count_;
      int64_t array_idx = array_param_groups.at(i).start_param_idx_;
      if (OB_UNLIKELY(array_idx + param_num > raw_param_cnt)) {
        ret = OB_ERR_UNEXPECTED;
      }
      // 1.1 build params before batch group
      for (; OB_SUCC(ret) && raw_idx < array_idx; raw_idx++) {
        if (OB_FAIL(ObResolverUtils::resolver_param(pc_ctx, *session, phy_param_store, stmt_type,
                    param_charset_type.at(raw_idx), neg_param_index, not_param_index,
                    must_be_positive_idx, formalize_prec_idx, pc_ctx.fp_result_.raw_params_.at(raw_idx), raw_idx,
                    enable_mysql_compatible_dates, obj_param, is_param, enable_decimal_int))) {
        } else if (!is_param) {
          not_param_cnt++;
        } else if (OB_FAIL(ab_params->push_back(obj_param))) {
        }
      }
      if (OB_SUCC(ret)) {
        array_param_groups.at(i).start_param_idx_ -= not_param_cnt;
      }
      // 1.2 build array_param in batch group
      for (int64_t j = 0; OB_SUCC(ret) && j < param_num; j++, raw_idx++, array_param_idx++) {
        ObArrayPCParam *raw_array_param = pc_ctx.fp_result_.array_params_.at(array_param_idx);
        ObSEArray<ObExprResType, 4> res_types;
        ObSqlArrayObj *array_param_ptr = ObSqlArrayObj::alloc(pc_ctx.allocator_, batch_num);
        bool is_same = true;
        ObExprResType new_res_type;
        if (OB_ISNULL(array_param_ptr) || OB_ISNULL(raw_array_param)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          for (int64_t k = 0; OB_SUCC(ret) && k < batch_num; k++) {
            if (OB_FAIL(ObResolverUtils::resolver_param(pc_ctx, *session, phy_param_store, stmt_type,
                        param_charset_type.at(raw_idx), neg_param_index, not_param_index,
                        must_be_positive_idx, formalize_prec_idx, raw_array_param->at(k), raw_idx,
                        enable_mysql_compatible_dates, array_param_ptr->data_[k], is_param,
                        enable_decimal_int))) {
            } else {
              const ObObjParam &param = array_param_ptr->data_[k];
              ObExprResType res_type;
              res_type.set_meta(ObSQLUtils::is_empty_string_typed_null(param) ? param.get_param_meta() : param.get_meta());
              res_type.set_accuracy(param.get_accuracy());
              res_type.set_result_flag(param.get_result_flag());
              if (res_type.get_length() == -1) {
                if (res_type.is_varchar()) {
                  res_type.set_length(OB_MAX_EXTENDED_VARCHAR_LENGTH);
                } else if (res_type.is_char()) {
                  res_type.set_length(OB_MAX_EXTENDED_CHAR_LENGTH_BYTE);
                }
              }
              if (k == 0) {
                new_res_type = res_type;
                if (OB_FAIL(res_types.push_back(res_type))) {
                }
              } else if (k > 0 && is_same) {
                is_same = ObSQLUtils::is_same_type(res_type, res_types.at(0));
                if (!is_same) {
                  if (OB_FAIL(res_types.push_back(res_type))) {
                  }
                }
              } else {
                if (OB_FAIL(res_types.push_back(res_type))) {
                }
              }
            }
          }
          /* Derive type */
          if (OB_SUCC(ret) && !is_same) {
            new_res_type.reset();
            ObExprVersion dummy_op(pc_ctx.allocator_);
            ObExprTypeCtx type_ctx;
            ObSQLUtils::init_type_ctx(session, type_ctx);
            if (OB_FAIL(dummy_op.aggregate_result_type_for_merge(new_res_type,
                                                                  &res_types.at(0),
                                                                  res_types.count(),
                                                                  type_ctx))) {
            }
          }
          if (OB_SUCC(ret)) {
            array_param.reset();
            array_param_ptr->element_.set_meta_type(new_res_type.get_obj_meta());
            array_param_ptr->element_.set_accuracy(new_res_type.get_accuracy());
            array_param.set_extend(reinterpret_cast<int64_t>(array_param_ptr), T_EXT_SQL_ARRAY);
            array_param.set_param_meta();
            array_param.get_param_flag().is_batch_parameter_ = true;
            if (OB_FAIL(ab_params->push_back(array_param))) {
            }
          }
        }
      }
    }
    for (; OB_SUCC(ret) && raw_idx < raw_param_cnt; raw_idx++) {
      if (OB_FAIL(ObResolverUtils::resolver_param(pc_ctx, *session, phy_param_store, stmt_type,
                  param_charset_type.at(raw_idx), neg_param_index, not_param_index,
                  must_be_positive_idx, formalize_prec_idx, pc_ctx.fp_result_.raw_params_.at(raw_idx), raw_idx,
                  enable_mysql_compatible_dates, obj_param, is_param, enable_decimal_int))) {
      } else if (!is_param) {
        not_param_cnt++;
      } else if (OB_FAIL(ab_params->push_back(obj_param))) {
      }
    }
  }
  return ret;
}

/* after handle parser, only resolve array params */
int ObValuesTableCompression::resolve_params_for_values_clause(ObPlanCacheCtx &pc_ctx)
{
  int ret = OB_SUCCESS;
  const ObBitSet<> bit_set_dummy;
  ObSQLSessionInfo *session = pc_ctx.exec_ctx_.get_my_session();
  ObPhysicalPlanCtx *phy_ctx = pc_ctx.exec_ctx_.get_physical_plan_ctx();
  ObObjParam obj_param;
  ObObjParam array_param;
  int64_t raw_param_cnt = pc_ctx.fp_result_.raw_params_.count();
  int64_t raw_idx = 0;  // idx in pc_ctx.fp_result_.raw_params_
  int64_t array_param_idx = 0;  // idx in pc_ctx.fp_result_.array_params_
  bool is_param = false;
  bool enable_decimal_int = false;
  bool enable_mysql_compatible_dates = false;
  const ObIArray<ObCharsetType> &param_charset_type = pc_ctx.param_charset_type_;
  if (OB_UNLIKELY(!pc_ctx.exec_ctx_.has_dynamic_values_table()) || OB_ISNULL(session) ||
      OB_ISNULL(phy_ctx) || OB_UNLIKELY(param_charset_type.count() > raw_param_cnt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObSQLUtils::check_enable_decimalint(session, enable_decimal_int))) {
  } else if (OB_FAIL(ObSQLUtils::check_enable_mysql_compatible_dates(session, false,
                          enable_mysql_compatible_dates))) {
  } else {
    ParamStore &phy_param_store = phy_ctx->get_param_store_for_update();
    ObIArray<ObArrayParamGroup> &array_param_groups = phy_ctx->get_array_param_groups();
    int64_t not_param_offset = 0;
    int64_t tmp_raw_idx = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < array_param_groups.count(); i++) {
      int64_t array_idx = array_param_groups.at(i).start_param_idx_;
      while (OB_SUCC(ret) && tmp_raw_idx < array_idx && tmp_raw_idx < raw_param_cnt) {
        ObPCParam *pc_param = pc_ctx.fp_result_.raw_params_.at(tmp_raw_idx);
        if (OB_ISNULL(pc_param)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (pc_param->flag_ == NOT_PARAM) {
          not_param_offset++;
        }
        tmp_raw_idx++;
      }
      if (OB_SUCC(ret)) {
        if (OB_UNLIKELY(not_param_offset > array_idx)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          array_param_groups.at(i).start_param_idx_ -= not_param_offset;
        }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < array_param_groups.count(); ++i) {
      int64_t param_num = array_param_groups.at(i).column_count_;
      int64_t batch_num = array_param_groups.at(i).row_count_;
      int64_t raw_idx = array_param_groups.at(i).start_param_idx_;
      if (OB_UNLIKELY(raw_idx + param_num > raw_param_cnt) ||
          OB_UNLIKELY(raw_idx + param_num > phy_param_store.count())) {
        ret = OB_ERR_UNEXPECTED;
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < param_num; j++, raw_idx++, array_param_idx++) {
        ObArrayPCParam *raw_array_param = pc_ctx.fp_result_.array_params_.at(array_param_idx);
        ObSEArray<ObExprResType, 4> res_types;
        ObSqlArrayObj *array_param_ptr = ObSqlArrayObj::alloc(pc_ctx.allocator_, batch_num);
        bool is_same = true;
        ObExprResType new_res_type;
        if (OB_ISNULL(array_param_ptr) || OB_ISNULL(raw_array_param)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          for (int64_t k = 0; OB_SUCC(ret) && k < batch_num; k++) {
            if (OB_FAIL(ObResolverUtils::resolver_param(pc_ctx, *session, phy_param_store, stmt::T_SELECT,
                        param_charset_type.at(raw_idx), bit_set_dummy, bit_set_dummy,
                        bit_set_dummy, bit_set_dummy, raw_array_param->at(k), raw_idx, enable_mysql_compatible_dates,
                        array_param_ptr->data_[k], is_param, enable_decimal_int))) {
            } else {
              const ObObjParam &param = array_param_ptr->data_[k];
              ObExprResType res_type;
              res_type.set_meta(ObSQLUtils::is_empty_string_typed_null(param) ? param.get_param_meta() : param.get_meta());
              res_type.set_accuracy(param.get_accuracy());
              res_type.set_result_flag(param.get_result_flag());
              if (res_type.get_length() == -1) {
                if (res_type.is_varchar()) {
                  res_type.set_length(OB_MAX_EXTENDED_VARCHAR_LENGTH);
                } else if (res_type.is_char()) {
                  res_type.set_length(OB_MAX_EXTENDED_CHAR_LENGTH_BYTE);
                }
              }
              if (k == 0) {
                new_res_type = res_type;
                if (OB_FAIL(res_types.push_back(res_type))) {
                }
              } else if (k > 0 && is_same) {
                is_same = ObSQLUtils::is_same_type(res_type, res_types.at(0));
                if (!is_same) {
                  if (OB_FAIL(res_types.push_back(res_type))) {
                  }
                }
              } else {
                if (OB_FAIL(res_types.push_back(res_type))) {
                }
              }
            }
          }
          /* Derive type */
          if (OB_SUCC(ret) && !is_same) {
            new_res_type.reset();
            ObExprVersion dummy_op(pc_ctx.allocator_);
            ObExprTypeCtx type_ctx;
            ObSQLUtils::init_type_ctx(session, type_ctx);
            if (OB_FAIL(dummy_op.aggregate_result_type_for_merge(new_res_type,
                                                                        &res_types.at(0),
                                                                        res_types.count(),
                                                                        type_ctx))) {
            } else {
            }
          }
          if (OB_SUCC(ret)) {
            array_param.reset();
            array_param_ptr->element_.set_meta_type(new_res_type.get_obj_meta());
            array_param_ptr->element_.set_accuracy(new_res_type.get_accuracy());
            array_param.set_extend(reinterpret_cast<int64_t>(array_param_ptr), T_EXT_SQL_ARRAY);
            array_param.set_param_meta();
            array_param.get_param_flag().is_batch_parameter_ = true;
            phy_param_store.at(raw_idx) = array_param;
          }
        }
      }
    }
  }
  return ret;
}

int ObValuesTableCompression::parser_values_row_str(ObIAllocator &allocator,
                                                    const ObString &no_param_sql,
                                                    const int64_t values_token_pos,
                                                    ObString &new_no_param_sql,
                                                    int64_t &old_pos,
                                                    int64_t &new_pos,
                                                    int64_t &row_count,
                                                    int64_t &param_count,
                                                    int64_t &delta_length,
                                                    bool &can_batch_opt)
{
  row_count = param_count = delta_length = 0;
  can_batch_opt = false;
  const int64_t removed_length = old_pos - new_pos;
  if (values_token_pos < removed_length) return OB_SUCCESS;
  const ObString &input = new_no_param_sql.empty() ? no_param_sql : new_no_param_sql;
  const auto result = peg::compress_values(
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(input.ptr()), input.length()),
      values_token_pos - removed_length);
  if (!result.eligible) return OB_SUCCESS;
  char *buffer = static_cast<char *>(allocator.alloc(result.sql.size()));
  if (buffer == nullptr) return OB_ALLOCATE_MEMORY_FAILED;
  MEMCPY(buffer, result.sql.data(), result.sql.size());
  new_no_param_sql.assign_ptr(buffer, static_cast<int64_t>(result.sql.size()));
  old_pos = result.source_end + removed_length;
  new_pos = result.output_end;
  row_count = result.rows;
  param_count = result.row_parameters;
  delta_length = result.removed_length;
  can_batch_opt = true;
  return OB_SUCCESS;
}

}
}
