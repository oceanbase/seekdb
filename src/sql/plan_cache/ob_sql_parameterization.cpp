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
#include "sql/syntax.h"
#include "ob_sql_parameterization.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/resolver/expr/numeric_literal.h"
#include "sql/resolver/expr/literal_decode_options.h"

using namespace oceanbase;
using namespace sql;
using namespace common;
using namespace share::schema;

SqlInfo::SqlInfo() : total_(0), last_active_time_(0), hit_count_(0),
    ps_need_parameterized_(true), need_check_fp_(false)
{}

namespace oceanbase::sql {
namespace {
rust::Slice<const uint8_t> bytes(const ObString &text)
{
  return {reinterpret_cast<const uint8_t *>(text.ptr()), static_cast<size_t>(text.length())};
}

ObString text(const rust::Vec<uint8_t> &value)
{
  return ObString(value.size(), reinterpret_cast<const char *>(value.data()));
}

int parameter_error(peg::ParameterError error)
{
  switch (error) {
    case peg::ParameterError::None: return OB_SUCCESS;
    case peg::ParameterError::Unsupported: return OB_NOT_SUPPORTED;
    case peg::ParameterError::InvalidArgument: return OB_INVALID_ARGUMENT;
    case peg::ParameterError::InvalidOutline:
      LOG_USER_ERROR(OB_INVALID_OUTLINE, "? appears at invalid position in sql_text");
      return OB_INVALID_OUTLINE;
    default: return OB_ERR_UNEXPECTED;
  }
}

peg::ParameterAction parameter_action(ParamProperty flag)
{
  switch (flag) {
    case NOT_PARAM: return peg::ParameterAction::Preserve;
    case NEG_PARAM: return peg::ParameterAction::UnaryNegation;
    case TRANS_NEG_PARAM: return peg::ParameterAction::Subtraction;
    default: return peg::ParameterAction::Normal;
  }
}

ParamProperty parameter_property(peg::ParameterAction action)
{
  switch (action) {
    case peg::ParameterAction::Preserve: return NOT_PARAM;
    case peg::ParameterAction::UnaryNegation: return NEG_PARAM;
    case peg::ParameterAction::Subtraction: return TRANS_NEG_PARAM;
    default: return NORMAL_PARAM;
  }
}

int parameter_tokens(const ObIArray<ObPCParam *> &raw, std::vector<peg::ParameterToken> &tokens,
                     const SqlInfo *info = nullptr)
{
  int ret = OB_SUCCESS;
  if (info != nullptr && info->token_actions_.count() != raw.count()) return OB_NOT_SUPPORTED;
  tokens.reserve(raw.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < raw.count(); ++i) {
    const auto *parameter = raw.at(i);
    if (parameter == nullptr || parameter->literal_ == nullptr || parameter->source_offset_ < 0
        || parameter->sql_position_ < 0) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const auto &literal = *parameter->literal_;
      tokens.push_back({literal.kind, literal.integer,
          static_cast<size_t>(parameter->source_offset_),
          static_cast<size_t>(parameter->source_offset_ + literal.source.length()),
          static_cast<size_t>(parameter->sql_position_), bytes(literal.text), bytes(literal.source),
          literal.concatenated, static_cast<size_t>(literal.first_part_length),
          info == nullptr ? parameter_action(parameter->flag_) : info->token_actions_.at(i)});
    }
  }
  return ret;
}

int materialize_value(const peg::ParameterValue &descriptor, const LiteralDecodeOptions &options,
                      ObIAllocator &allocator, ObObjParam &value)
{
  int ret = OB_SUCCESS;
  LiteralToken literal{descriptor.kind, descriptor.integer, text(descriptor.text), {},
      descriptor.concatenated, static_cast<int32_t>(descriptor.first_part_length)};
  LiteralDecodeOptions effective = options;
  const ObString charset(descriptor.charset.size(), descriptor.charset.data());
  if (!charset.empty()) effective.charset = &charset;
  if (!descriptor.date_unit.empty()) {
    const ObString unit(descriptor.date_unit.size(), descriptor.date_unit.data());
    for (int i = DATE_UNIT_MICROSECOND; i < DATE_UNIT_MAX; ++i) {
      if (unit == ObString::make_string(ob_date_unit_type_str(static_cast<ObDateUnitType>(i)))) {
        literal.integer = i;
        break;
      }
    }
  }
  ObObjParam decoded;
  ObString prefix;
  OZ (ObResolverUtils::resolve_literal(literal, effective, allocator, decoded, prefix));
  if (OB_SUCC(ret)) {
    value = decoded;
    OZ (ob_write_obj(allocator, decoded, value));
  }
  return ret;
}

int install_projection(const peg::ParameterProjection &record, SelectItemParamInfoMap &names)
{
  int ret = OB_SUCCESS;
  SelectItemParamInfo info;
  if (record.name.size() > SelectItemParamInfo::PARAMED_FIELD_BUF_LEN) return OB_ERR_UNEXPECTED;
  MEMCPY(info.paramed_field_name_, record.name.data(), record.name.size());
  info.name_len_ = record.name.size();
  info.esc_str_flag_ = record.escaped_string;
  info.null_literal_ = record.null_literal;
  for (auto position : record.questions) OZ (info.questions_pos_.push_back(position));
  for (auto index : record.parameters) OZ (info.params_idx_.push_back(index));
  for (auto index : record.negative_parameters) OZ (info.neg_params_idx_.add_member(index));
  OZ (names.set_refactored(&record.node, info));
  return ret;
}
}
}

