// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_SQL_PLUGIN_CANDIDATE_GRAPH_H_
#define SEEKDB_SQL_PLUGIN_CANDIDATE_GRAPH_H_
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstring>
#include "seekdb/plugin/server_dev_planner.h"
#include "sql/optimizer/ob_log_join.h"
#include "sql/optimizer/ob_log_sort.h"
#include "sql/optimizer/ob_log_group_by.h"
#include "sql/optimizer/ob_log_window_function.h"
#include "sql/optimizer/ob_log_distinct.h"
#include "sql/optimizer/ob_log_function_table.h"
#include "sql/optimizer/ob_log_table_scan.h"
#include "sql/optimizer/log_plugin_custom.h"
#include "sql/resolver/expr/plugin_expr_type.h"
#include "sql/resolver/dml/ob_select_stmt.h"

namespace oceanbase { namespace sql {
// Invocation-owned handles, never raw pointers in the plugin ABI. Do not call
// get_op_exprs here: several implementations allocate/modify planner state.
class CandidateGraph {
public:
  seekdb_plugin_status_t value_info(uint32_t plan, uint32_t expression,
      seekdb_plugin_value_info_v1_t *out) {
    const bool valid = out && out->struct_size == sizeof(*out);
    if (out) *out = {};
    return run([&] {
      if (!valid || plan >= nodes_.size() || expression >= expressions_.size()) return common::OB_INVALID_ARGUMENT;
      auto *expr = expressions_[expression];
      seekdb_plugin_value_info_v1_t info{}; info.struct_size = sizeof(info);
      if (expr->is_op_expr() || expr->is_case_op_expr() || expr->is_sys_func_expr())
        info.flags |= SEEKDB_PLUGIN_VALUE_SCALAR_ARGUMENTS;
      const auto contains = [&](const auto &values) {
        for (int64_t i = 0; i < values.count(); ++i) if (values.at(i) == expr) return true;
        return false;
      };
      auto *owner = nodes_[plan]->get_plan();
      std::vector<ObLogicalOperator *> work{nodes_[plan]};
      std::unordered_set<ObLogicalOperator *> seen;
      bool available = false;
      while (!available && !work.empty()) {
        auto *node = work.back(); work.pop_back();
        if (!node || node->get_plan() != owner) return common::OB_INVALID_ARGUMENT;
        if (!seen.insert(node).second) return common::OB_INVALID_DATA;
        if (seen.size() > 4096) return common::OB_SIZE_OVERFLOW;
        bool pass = false;
        switch (node->get_type()) {
          case log_op_def::LOG_GROUP_BY: {
            auto &group = static_cast<ObLogGroupBy &>(*node);
            available = contains(group.get_group_by_exprs()) || contains(group.get_rollup_exprs()) ||
                contains(group.get_aggr_funcs());
            break; // Never recover an ungrouped input column through GROUP.
          }
          case log_op_def::LOG_DISTINCT:
            available = contains(static_cast<ObLogDistinct &>(*node).get_distinct_exprs());
            break;
          case log_op_def::LOG_WINDOW_FUNCTION:
            available = contains(static_cast<ObLogWindowFunction &>(*node).get_window_exprs());
            pass = true; break;
          case log_op_def::LOG_PLUGIN_CUSTOM: {
            auto &custom = static_cast<LogPluginCustom &>(*node);
            if (custom.explicit_layout()) available = contains(custom.result_exprs());
            else pass = true;
            break;
          }
          case log_op_def::LOG_SORT:
          case log_op_def::LOG_MATERIAL:
          case log_op_def::LOG_LIMIT:
            pass = true; break;
          case log_op_def::LOG_TABLE_SCAN:
          case log_op_def::LOG_FUNCTION_TABLE: {
            if (expr->is_column_ref_expr() && node->get_stmt()) {
              const auto table = node->get_type() == log_op_def::LOG_TABLE_SCAN ?
                  static_cast<ObLogTableScan &>(*node).get_table_id() :
                  static_cast<ObLogFunctionTable &>(*node).get_table_id();
              // Access arrays may not be allocated at this planning stage.
              // Require the exact resolved column in this query block.
              if (static_cast<ObColumnRefRawExpr *>(expr)->get_table_id() == table)
                for (int64_t i = 0; i < node->get_stmt()->get_column_size(); ++i)
                  available |= node->get_stmt()->get_column_items().at(i).expr_ == expr;
            }
            break;
          }
          case log_op_def::LOG_JOIN: {
            if (node->get_num_of_child() != 2) return common::OB_INVALID_DATA;
            const auto kind = static_cast<ObLogJoin &>(*node).get_join_type();
            if (kind == INNER_JOIN || (expr->is_column_ref_expr() &&
                (kind == LEFT_OUTER_JOIN || kind == RIGHT_OUTER_JOIN || kind == FULL_OUTER_JOIN))) {
              work.push_back(node->get_child(0)); work.push_back(node->get_child(1));
            } else if (kind == LEFT_SEMI_JOIN || kind == LEFT_ANTI_JOIN) work.push_back(node->get_child(0));
            else if (kind == RIGHT_SEMI_JOIN || kind == RIGHT_ANTI_JOIN) work.push_back(node->get_child(1));
            break;
          }
          default: break;
        }
        if (!available && pass) {
          if (node->get_num_of_child() != 1) return common::OB_INVALID_DATA;
          work.push_back(node->get_child(0));
        }
      }
      if (available) info.flags |= SEEKDB_PLUGIN_VALUE_AVAILABLE;
      *out = info; return common::OB_SUCCESS;
    });
  }
  seekdb_plugin_status_t sort_info(uint32_t id, seekdb_plugin_sort_info_v1_t *out) {
    const bool valid = out && out->struct_size == sizeof(*out);
    if (out) *out = {};
    return run([&] {
      if (!valid || id >= nodes_.size()) return common::OB_INVALID_ARGUMENT;
      seekdb_plugin_sort_info_v1_t info{}; info.struct_size = sizeof(info);
      info.topn_expression = info.topk_limit_expression = info.topk_offset_expression =
          info.hash_expression = UINT32_MAX;
      if (nodes_[id]->get_type() == log_op_def::LOG_SORT) {
        auto &sort = static_cast<ObLogSort &>(*nodes_[id]);
        const int64_t count = sort.get_sort_keys().count();
        if (count < 0 || count > UINT32_MAX || sort.get_prefix_pos() < 0 ||
            sort.get_prefix_pos() > count || sort.get_part_cnt() < 0 || sort.get_part_cnt() > count)
          return common::OB_INVALID_DATA;
        info.flags = SEEKDB_PLUGIN_SORT_PRESENT |
            (sort.enable_encode_sortkey_opt() ? SEEKDB_PLUGIN_SORT_ENCODED_KEYS : 0) |
            (sort.is_local_merge_sort() ? SEEKDB_PLUGIN_SORT_LOCAL_MERGE : 0) |
            (sort.is_fetch_with_ties() ? SEEKDB_PLUGIN_SORT_WITH_TIES : 0) |
            (sort.enable_pd_topn_filter() ? SEEKDB_PLUGIN_SORT_RUNTIME_FILTER : 0);
        info.key_count = count; info.prefix_key_count = sort.get_prefix_pos();
        info.partition_key_count = sort.get_part_cnt();
        ObRawExpr *exprs[] = {sort.get_topn_expr(), sort.get_topk_limit_expr(),
            sort.get_topk_offset_expr(), sort.get_hash_sortkey().expr_};
        uint32_t *ids[] = {&info.topn_expression, &info.topk_limit_expression,
            &info.topk_offset_expression, &info.hash_expression};
        for (int i = 0; i < 4; ++i) if (exprs[i]) {
          const int ret = intern(exprs[i], ids[i], expressions_, expression_ids_, 16384);
          if (ret != common::OB_SUCCESS) return ret;
        }
      }
      *out = info; return common::OB_SUCCESS;
    });
  }
  seekdb_plugin_status_t sort_key(uint32_t id, uint32_t index, uint32_t *expr, uint32_t *flags) {
    if (expr) *expr = UINT32_MAX;
    if (flags) *flags = 0;
    return run([&] {
      if (!expr || !flags || expr == flags || id >= nodes_.size() ||
          nodes_[id]->get_type() != log_op_def::LOG_SORT) return common::OB_INVALID_ARGUMENT;
      const auto &keys = static_cast<ObLogSort &>(*nodes_[id]).get_sort_keys();
      if (index >= keys.count()) return common::OB_INVALID_ARGUMENT;
      uint32_t normalized = 0;
      switch (keys.at(index).order_type_) {
        case NULLS_FIRST_ASC: normalized = SEEKDB_PLUGIN_SORT_KEY_NULLS_FIRST; break;
        case NULLS_LAST_ASC: break;
        case NULLS_FIRST_DESC: normalized = SEEKDB_PLUGIN_SORT_KEY_DESC | SEEKDB_PLUGIN_SORT_KEY_NULLS_FIRST; break;
        case NULLS_LAST_DESC: normalized = SEEKDB_PLUGIN_SORT_KEY_DESC; break;
        default: return common::OB_INVALID_DATA;
      }
      const int ret = intern(keys.at(index).expr_, expr, expressions_, expression_ids_, 16384);
      if (ret == common::OB_SUCCESS) *flags = normalized;
      return ret;
    });
  }
  seekdb_plugin_status_t binding_count(uint32_t plan, uint32_t role, uint32_t *out) {
    if (out) *out = 0;
    return run([&] {
      if (!out) return common::OB_INVALID_ARGUMENT;
      const common::ObIArray<ObExecParamRawExpr *> *bindings = nullptr;
      int ret = parameter_bindings(plan, role, bindings);
      if (ret != common::OB_SUCCESS) return ret;
      if (bindings && bindings->count() > UINT32_MAX) return common::OB_SIZE_OVERFLOW;
      *out = bindings ? static_cast<uint32_t>(bindings->count()) : 0;
      return common::OB_SUCCESS;
    });
  }
  seekdb_plugin_status_t binding(uint32_t plan, uint32_t role, uint32_t index,
      uint32_t *parameter, uint32_t *value) {
    if (parameter) *parameter = UINT32_MAX;
    if (value) *value = UINT32_MAX;
    return run([&] {
      if (!parameter || !value || parameter == value) return common::OB_INVALID_ARGUMENT;
      const common::ObIArray<ObExecParamRawExpr *> *bindings = nullptr;
      int ret = parameter_bindings(plan, role, bindings);
      if (ret != common::OB_SUCCESS) return ret;
      if (!bindings || index >= bindings->count() || !bindings->at(index) ||
          !bindings->at(index)->get_ref_expr()) return common::OB_INVALID_ARGUMENT;
      uint32_t p = UINT32_MAX, v = UINT32_MAX;
      ret = intern(static_cast<ObRawExpr *>(bindings->at(index)), &p, expressions_, expression_ids_, 16384);
      if (ret == common::OB_SUCCESS)
        ret = intern(bindings->at(index)->get_ref_expr(), &v, expressions_, expression_ids_, 16384);
      if (ret == common::OB_SUCCESS) { *parameter = p; *value = v; }
      return ret;
    });
  }
  seekdb_plugin_status_t plan_semantics(uint32_t id, seekdb_plugin_plan_semantics_v1_t *out) {
    const bool valid = out && out->struct_size == sizeof(*out);
    if (out) *out = {};
    return run([&] {
      if (!valid || id >= nodes_.size()) return common::OB_INVALID_ARGUMENT;
      auto &node = *nodes_[id];
      seekdb_plugin_plan_semantics_v1_t info{}; info.struct_size = sizeof(info);
      if ((node.is_local() || node.is_match_all()) && node.get_parallel() == 1)
        info.flags = SEEKDB_PLUGIN_PLAN_LOCAL_SERIAL;
      if (node.get_type() == log_op_def::LOG_JOIN) {
        switch (static_cast<ObLogJoin &>(node).get_join_type()) {
          case INNER_JOIN: info.relation_kind = SEEKDB_PLUGIN_RELATION_INNER_JOIN; break;
          case LEFT_OUTER_JOIN: info.relation_kind = SEEKDB_PLUGIN_RELATION_LEFT_JOIN; break;
          case RIGHT_OUTER_JOIN: info.relation_kind = SEEKDB_PLUGIN_RELATION_RIGHT_JOIN; break;
          case FULL_OUTER_JOIN: info.relation_kind = SEEKDB_PLUGIN_RELATION_FULL_JOIN; break;
          case LEFT_SEMI_JOIN: info.relation_kind = SEEKDB_PLUGIN_RELATION_LEFT_SEMI; break;
          case RIGHT_SEMI_JOIN: info.relation_kind = SEEKDB_PLUGIN_RELATION_RIGHT_SEMI; break;
          case LEFT_ANTI_JOIN: info.relation_kind = SEEKDB_PLUGIN_RELATION_LEFT_ANTI; break;
          case RIGHT_ANTI_JOIN: info.relation_kind = SEEKDB_PLUGIN_RELATION_RIGHT_ANTI; break;
          default: break;
        }
      }
      *out = info; return common::OB_SUCCESS;
    });
  }
  seekdb_plugin_status_t expression_semantics(uint32_t id, seekdb_plugin_expr_semantics_v1_t *out) {
    const bool valid = out && out->struct_size == sizeof(*out);
    if (out) *out = {};
    return run([&] {
      if (!valid || id >= expressions_.size()) return common::OB_INVALID_ARGUMENT;
      const auto &expr = *expressions_[id];
      seekdb_plugin_expr_semantics_v1_t info{}; info.struct_size = sizeof(info);
      if (expr.get_expr_type() == T_OP_EQ) info.comparison_kind = SEEKDB_PLUGIN_COMPARE_EQUAL;
      else if (expr.get_expr_type() == T_OP_NSEQ) info.comparison_kind = SEEKDB_PLUGIN_COMPARE_NULL_SAFE_EQUAL;
      // Table-function columns also carry logical IDs for built-in types.
      // Accept only the exact core identities with native, non-stored values;
      // an integer carrier alone says nothing about a custom type's ordering.
      const auto *logical = expr.get_plugin_type();
      bool builtin = logical == nullptr;
      if (logical && !logical->stored_ && logical->physical_type_ == expr.get_data_type()) {
        for (const char *id : {"core.type.bool", "core.type.int32", "core.type.uint32",
                              "core.type.int64", "core.type.uint64"})
          builtin |= logical->logical_id_ == common::ObString::make_string(id);
      }
      if (builtin && common::ob_is_integer_type(expr.get_data_type()))
        info.value_kind = common::ob_is_unsigned_type(expr.get_data_type()) ?
            SEEKDB_PLUGIN_VALUE_UNSIGNED_INTEGER : SEEKDB_PLUGIN_VALUE_SIGNED_INTEGER;
      bool scalar = true;
      std::vector<const ObRawExpr *> work{&expr};
      std::unordered_set<const ObRawExpr *> seen;
      while (scalar && !work.empty()) {
        const auto *part = work.back(); work.pop_back();
        if (!part) return common::OB_INVALID_DATA;
        if (!seen.insert(part).second) continue;
        if (seen.size() > 16384) return common::OB_SIZE_OVERFLOW;
        bool impure = false;
        const int ret = part->is_non_pure_sys_func_expr(impure);
        if (ret != common::OB_SUCCESS) return ret;
        scalar = (part->is_const_expr() || part->is_column_ref_expr() || part->is_op_expr() ||
            part->is_case_op_expr() || part->is_sys_func_expr()) &&
            part->is_deterministic() && part->check_is_deterministic_expr() && !impure &&
            !part->has_flag(CNT_AGG) && !part->has_flag(CNT_WINDOW_FUNC) && !part->has_flag(CNT_SUB_QUERY) &&
            !part->has_flag(CNT_DYNAMIC_PARAM) && !part->has_flag(CNT_ONETIME) && !part->has_flag(CNT_PL_UDF) &&
            !part->has_flag(CNT_PSEUDO_COLUMN) && !part->has_flag(CNT_OP_PSEUDO_COLUMN) &&
            !part->has_flag(CNT_CUR_TIME) && !part->has_flag(CNT_SET_OP) && !part->has_flag(CNT_ALIAS);
        for (int64_t i = 0; scalar && i < part->get_param_count(); ++i) work.push_back(part->get_param_expr(i));
      }
      if (scalar) info.flags = SEEKDB_PLUGIN_EXPR_SCALAR_DETERMINISTIC;
      *out = info; return common::OB_SUCCESS;
    });
  }
  seekdb_plugin_status_t scope(const ObDMLStmt *stmt, uint32_t plan, uint32_t expression, uint32_t *out) {
    if (out) *out = UINT32_MAX;
    return run([&] {
      if (!out || !stmt || plan >= nodes_.size() || expression >= expressions_.size()) return common::OB_INVALID_ARGUMENT;
      const auto &ids = expressions_[expression]->get_relation_ids();
      *out = nodes_[plan]->get_stmt() != stmt ? SEEKDB_PLUGIN_SCOPE_OUTSIDE : ids.is_empty() ?
          SEEKDB_PLUGIN_SCOPE_INDEPENDENT : ids.is_subset(nodes_[plan]->get_table_set()) ?
          SEEKDB_PLUGIN_SCOPE_CONTAINED : SEEKDB_PLUGIN_SCOPE_OUTSIDE;
      return common::OB_SUCCESS;
    });
  }
  seekdb_plugin_status_t column_count(const ObDMLStmt *stmt, uint32_t *out) {
    if (out) *out = 0;
    return run([&] {
      if (!stmt || !out) return common::OB_INVALID_ARGUMENT;
      if (stmt->get_column_size() < 0 || stmt->get_column_size() > UINT32_MAX) return common::OB_SIZE_OVERFLOW;
      *out = stmt->get_column_size(); return common::OB_SUCCESS;
    });
  }
  seekdb_plugin_status_t column(const ObDMLStmt *stmt, uint32_t index, uint32_t *out) {
    if (out) *out = UINT32_MAX;
    return run([&] {
      if (!stmt || index >= stmt->get_column_size()) return common::OB_INVALID_ARGUMENT;
      return intern(static_cast<ObRawExpr *>(stmt->get_column_items().at(index).expr_), out, expressions_, expression_ids_, 16384);
    });
  }
  int error() const { return error_; }
private:
  int parameter_bindings(uint32_t plan, uint32_t role,
      const common::ObIArray<ObExecParamRawExpr *> *&out) {
    out = nullptr;
    if (plan >= nodes_.size() || role < SEEKDB_PLUGIN_BIND_JOIN_NESTED_LOOP ||
        role > SEEKDB_PLUGIN_BIND_JOIN_RIGHT_PUSH_DOWN) return common::OB_INVALID_ARGUMENT;
    auto *node = nodes_[plan];
    if (node->get_type() == log_op_def::LOG_JOIN) {
      auto &join = static_cast<ObLogJoin &>(*node);
      out = role == SEEKDB_PLUGIN_BIND_JOIN_NESTED_LOOP ? &join.get_nl_params() :
          role == SEEKDB_PLUGIN_BIND_JOIN_LEFT_PUSH_DOWN ? &join.get_above_pushdown_left_params() :
          &join.get_above_pushdown_right_params();
    }
    return common::OB_SUCCESS;
  }
public:
  int resolve_plan(uint32_t id, ObLogicalOperator *&out) {
    out = nullptr;
    if (error_ == common::OB_SUCCESS && id >= nodes_.size()) error_ = common::OB_INVALID_ARGUMENT;
    if (error_ == common::OB_SUCCESS) out = nodes_[id];
    return error_;
  }
  seekdb_plugin_status_t query(const ObDMLStmt *stmt, seekdb_plugin_query_info_v1_t *out) {
    const bool valid = out && out->struct_size == sizeof(*out);
    if (out) *out = {};
    return run([&] {
      if (!valid || !stmt) return common::OB_INVALID_ARGUMENT;
      seekdb_plugin_query_info_v1_t info{};
      info.struct_size = sizeof(info); info.statement_type = stmt->get_stmt_type();
      if (stmt->is_select_stmt()) {
        const auto &select = static_cast<const ObSelectStmt &>(*stmt);
        if (select.get_select_item_size() < 0 || select.get_select_item_size() > UINT32_MAX)
          return common::OB_SIZE_OVERFLOW;
        info.flags = SEEKDB_PLUGIN_QUERY_SELECT_LIST;
        if (select.is_set_stmt()) info.flags |= SEEKDB_PLUGIN_QUERY_SET_OPERATION;
        info.target_count = select.get_select_item_size();
      }
      *out = info; return common::OB_SUCCESS;
    });
  }
  seekdb_plugin_status_t target(const ObDMLStmt *stmt, uint32_t index, uint32_t *out) {
    if (out) *out = UINT32_MAX;
    return run([&] {
      if (!stmt || !stmt->is_select_stmt()) return common::OB_INVALID_ARGUMENT;
      const auto &select = static_cast<const ObSelectStmt &>(*stmt);
      if (index >= select.get_select_item_size()) return common::OB_INVALID_ARGUMENT;
      return intern(select.get_select_item(index).expr_, out, expressions_, expression_ids_, 16384);
    });
  }
  int resolve_expression(uint32_t id, ObRawExpr *&out) {
    out = nullptr;
    if (error_ == common::OB_SUCCESS && id >= expressions_.size()) error_ = common::OB_INVALID_ARGUMENT;
    if (error_ == common::OB_SUCCESS) out = expressions_[id];
    return error_;
  }
  seekdb_plugin_status_t root(ObLogicalOperator *node, uint32_t *out) {
    if (out) *out = UINT32_MAX;
    return run([&] { return intern(node, out, nodes_, node_ids_, 4096); });
  }
  seekdb_plugin_status_t plan(uint32_t id, seekdb_plugin_plan_info_v1_t *out) {
    const bool valid = out && out->struct_size == sizeof(*out);
    if (out) *out = {};
    return run([&] {
      if (!valid || id >= nodes_.size()) return common::OB_INVALID_ARGUMENT;
      auto &node = *nodes_[id];
      seekdb_plugin_plan_info_v1_t info{};
      info.struct_size = sizeof(info); info.operator_type = node.get_type();
      info.child_count = node.get_num_of_child(); info.join_type = UINT32_MAX;
      if (node.get_type() == log_op_def::LOG_JOIN)
        info.join_type = static_cast<ObLogJoin &>(node).get_join_type();
      for (uint32_t role = 1; role <= 5; ++role) {
        const int64_t count = expression_count(node, role);
        if (count < 0 || count > UINT32_MAX) return common::OB_SIZE_OVERFLOW;
        info.expression_counts[role - 1] = count;
      }
      info.cost = node.get_cost(); info.rows = node.get_card(); info.width = node.get_width();
      *out = info; return common::OB_SUCCESS;
    });
  }
  seekdb_plugin_status_t child(uint32_t id, uint32_t index, uint32_t *out) {
    if (out) *out = UINT32_MAX;
    return run([&] {
      if (id >= nodes_.size() || index >= nodes_[id]->get_num_of_child()) return common::OB_INVALID_ARGUMENT;
      return intern(nodes_[id]->get_child(index), out, nodes_, node_ids_, 4096);
    });
  }
  seekdb_plugin_status_t expression(uint32_t id, uint32_t role, uint32_t index,
      uint32_t *out, uint32_t *ordering) {
    if (out) *out = UINT32_MAX;
    if (ordering) *ordering = UINT32_MAX;
    return run([&] {
      if (!ordering || id >= nodes_.size() || role < 1 || role > 5 ||
          index >= expression_count(*nodes_[id], role)) return common::OB_INVALID_ARGUMENT;
      auto &node = *nodes_[id];
      ObRawExpr *expr = nullptr;
      if (role == SEEKDB_PLUGIN_PLAN_ORDERING) expr = node.get_op_ordering().at(index).expr_;
      else expr = expressions(node, role)->at(index);
      const int ret = intern(expr, out, expressions_, expression_ids_, 16384);
      if (ret == common::OB_SUCCESS && role == SEEKDB_PLUGIN_PLAN_ORDERING)
        *ordering = node.get_op_ordering().at(index).order_type_;
      return ret;
    });
  }
  seekdb_plugin_status_t describe(uint32_t id, seekdb_plugin_expr_info_v1_t *out) {
    const bool valid = out && out->struct_size == sizeof(*out);
    if (out) *out = {};
    return run([&] {
      if (!valid || id >= expressions_.size()) return common::OB_INVALID_ARGUMENT;
      const auto &expr = *expressions_[id];
      if (expr.get_param_count() < 0 || expr.get_param_count() > UINT32_MAX) return common::OB_SIZE_OVERFLOW;
      seekdb_plugin_expr_info_v1_t info{};
      info.struct_size = sizeof(info); info.expression_type = expr.get_expr_type();
      info.sql_type = expr.get_data_type(); info.argument_count = expr.get_param_count();
      info.collation = expr.get_collation_type(); info.precision = info.scale = -1;
      if (common::ob_is_integer_type(expr.get_data_type()) || common::ob_is_float_type(expr.get_data_type()) ||
          common::ob_is_double_type(expr.get_data_type()) || common::ob_is_number_tc(expr.get_data_type())) {
        info.precision = expr.get_result_type().get_accuracy().get_precision();
        info.scale = expr.get_result_type().get_accuracy().get_scale();
      }
      if (expr.is_column_ref_expr()) {
        const auto &column = static_cast<const ObColumnRefRawExpr &>(expr);
        info.flags |= SEEKDB_PLUGIN_EXPR_COLUMN;
        info.table_id = column.get_table_id(); info.column_id = column.get_column_id();
      }
      if (expr.is_const_expr()) info.flags |= SEEKDB_PLUGIN_EXPR_CONSTANT;
      if (expr.is_not_null_for_read()) info.flags |= SEEKDB_PLUGIN_EXPR_NOT_NULL;
      if (const auto *type = expr.get_plugin_type()) {
        if (type->logical_id_.length() <= 0 || type->logical_id_.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
            !type->logical_id_.ptr() || std::memchr(type->logical_id_.ptr(), 0, type->logical_id_.length()))
          return common::OB_INVALID_DATA;
        info.flags |= SEEKDB_PLUGIN_EXPR_PLUGIN_TYPE;
        if (type->stored_) info.flags |= SEEKDB_PLUGIN_EXPR_STORED;
        std::memcpy(info.type_id, type->logical_id_.ptr(), type->logical_id_.length());
      }
      *out = info; return common::OB_SUCCESS;
    });
  }
  seekdb_plugin_status_t argument(uint32_t id, uint32_t index, uint32_t *out) {
    if (out) *out = UINT32_MAX;
    return run([&] {
      if (id >= expressions_.size() || index >= expressions_[id]->get_param_count()) return common::OB_INVALID_ARGUMENT;
      return intern(expressions_[id]->get_param_expr(index), out, expressions_, expression_ids_, 16384);
    });
  }
private:
  static common::ObIArray<ObRawExpr *> *expressions(ObLogicalOperator &node, uint32_t role) {
    switch (role) {
      case SEEKDB_PLUGIN_PLAN_FILTER: return &node.get_filter_exprs();
      case SEEKDB_PLUGIN_PLAN_STARTUP: return &node.get_startup_exprs();
      case SEEKDB_PLUGIN_PLAN_JOIN_CONDITION: return node.get_type() == log_op_def::LOG_JOIN ?
          &static_cast<ObLogJoin &>(node).get_join_conditions() : nullptr;
      case SEEKDB_PLUGIN_PLAN_JOIN_FILTER: return node.get_type() == log_op_def::LOG_JOIN ?
          &static_cast<ObLogJoin &>(node).get_join_filters() : nullptr;
      default: return nullptr;
    }
  }
  static int64_t expression_count(ObLogicalOperator &node, uint32_t role) {
    if (role == SEEKDB_PLUGIN_PLAN_ORDERING) return node.get_op_ordering().count();
    const auto *items = expressions(node, role);
    return items ? items->count() : 0;
  }
  template <typename T> static int intern(T *pointer, uint32_t *out, std::vector<T *> &items,
      std::unordered_map<T *, uint32_t> &ids, size_t limit) {
    if (!out || !pointer) return common::OB_INVALID_ARGUMENT;
    const auto found = ids.find(pointer);
    if (found != ids.end()) { *out = found->second; return common::OB_SUCCESS; }
    if (items.size() >= limit) return common::OB_SIZE_OVERFLOW;
    const uint32_t id = items.size();
    items.push_back(pointer); ids.emplace(pointer, id); *out = id;
    return common::OB_SUCCESS;
  }
  template <typename F> seekdb_plugin_status_t run(F call) noexcept {
    if (error_ == common::OB_SUCCESS) {
      try { error_ = call(); }
      catch (const std::bad_alloc &) { error_ = common::OB_ALLOCATE_MEMORY_FAILED; }
      catch (...) { error_ = common::OB_ERR_UNEXPECTED; }
    }
    return error_ == common::OB_SUCCESS ? SEEKDB_PLUGIN_STATUS_OK :
        error_ == common::OB_INVALID_ARGUMENT ? SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT :
        error_ == common::OB_ALLOCATE_MEMORY_FAILED ? SEEKDB_PLUGIN_STATUS_NO_MEMORY : SEEKDB_PLUGIN_STATUS_INTERNAL;
  }
  int error_ = common::OB_SUCCESS;
  std::vector<ObLogicalOperator *> nodes_;
  std::unordered_map<ObLogicalOperator *, uint32_t> node_ids_;
  std::vector<ObRawExpr *> expressions_;
  std::unordered_map<ObRawExpr *, uint32_t> expression_ids_;
};
} }
#endif