int ObSqlParameterization::transform_syntax_tree(ObIAllocator &allocator,
                                                const ObSQLSessionInfo &session,
                                                const ObIArray<ObPCParam *> *raw_params,
                                                const ParsedSql &parsed,
                                                SqlInfo &sql_info,
                                                ParamStore &params,
                                                ParameterizationResult &bindings,
                                                SelectItemParamInfoMap *projection_names,
                                                bool outline,
                                                SQL_EXECUTION_MODE mode,
                                                bool from_pl,
                                                ObIArray<ObString> *user_variables)
{
  int ret = OB_SUCCESS;
  const int64_t parameter_base = params.count();
  bindings.reset();
  if (parsed.statement() == nullptr) return OB_ERR_UNEXPECTED;
  ObFastParserResult scan;
  if (raw_params == nullptr) {
    FPContext context;
    context.sql_mode_ = session.get_sql_mode();
    OZ (fast_parser(allocator, context, parsed.original_source(), scan));
    raw_params = &scan.raw_params_;
  }
  std::vector<peg::ParameterToken> tokens;
  OZ (parameter_tokens(*raw_params, tokens));
  ObCollationType collation = CS_TYPE_INVALID;
  bool decimal_int = false;
  bool mysql_dates = false;
  OZ (session.get_collation_connection(collation));
  OZ (ObSQLUtils::check_enable_decimalint(&session, decimal_int));
  OZ (ObSQLUtils::check_enable_mysql_compatible_dates(&session, false, mysql_dates));
  const peg::ParameterOptions options{
      is_prepare_mode(mode) ? peg::ParameterMode::Prepare
          : is_execute_mode(mode) ? peg::ParameterMode::Execute : peg::ParameterMode::Text,
      outline, from_pl, is_no_unsigned_subtraction(session.get_sql_mode()), projection_names != nullptr,
      SelectItemParamInfo::PARAMED_FIELD_BUF_LEN, static_cast<size_t>(parameter_base)};
  if (OB_SUCC(ret)) {
    auto result = peg::parameterize(*parsed.statement(), bytes(parsed.original_source()),
        {tokens.data(), tokens.size()}, options);
    OZ (parameter_error(result.error));
    for (size_t i = 0; OB_SUCC(ret) && i < result.tokens.size(); ++i) {
      const auto &policy = result.tokens[i];
      ObCharsetType charset = CHARSET_INVALID;
      if (!policy.charset.empty()) {
        const ObString name(policy.charset.size(), policy.charset.data());
        charset = ObCharset::charset_type(name);
        if (charset == CHARSET_INVALID) {
          ret = OB_ERR_UNKNOWN_CHARSET;
          LOG_USER_ERROR(OB_ERR_UNKNOWN_CHARSET, name.length(), name.ptr());
        }
      }
      OZ (sql_info.param_charset_type_.push_back(charset));
      OZ (sql_info.token_actions_.push_back(policy.action));
      switch (policy.action) {
        case peg::ParameterAction::Preserve: OZ (sql_info.not_param_index_.add_member(i)); break;
        case peg::ParameterAction::UnaryNegation: OZ (sql_info.neg_param_index_.add_member(i)); break;
        case peg::ParameterAction::Subtraction: OZ (sql_info.trans_from_minus_index_.add_member(i)); break;
        default: break;
      }
      if (policy.positive) OZ (sql_info.must_be_positive_index_.add_member(i));
      if (policy.arithmetic) OZ (sql_info.fmt_int_or_ch_decint_idx_.add_member(i));
      if (policy.excluded_placeholder) OZ (sql_info.ps_not_param_offsets_.push_back(policy.placeholder));
    }
    for (auto offset : result.unchecked_placeholders) OZ (sql_info.no_check_type_offsets_.push_back(offset));
    for (const auto &record : result.bindings) {
      if (OB_FAIL(ret)) break;
      const auto &token = *raw_params->at(record.raw_index);
      const NumericLiteralOptions numeric{stmt::T_NONE, decimal_int, from_pl,
          result.tokens[record.raw_index].arithmetic, session.get_min_const_integer_precision()};
      const LiteralDecodeOptions decode{numeric, collation, session.get_timezone_info(), session.get_sql_mode(),
          share::COMPAT_MYSQL57, mysql_dates, outline, nullptr, NumericSignMode::Source};
      ObObjParam value;
      OZ (materialize_value(record.value, decode, allocator, value));
      if (record.fixed) {
        value.set_need_to_check_type(record.check_type);
        if (record.scale_policy == peg::ParameterScalePolicy::IgnoreNonDecimalScale && !value.is_decimal_int()) {
          value.set_ignore_scale_check(true);
        }
        OZ (sql_info.fixed_param_idx_.push_back(record.parameter_index));
      }
      value.set_is_boolean(value.is_boolean() || record.boolean);
      value.set_raw_text_info(token.source_offset_, token.literal_->source.length());
      if (record.append) {
        OZ (params.push_back(value));
      }
    }
    if (OB_SUCC(ret) && projection_names != nullptr && !result.projections.empty()) {
      if (!projection_names->created()) OZ (projection_names->create(result.projections.size(), "ProjectionNames"));
      for (const auto &record : result.projections) OZ (install_projection(record, *projection_names));
    }
    if (user_variables != nullptr) {
      for (const auto &name : result.user_variables) {
        ObString copied;
        OZ (ob_write_string(allocator, ObString(name.size(), name.data()), copied));
        if (OB_SUCC(ret)) ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, copied);
        OZ (user_variables->push_back(copied));
      }
    }
    if (OB_SUCC(ret)) {
      bindings.install(std::move(result.effects));
      sql_info.total_ = result.tokens.size();
      sql_info.ps_need_parameterized_ = result.ps_eligible;
    }
  }
  if (OB_FAIL(ret)) {
    bindings.reset();
    while (params.count() > parameter_base) params.pop_back();
    if (projection_names != nullptr && projection_names->created()) projection_names->clear();
  }
  return ret;
}

int ObSqlParameterization::check_and_generate_param_info(const ObIArray<ObPCParam *> &raw_params,
                                                         const SqlInfo &sql_info,
                                                         ObIArray<ObPCParam *> &special_params)
{
  int ret = OB_SUCCESS;
  std::vector<peg::ParameterToken> tokens;
  OZ (parameter_tokens(raw_params, tokens, &sql_info));
  if (OB_SUCC(ret)) {
    const auto result = peg::classify_parameter_tokens({tokens.data(), tokens.size()}, sql_info.total_, sql_info.need_check_fp_);
    OZ (parameter_error(result.error));
    for (size_t i = 0; OB_SUCC(ret) && i < result.actions.size(); ++i) {
      raw_params.at(i)->flag_ = parameter_property(result.actions[i]);
      if (raw_params.at(i)->flag_ != NORMAL_PARAM) OZ (special_params.push_back(raw_params.at(i)));
    }
  }
  return ret;
}

int ObSqlParameterization::construct_no_check_type_params(const ObIArray<int64_t> &offsets,
                                                          ParamStore &params)
{
  int ret = OB_SUCCESS;
  std::vector<size_t> unchecked;
  std::vector<peg::ParameterRepresentation> representations;
  for (int64_t i = 0; OB_SUCC(ret) && i < offsets.count(); ++i) {
    if (offsets.at(i) < 0 || offsets.at(i) >= params.count()) ret = OB_INVALID_ARGUMENT;
    else unchecked.push_back(offsets.at(i));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); ++i) {
    representations.push_back(params.at(i).is_ext() || ob_is_enumset_inner_tc(params.at(i).get_meta().get_type())
        ? peg::ParameterRepresentation::Extended : peg::ParameterRepresentation::Ordinary);
  }
  if (OB_SUCC(ret)) {
    const auto policies = peg::unchecked_parameter_types({unchecked.data(), unchecked.size()},
        {representations.data(), representations.size()});
    for (size_t i = 0; i < policies.size(); ++i) {
      if (policies[i] == peg::ParameterTypeCheck::SkipType) params.at(i).set_need_to_check_type(false);
      else if (policies[i] == peg::ParameterTypeCheck::SkipExtendedType) params.at(i).set_need_to_check_extend_type(false);
    }
  }
  return ret;
}

int ObSqlParameterization::construct_sql(const ObString &sql, ObIArray<ObPCParam *> &parameters,
                                         char *buf, int32_t buf_len, int32_t &pos)
{
  int ret = OB_SUCCESS;
  std::vector<peg::ParameterToken> tokens;
  OZ (parameter_tokens(parameters, tokens));
  if (OB_SUCC(ret)) {
    const auto result = peg::rewrite_parameter_sql(bytes(sql), {tokens.data(), tokens.size()}, false);
    OZ (parameter_error(result.error));
    if (OB_SUCC(ret)) {
      if (pos < 0 || pos > buf_len || result.sql.size() > static_cast<size_t>(buf_len - pos)) ret = OB_BUF_NOT_ENOUGH;
      else { MEMCPY(buf + pos, result.sql.data(), result.sql.size()); pos += result.sql.size(); }
    }
  }
  return ret;
}

int ObSqlParameterization::try_format_in_expr(const ObString &sql, char *buf, int32_t buf_len,
                                              int32_t &pos, bool &can_format)
{
  int ret = OB_SUCCESS;
  const auto result = peg::fold_parameter_in_lists(bytes(sql));
  OZ (parameter_error(result.error));
  can_format = result.folded_in_list;
  if (OB_SUCC(ret)) {
    if (pos < 0 || pos > buf_len || result.sql.size() > static_cast<size_t>(buf_len - pos)) ret = OB_BUF_NOT_ENOUGH;
    else { MEMCPY(buf + pos, result.sql.data(), result.sql.size()); pos += result.sql.size(); }
  }
  return ret;
}

namespace {
peg::SqlMode parser_mode(ObSQLMode mode)
{
  return {bool(mode & SMO_ANSI_QUOTES), bool(mode & SMO_NO_BACKSLASH_ESCAPES),
      bool(mode & SMO_PIPES_AS_CONCAT), bool(mode & SMO_HIGH_NOT_PRECEDENCE),
      bool(mode & SMO_REAL_AS_FLOAT)};
}
}

int ObSqlParameterization::formalize_fast_parameter_sql(ObIAllocator &allocator,
                                                        const ObString &src_sql, ObString &dest_sql,
                                                        ObIArray<ObPCParam *> &raw_params,
                                                        const FPContext &fp_ctx)
{
  int ret = OB_SUCCESS;
  dest_sql = src_sql;
  if (peg::fast_parameterization_allowed(bytes(src_sql), parser_mode(fp_ctx.sql_mode_))) {
    FPContext context = fp_ctx;
    context.is_format_ = true;
    ObFastParserResult result;
    OZ (ObFastParser::parse(src_sql, context, allocator, result));
    if (OB_SUCC(ret)) { dest_sql = result.pc_key_.name_; OZ (raw_params.assign(result.raw_params_)); }
  }
  return ret;
}

int ObSqlParameterization::formalize_sql_filter_hint(ObIAllocator &allocator,
                                                     const ObString &src_sql, ObString &dest_sql,
                                                     ObIArray<ObPCParam *> &raw_params)
{
  int ret = OB_SUCCESS;
  if (peg::fast_parameterization_allowed(bytes(src_sql), peg::SqlMode{})) {
    FPContext context;
    context.is_format_ = true;
    ObFastParserResult result;
    OZ (ObFastParser::parse(src_sql, context, allocator, result));
    if (OB_SUCC(ret) && result.raw_params_.count() != raw_params.count()) ret = OB_ERR_UNEXPECTED;
    if (OB_SUCC(ret)) {
      dest_sql = result.pc_key_.name_;
      for (int64_t i = 0; i < raw_params.count(); ++i) {
        if (raw_params.at(i) != nullptr) raw_params.at(i)->sql_position_ = result.raw_params_.at(i)->sql_position_;
      }
    }
  }
  return ret;
}

int ObSqlParameterization::fast_parser(ObIAllocator &allocator, const FPContext &context,
                                       const ObString &sql, ObFastParserResult &result)
{
  if (peg::fast_parameterization_allowed(bytes(sql), parser_mode(context.sql_mode_))) {
    return ObFastParser::parse(sql, context, allocator, result);
  }
  result.pc_key_.name_ = sql;
  return OB_SUCCESS;
}

int ObSqlParameterization::raw_fast_parameterize_sql(ObIAllocator &allocator,
                                                     const ObSQLSessionInfo &session,
                                                     const ObString &sql, ObString &no_param_sql,
                                                     ObIArray<ObPCParam *> &raw_params, ParseMode parse_mode)
{
  int ret = OB_SUCCESS;
  const auto length = peg::normalized_sql_length(bytes(sql));
  FPContext context;
  context.sql_mode_ = session.get_sql_mode();
  context.parameterize_ = parse_mode == FP_MODE || parse_mode == FP_PARAMERIZE_AND_FILTER_HINT_MODE;
  context.ignore_hint_ = parse_mode == FP_PARAMERIZE_AND_FILTER_HINT_MODE
      || parse_mode == FP_NO_PARAMERIZE_AND_FILTER_HINT_MODE;
  ObFastParserResult result;
  NG_TRACE(pc_fast_parse_start);
  if (length == 0) ret = OB_ERR_EMPTY_QUERY;
  else if (OB_FAIL(ObFastParser::parse(ObString(length, sql.ptr()), context, allocator, result))) {
    if (ret == OB_ERR_PARSER_SYNTAX) ret = OB_ERR_PARSE_SQL;
  } else {
    no_param_sql = result.pc_key_.name_;
    OZ (raw_params.assign(result.raw_params_));
  }
  NG_TRACE(pc_fast_parse_end);
  return ret;
}

int ObSqlParameterization::formalize_sql_text(ObIAllocator &allocator, const ObString &src_sql,
                                              ObString &fmt_sql, const SqlInfo &sql_info,
                                              const FPContext &context)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObPCParam *, OB_PC_RAW_PARAM_COUNT> raw_params;
  ObSEArray<ObPCParam *, OB_PC_SPECIAL_PARAM_COUNT> special_params;
  OZ (formalize_fast_parameter_sql(allocator, src_sql, fmt_sql, raw_params, context));
  OZ (check_and_generate_param_info(raw_params, sql_info, special_params));
  std::vector<peg::ParameterToken> tokens;
  OZ (parameter_tokens(special_params, tokens));
  if (OB_SUCC(ret)) {
    const auto result = peg::rewrite_parameter_sql(bytes(fmt_sql), {tokens.data(), tokens.size()}, true);
    OZ (parameter_error(result.error));
    OZ (ob_write_string(allocator, text(result.sql), fmt_sql));
  }
  return ret;
}

SQL_EXECUTION_MODE ObSqlParameterization::get_sql_execution_mode(ObPlanCacheCtx &pc_ctx)
{
  if (PC_PS_MODE == pc_ctx.mode_ || PC_PL_MODE == pc_ctx.mode_) {
    return pc_ctx.is_parameterized_execute_
        ? (PC_PL_MODE == pc_ctx.mode_ ? PL_EXECUTE_MODE : PS_EXECUTE_MODE)
        : (PC_PL_MODE == pc_ctx.mode_ ? PL_PREPARE_MODE : PS_PREPARE_MODE);
  }
  return TEXT_MODE;
}

bool ObSqlParameterization::is_prepare_mode(SQL_EXECUTION_MODE mode)
{
  return mode == PS_PREPARE_MODE || mode == PL_PREPARE_MODE;
}

bool ObSqlParameterization::is_execute_mode(SQL_EXECUTION_MODE mode)
{
  return mode == PS_EXECUTE_MODE || mode == PL_EXECUTE_MODE;
}

bool ObSqlParameterization::is_text_mode(SQL_EXECUTION_MODE mode)
{
  return mode == TEXT_MODE;
}


// Long path SQL parameterization
int ObSqlParameterization::parameterize_syntax_tree(common::ObIAllocator &allocator,
                                                    bool is_transform_outline,
                                                    ObPlanCacheCtx &pc_ctx,
                                                    const ParsedSql &parsed,
                                                    ParamStore &params)
{
  int ret = OB_SUCCESS;
  if (pc_ctx.select_item_param_infos_.created()) {
    pc_ctx.select_item_param_infos_.clear();
  }
  SqlInfo sql_info;
  bool need_parameterized = false;
  SQL_EXECUTION_MODE mode = get_sql_execution_mode(pc_ctx);
  ObSQLSessionInfo *session = NULL;
  ObSEArray<ObPCParam *, OB_PC_SPECIAL_PARAM_COUNT> special_params;
  ObSEArray<ObString, 4> user_var_names;
  FPContext fp_ctx;

  int tmp_ret = OB_SUCCESS;
  tmp_ret = OB_E(EventTable::EN_SQL_PARAM_FP_NP_NOT_SAME_ERROR) OB_SUCCESS;
  if (OB_SUCCESS != tmp_ret) {
    sql_info.need_check_fp_ = true;
  }
  int64_t reserved_cnt = 0;
  if (OB_ISNULL(session = pc_ctx.exec_ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    SQL_PC_LOG(ERROR, "got session is NULL", K(ret));
  } else {
    fp_ctx.enable_batched_multi_stmt_ = pc_ctx.sql_ctx_.handle_batched_multi_stmt();
    fp_ctx.sql_mode_ = session->get_sql_mode();
    fp_ctx.is_format_ = false;
  }

  if (OB_FAIL(ret)) {
  } else if (is_prepare_mode(mode)
            || is_transform_outline) {
    // if so, faster parser is needed
    // otherwise, fast parser has been done before
    pc_ctx.fp_result_.reset();

    ObString raw_sql = pc_ctx.raw_sql_;
    if (pc_ctx.sql_ctx_.is_do_insert_batch_opt()) {
      raw_sql = pc_ctx.insert_batch_opt_info_.new_reconstruct_sql_;
    } else if (pc_ctx.exec_ctx_.has_dynamic_values_table()) {
      raw_sql = pc_ctx.new_raw_sql_;
    }
    if (OB_FAIL(fast_parser(allocator,
                            fp_ctx,
                            raw_sql,
                            pc_ctx.fp_result_))) {
    }
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (FALSE_IT(reserved_cnt = pc_ctx.fp_result_.raw_params_.count())) {
  } else if (!is_execute_mode(mode)
             && (OB_FAIL(params.reserve(reserved_cnt)) // Reserve array to avoid extending
             || OB_FAIL(sql_info.param_charset_type_.reserve(reserved_cnt))
             || OB_FAIL(sql_info.fixed_param_idx_.reserve(reserved_cnt)))) {
  } else if (OB_FAIL(transform_syntax_tree(allocator,
                                           *session,
                                           is_execute_mode(mode) ? NULL : &pc_ctx.fp_result_.raw_params_,
                                           parsed,
                                           sql_info,
                                           params,
                                           pc_ctx.parameterization_,
                                           is_prepare_mode(mode) ? NULL : &pc_ctx.select_item_param_infos_,
                                           is_transform_outline,
                                           mode, false, &user_var_names))) {
    if (OB_NOT_SUPPORTED != ret) {
      SQL_PC_LOG(WARN, "fail to normal parameterized parser tree", K(ret));
    }
  } else {
    need_parameterized = (!(PC_PS_MODE == pc_ctx.mode_ || PC_PL_MODE == pc_ctx.mode_)
                          || (is_prepare_mode(mode) && sql_info.ps_need_parameterized_));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(pc_ctx.sql_ctx_.set_related_user_var_names(user_var_names, allocator))) {
  } else if (is_execute_mode(mode)) {
    if (OB_FAIL(gen_ps_not_param_var(sql_info.ps_not_param_offsets_, params, pc_ctx))) {
    } else if (OB_FAIL(construct_no_check_type_params(sql_info.no_check_type_offsets_, params))) {
    }
  } else if (need_parameterized) {
    if (OB_FAIL(check_and_generate_param_info(pc_ctx.fp_result_.raw_params_,
                                              sql_info,
                                              special_params))) {
      if (OB_NOT_SUPPORTED != ret) {
        SQL_PC_LOG(WARN, "fail to check and generate param info", K(ret));
      } else if (sql_info.need_check_fp_) {
        SQL_PC_LOG(INFO, "print tree", K(session->get_current_query_string()), "sql", parsed.original_source());
      } else {
        // do nothing
      }
    } else if (OB_FAIL(gen_special_param_info(sql_info, pc_ctx))) {
    } else {
      // do nothing
    }
    if (OB_SUCC(ret)) {
      int32_t pos = 0;
      ObString con_str;
      int64_t len = pc_ctx.raw_sql_.length();
      char* buf = (char *)allocator.alloc(len);

      if (NULL == buf) {
        SQL_PC_LOG(WARN, "fail to alloc buf", K(pc_ctx.raw_sql_.length()));
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(construct_sql(pc_ctx.fp_result_.pc_key_.name_, special_params, buf, len, pos))) {
      } else if (!pc_ctx.is_batch_insert_opt_ &&
                 !pc_ctx.exec_ctx_.has_dynamic_values_table() &&
                 OB_FAIL(ObSqlParameterization::formalize_sql_text(allocator, pc_ctx.raw_sql_,
                                                  pc_ctx.sql_ctx_.plan_key_.format_sql_,
                                                  sql_info, fp_ctx))) {
        SQL_PC_LOG(WARN, "fail to formalize sql text", K(ret), K(pc_ctx.raw_sql_));
      } else {
        pc_ctx.sql_ctx_.plan_key_.constructed_sql_.assign_ptr(buf, pos);
        pc_ctx.ps_need_parameterized_ = sql_info.ps_need_parameterized_;
        pc_ctx.normal_parse_const_cnt_ = sql_info.total_;
      }
    }
  }
  if (OB_FAIL(ret)) {
    pc_ctx.parameterization_.reset();
    if (pc_ctx.select_item_param_infos_.created()) pc_ctx.select_item_param_infos_.clear();
  }

  return ret;
}
// Generate not param info information and index information
int ObSqlParameterization::gen_special_param_info(SqlInfo &sql_info, ObPlanCacheCtx &pc_ctx)
{
  int ret = OB_SUCCESS;
  const LiteralToken *raw_param = NULL;
  NotParamInfo np_info;
  pc_ctx.not_param_info_.set_capacity(sql_info.not_param_index_.num_members());
  for (int32_t i = 0; OB_SUCC(ret) && i < pc_ctx.fp_result_.raw_params_.count(); i ++) {
    if (OB_ISNULL(pc_ctx.fp_result_.raw_params_.at(i))) {
      ret = OB_INVALID_ARGUMENT;
    } else if (NULL == (raw_param = pc_ctx.fp_result_.raw_params_.at(i)->literal_)) {
      ret = OB_INVALID_ARGUMENT;
      SQL_PC_LOG(WARN, "invalid argument", K(ret));
    } else if (sql_info.not_param_index_.has_member(i)) { //not param
      np_info.reset();
      np_info.idx_ = i;
      np_info.raw_text_ = raw_param->source;
      if (OB_FAIL(pc_ctx.not_param_info_.push_back(np_info))) {
      }
    }
  } // for end

  if (OB_SUCC(ret)) {
    if (OB_FAIL(pc_ctx.not_param_index_.add_members2(sql_info.not_param_index_))) {
    } else if (OB_FAIL(pc_ctx.neg_param_index_.add_members2(sql_info.neg_param_index_))) {
    } else if (OB_FAIL(pc_ctx.neg_param_index_.add_members2(sql_info.trans_from_minus_index_))) {
    } else if (OB_FAIL(pc_ctx.param_charset_type_.assign(sql_info.param_charset_type_))) {
    } else if (OB_FAIL(pc_ctx.fixed_param_idx_.assign(sql_info.fixed_param_idx_))) {
    } else if (OB_FAIL(pc_ctx.must_be_positive_index_.add_members2(sql_info.must_be_positive_index_))) {
    } else if (OB_FAIL(pc_ctx.fmt_int_or_ch_decint_idx_.add_members2(sql_info.fmt_int_or_ch_decint_idx_))){
    }
  }

  return ret;
}

int ObSqlParameterization::gen_ps_not_param_var(const ObIArray<int64_t> &offsets,
                                                ParamStore &params,
                                                ObPlanCacheCtx &pc_ctx)
{
  int ret = OB_SUCCESS;
  pc_ctx.not_param_var_.set_capacity(offsets.count());
  for (int i = 0; OB_SUCC(ret) && i < offsets.count(); ++i) {
    const int64_t offset = offsets.at(i);
    PsNotParamInfo ps_not_param_var;
    ps_not_param_var.idx_ = offset;
    if (offset >= params.count()) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ps_not_param_var.ps_param_ = params.at(offset);
      if (OB_FAIL(pc_ctx.not_param_var_.push_back(ps_not_param_var))) {
      } else if (OB_FAIL(pc_ctx.not_param_index_.add_member(offset))) {
      } 
    }
  }
  return ret;
}
