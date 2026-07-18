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

#include "sql/engine/expr/ob_expr_tokenize.h"

#include "lib/alloc/alloc_struct.h"
#include "lib/allocator/page_arena.h"
#include "lib/charset/ob_charset.h"
#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_tree.h"
#include "lib/hash/ob_hashutils.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_macro_utils.h"
#include "object/ob_object.h"
#include "plugin/sys/ob_plugin_helper.h"
#include "sql/resolver/ddl/ob_fts_index_builder_util.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/ob_json_access_utils.h"
#include "storage/fts/dict/ob_gen_dic_loader.h"
#include "storage/fts/ob_fts_parser_property.h"
#include "storage/fts/ob_fts_plugin_helper.h"

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_json_func_helper.h" // file not self-contained, there're logs inside.

namespace oceanbase
{
namespace sql
{
namespace
{

struct ObTokenizeMemoKey
{
  ObTokenizeMemoKey()
      : fulltext_(),
        parser_name_(),
        collation_(CS_TYPE_INVALID),
        dict_epoch_(0),
        hash_(0)
  {}

  void finish_hash()
  {
    uint64_t value = common::murmurhash(fulltext_.ptr(), fulltext_.length(), 0);
    value = common::murmurhash(parser_name_.ptr(), parser_name_.length(), value);
    value = common::murmurhash(&collation_, sizeof(collation_), value);
    value = common::murmurhash(&dict_epoch_, sizeof(dict_epoch_), value);
    hash_ = value;
  }

  ObString fulltext_;
  ObString parser_name_;
  ObCollationType collation_;
  uint64_t dict_epoch_;
  uint64_t hash_;
};

class ObTokenizeMemo final
{
public:
  static constexpr int64_t SLOT_COUNT = 16;
  static constexpr int64_t MAX_FULLTEXT_LENGTH = 4096;
  static constexpr int64_t MAX_PARSER_NAME_LENGTH = 128;
  static constexpr int64_t MAX_RESULT_LENGTH = 32768;

  static ObTokenizeMemo &instance()
  {
    static ObTokenizeMemo memo;
    return memo;
  }

  bool can_store(const ObTokenizeMemoKey &key) const
  {
    return key.fulltext_.length() <= MAX_FULLTEXT_LENGTH
        && key.parser_name_.length() <= MAX_PARSER_NAME_LENGTH;
  }

  int find(const ObTokenizeMemoKey &key,
           const ObExpr &expr,
           ObEvalCtx &ctx,
           ObDatum &result,
           bool &found)
  {
    int ret = OB_SUCCESS;
    found = false;
    Slot &slot = slots_[key.hash_ & (SLOT_COUNT - 1)];
    common::ObSpinLockGuard guard(slot.lock_);
    if (slot.matches(key)) {
      char *buffer = nullptr;
      if (slot.result_length_ > 0
          && OB_ISNULL(buffer = expr.get_str_res_mem(ctx, slot.result_length_))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to allocate tokenize memo result", K(ret), K(slot.result_length_));
      } else {
        if (slot.result_length_ > 0) {
          MEMCPY(buffer, slot.result_, slot.result_length_);
        }
        result.set_string(buffer, slot.result_length_);
        found = true;
      }
    }
    return ret;
  }

  void store(const ObTokenizeMemoKey &key, const ObString &result)
  {
    if (can_store(key) && result.length() <= MAX_RESULT_LENGTH) {
      Slot &slot = slots_[key.hash_ & (SLOT_COUNT - 1)];
      common::ObSpinLockGuard guard(slot.lock_);
      slot.assign(key, result);
    }
  }

private:
  struct Slot
  {
    Slot()
        : lock_(),
          hash_(0),
          collation_(CS_TYPE_INVALID),
          dict_epoch_(0),
          fulltext_length_(0),
          parser_name_length_(0),
          result_length_(0),
          valid_(false)
    {}

    bool matches(const ObTokenizeMemoKey &key) const
    {
      return valid_
          && hash_ == key.hash_
          && collation_ == key.collation_
          && dict_epoch_ == key.dict_epoch_
          && fulltext_length_ == key.fulltext_.length()
          && parser_name_length_ == key.parser_name_.length()
          && (0 == fulltext_length_
              || 0 == MEMCMP(fulltext_, key.fulltext_.ptr(), fulltext_length_))
          && (0 == parser_name_length_
              || 0 == MEMCMP(parser_name_, key.parser_name_.ptr(), parser_name_length_));
    }

    void assign(const ObTokenizeMemoKey &key, const ObString &result)
    {
      hash_ = key.hash_;
      collation_ = key.collation_;
      dict_epoch_ = key.dict_epoch_;
      fulltext_length_ = static_cast<int32_t>(key.fulltext_.length());
      parser_name_length_ = static_cast<int32_t>(key.parser_name_.length());
      result_length_ = static_cast<int32_t>(result.length());
      if (fulltext_length_ > 0) {
        MEMCPY(fulltext_, key.fulltext_.ptr(), fulltext_length_);
      }
      if (parser_name_length_ > 0) {
        MEMCPY(parser_name_, key.parser_name_.ptr(), parser_name_length_);
      }
      if (result_length_ > 0) {
        MEMCPY(result_, result.ptr(), result_length_);
      }
      valid_ = true;
    }

    common::ObSpinLock lock_;
    char fulltext_[MAX_FULLTEXT_LENGTH];
    char parser_name_[MAX_PARSER_NAME_LENGTH];
    char result_[MAX_RESULT_LENGTH];
    uint64_t hash_;
    ObCollationType collation_;
    uint64_t dict_epoch_;
    int32_t fulltext_length_;
    int32_t parser_name_length_;
    int32_t result_length_;
    bool valid_;
  };

  ObTokenizeMemo() : slots_() {}
  ~ObTokenizeMemo() = default;

  Slot slots_[SLOT_COUNT];
  DISALLOW_COPY_AND_ASSIGN(ObTokenizeMemo);
};

bool is_memoizable_parser(const ObString &parser_name)
{
  return 0 == parser_name.case_compare("space")
      || 0 == parser_name.case_compare("ngram")
      || 0 == parser_name.case_compare("beng")
      || 0 == parser_name.case_compare("ik")
      || 0 == parser_name.case_compare("ngram2");
}

int prepare_tokenize_memo_key(const ObExpr &expr,
                              ObEvalCtx &ctx,
                              ObTokenizeMemoKey &key,
                              bool &memoizable)
{
  int ret = OB_SUCCESS;
  ObDatum *fulltext_datum = nullptr;
  ObDatum *parser_datum = nullptr;
  memoizable = false;
  if (expr.arg_cnt_ > 2 || expr.args_[0]->obj_meta_.has_lob_header()) {
    // Parser properties and LOB locators retain the general evaluation path.
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, fulltext_datum))) {
    LOG_WARN("fail to evaluate tokenize input for memo key", K(ret));
  } else if (OB_ISNULL(fulltext_datum) || fulltext_datum->is_null()) {
    // Keep NULL handling on the general path.
  } else if (expr.arg_cnt_ >= 2 && OB_FAIL(expr.args_[1]->eval(ctx, parser_datum))) {
    LOG_WARN("fail to evaluate tokenize parser for memo key", K(ret));
  } else {
    key.fulltext_ = fulltext_datum->get_string();
    key.parser_name_ = expr.arg_cnt_ < 2 || OB_ISNULL(parser_datum) || parser_datum->is_null()
        ? ObString::make_string(OB_DEFAULT_FULLTEXT_PARSER_NAME)
        : parser_datum->get_string().trim();
    key.collation_ = expr.args_[0]->obj_meta_.get_collation_type();
    if (!is_memoizable_parser(key.parser_name_)) {
      // A user parser can carry state outside of the built-in dictionary hub.
    } else if (0 == key.parser_name_.case_compare("ik")) {
      // Custom IK dictionaries can refresh asynchronously. The new dictionary
      // manager has no single global generation, so avoid stale memo entries.
    } else {
      key.finish_hash();
      memoizable = ObTokenizeMemo::instance().can_store(key);
    }
  }
  return ret;
}

} // namespace

ObExprTokenize::ObExprTokenize(common::ObIAllocator &alloc)
    : ObStringExprOperator(alloc,
                           T_FUN_TOKENIZE,
                           N_TOKENIZE,
                           MORE_THAN_ZERO,
                           VALID_FOR_GENERATED_COL)
{
}

ObExprTokenize::~ObExprTokenize() {}

int ObExprTokenize::eval_tokenize(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;

  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  common::ObArenaAllocator &temp_allocator = tmp_alloc_g.get_allocator();

  ObIJsonBase *json_result = nullptr;
  TokenizeParam param;
  ObTokenizeMemoKey memo_key;
  bool memoizable = false;
  bool memo_hit = false;

  // check param num, which is checked in ObExprOperator::calc_result_typeN.
  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Args count invalid.", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(prepare_tokenize_memo_key(expr, ctx, memo_key, memoizable))) {
    LOG_WARN("fail to prepare tokenize memo key", K(ret));
  } else if (memoizable
             && OB_FAIL(ObTokenizeMemo::instance().find(
                    memo_key, expr, ctx, expr_datum, memo_hit))) {
    LOG_WARN("fail to find tokenize memo", K(ret));
  } else if (memo_hit) {
    // The cached value is the packed binary JSON datum.
  } else if (OB_FAIL(parse_param(expr, ctx, temp_allocator, param))) {
    LOG_WARN("Fail to parse param", K(ret));
  } else if (OB_FAIL(tokenize_fulltext(param, param.output_mode_, temp_allocator, json_result))) {
    LOG_WARN("Fail to tokenize fulltext", K(ret));
  } else if (OB_FAIL(ObJsonExprHelper::pack_json_res(expr,
                                                     ctx,
                                                     temp_allocator,
                                                     json_result,
                                                     expr_datum))) {
    LOG_WARN("fail to pack json result", K(ret));
  } else if (memoizable && !expr_datum.is_null()) {
    ObTokenizeMemo::instance().store(memo_key, expr_datum.get_string());
  }

  return ret;
}

int ObExprTokenize::tokenize_fulltext(const TokenizeParam &param,
                                      TokenizeParam::OUTPUT_MODE mode,
                                      ObIAllocator &allocator,
                                      ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  storage::ObFTParseHelper tokenize_helper;
  const int64_t ft_word_bkt_cnt = MIN(MAX(param.fulltext_.length() / 2, 2), 997);
  int64_t doc_len = 0;
  ObFTWordMap token_map;

  ObArenaAllocator tmp_parse_alloc(ObMemAttr("Tmp buffer"));

  if (TokenizeParam::OUTPUT_MODE::DEFAULT != mode && TokenizeParam::OUTPUT_MODE::ALL != mode) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid output mode", K(ret), K(mode));
  } else if (OB_FAIL(tokenize_helper.init(&allocator, param.parser_name_, param.properties_))) {
    LOG_WARN("Fail to init tokenize helper", K(ret));
  } else if (OB_FAIL(token_map.create(ft_word_bkt_cnt, common::ObMemAttr("FTWordMap")))) {
    LOG_WARN("Fail to create token map", K(ret));
  } else if (
      (0 != param.fulltext_.length())
      && OB_FAIL(tokenize_helper.segment(
                     param.meta_,
                     param.fulltext_.ptr(),
                     param.fulltext_.length(),
                     doc_len,
                     token_map))) {
    LOG_WARN("Fail to segment fulltext", K(ret));
  } else {
    switch (param.output_mode_) {
    case TokenizeParam::OUTPUT_MODE::DEFAULT: {
      if (OB_FAIL(tokenize_helper.make_token_array_json(token_map, result))) {
        LOG_WARN("Fail to construct json array", K(ret));
      } else {
        // pass
      }
      break;
    }
    case TokenizeParam::OUTPUT_MODE::ALL: {
      if (OB_FAIL(tokenize_helper.make_detail_json(token_map, doc_len, result))) {
        LOG_WARN("Fail to construct detaild json", K(ret));
      } else {
        // pass
      }
      break;
    }
    default:
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Invalid output mode", K(ret), K(param.output_mode_));
    }
  }
  return ret;
}

ObExprTokenize::TokenizeParam ::TokenizeParam()
  : allocator_(ObMemAttr("TokenizeParam")),
    parser_name_(ObString(OB_DEFAULT_FULLTEXT_PARSER_NAME)),
    meta_(),
    fulltext_(),
    output_mode_(OUTPUT_MODE::DEFAULT)
{
}

int ObExprTokenize::TokenizeParam::parse_json_param(const ObIJsonBase *obj,
                                                    const ObString &database_name,
                                                    const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  ObString str;
  ObIJsonBase *val;

  if (OB_UNLIKELY(nullptr == obj)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Json param is null.", K(ret));
  } else if (ObJsonNodeType::J_OBJECT != obj->json_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Json args should be an object", K(ret));
  } else if (obj->element_count() == 0) {
    // no data
  } else if (OB_FAIL(obj->get_object_value(0, str, val))) {
    LOG_WARN("Failed to take para key from json object.", K(ret));
  } else if (0 == str.case_compare(CASE_INDICATOR_STR)) {
    if (ObJsonNodeType::J_STRING != val->json_type()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Json argument invalid", K(ret));
    } else if (0 == ObString(val->get_data_length(), val->get_data()).case_compare("UPPER")) {
    } else if (0 == ObString(val->get_data_length(), val->get_data()).case_compare("LOWER")) {
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Case indentifier not valid", K(ret));
    }
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "case indentifier");
  } else if (0 == str.case_compare(OUTPUT_MODE_STR)) {
    if (ObJsonNodeType::J_STRING != val->json_type()) {
      LOG_WARN("Json argument invalid", K(ret));
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "output mode should be string default or all");
    } else if (0 == ObString(val->get_data_length(), val->get_data()).case_compare("DEFAULT")) {
      output_mode_ = DEFAULT;
    } else if (0 == ObString(val->get_data_length(), val->get_data()).case_compare("ALL")) {
      output_mode_ = ALL;
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "output mode should be string default or all");
    }
  } else if (0 == str.case_compare(STOPWORDS_LIST_STR)) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "stopwords");
  } else if (0 == str.case_compare(ADDITIONAL_ARGS_STR)) {
    if (ObJsonNodeType::J_ARRAY != val->json_type()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Additional args should be an array", K(ret));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parser arguments");
    } else {
      ObString json_str;
      if (OB_FAIL(ObFTParserJsonProps::tokenize_array_to_props_json(
              allocator_, val, database_name, tenant_id, json_str))) {
        LOG_WARN("Fail to tokenize array to props json", K(ret));
        ObSqlString message;
        message.append_fmt("format in %s form", ADDITIONAL_ARGS_STR);
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, message.ptr());
      } else {
        properties_ = json_str;
      }
    }
  } else {
    LOG_WARN("Unsupported parameter", K(ret), K(str));
    ret = OB_INVALID_ARGUMENT;
    ObSqlString message;
    message.append_fmt("config: %s", str.ptr());
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, message.ptr());
  }
  return ret;
}

int ObExprTokenize::parse_param(const ObExpr &expr,
                                ObEvalCtx &ctx,
                                common::ObArenaAllocator &allocator,
                                TokenizeParam &param)
{
  int ret = OB_SUCCESS;

  ObDatum *parser_params_datum;
  ObString raw_parser_name = ObString::make_string(OB_DEFAULT_FULLTEXT_PARSER_NAME);

  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  const uint64_t tenant_id = 1UL;
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator(), expr.type_, ret);

  ObString database_name;
  ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();
  if (OB_NOT_NULL(session)) {
    database_name = session->get_database_name();
  }

  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Args count invalid.", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(parse_fulltext(expr, ctx, param))) {
    LOG_WARN("Fail to parse fulltext.", K(ret));
  } else if (OB_FAIL(parse_parser_name(expr, ctx, param))) {
    LOG_WARN("Fail to parse parser params.", K(ret));
  } else if (OB_FAIL(parse_parser_properties(
                 expr, tenant_id, database_name, ctx, temp_allocator, param))) {
    LOG_WARN("Fail to parse parser params.", K(ret));
  } else if (OB_FAIL(param.reform_parser_properties(param.properties_, tenant_id))) {
    LOG_WARN("Fail to reform parser params.", K(ret));
  } else if (OB_FAIL(param.try_load_dictionary_for_ik())) {
    LOG_WARN("fail to try load dictionary for ik", K(ret));
  }
  return ret;
}

int ObExprTokenize::construct_ft_parser_inner_name(const ObString &input_str, TokenizeParam &param)
{
  int ret = OB_SUCCESS;
  // make an extract parser name
  share::ObPluginName plugin_name;
  storage::ObFTParser parser;

  char *parser_name_buf = nullptr;
  if (OB_ISNULL(parser_name_buf
                = static_cast<char *>(param.allocator_.alloc(OB_PLUGIN_NAME_LENGTH)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Fail to alloc memory", K(ret));
  } else if (OB_FAIL(plugin_name.set_name(input_str))) {
    LOG_WARN("Fail to set plugin name", K(ret));
  } else if (OB_FAIL(plugin::ObPluginHelper::find_ftparser(input_str, parser))) {
    LOG_WARN("Fail to get ft parser", K(ret));
  } else if (OB_FAIL(parser.serialize_to_str(parser_name_buf, OB_PLUGIN_NAME_LENGTH))) {
    LOG_WARN("Fail to parse ft parser name", K(ret));
  } else {
    param.parser_name_ = ObString::make_string(parser_name_buf);
  }
  return ret;
}

int ObExprTokenize::calc_result_typeN(ObExprResType &type,
                                      ObExprResType *types,
                                      int64_t param_num,
                                      common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(param_num < 1 || param_num > 3)) {
    ret = OB_ERR_PARAM_SIZE;
    ObString expr_name(N_TOKENIZE);
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, expr_name.length(), expr_name.ptr());
  } else {
    // just okay
  }

  ObLength length = ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType].get_length();

  if (OB_SUCC(ret)) {
    // set res type
    type.set_json();
    type.set_length(length); // keep consistent with other json expr, maybe calc it later.

    // param type set, skip charset after first param
    for (int64_t i = 1; OB_SUCC(ret) && i < param_num; ++i) {
      if (ob_is_string_type(types[i].get_type())) {
        if (types[i].get_charset_type() != CHARSET_UTF8MB4) {
          types[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
        }
      }
    }

    // handle param
    if (param_num >= 2) {
      types[1].set_varchar();
    }

    if (param_num >= 3) {
      if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types, 2, N_TOKENIZE))) {
        LOG_WARN("wrong type for json doc.", K(ret), K(types[2].get_type()));
      }
    }
  }

  return ret;
}

int ObExprTokenize::cg_expr(ObExprCGCtx &op_cg_ctx,
                            const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  CK((rt_expr.arg_cnt_ >= 1 && rt_expr.arg_cnt_ <= 3));
  if (OB_SUCC(ret)) {
    // do register
    rt_expr.eval_func_ = eval_tokenize;
  }
  return ret;
}

int ObExprTokenize::parse_fulltext(const ObExpr &expr, ObEvalCtx &ctx, TokenizeParam &param)
{
  int ret = OB_SUCCESS;

  ObDatum *fulltext_datum;

  if (OB_FAIL(expr.args_[0]->eval(ctx, fulltext_datum))) {
    LOG_WARN("Fail to eval fulltext.", K(ret));
  } else {
    if (fulltext_datum->is_null()) {
      // do nothing, return empty result
      param.fulltext_ = ObString::make_empty_string();
    } else {
      param.fulltext_ = fulltext_datum->get_string();
    }
    param.meta_.set_varchar(); // as we hardcoded in fts_index
    param.meta_.set_collation_type(expr.args_[0]->obj_meta_.get_collation_type());
  }
  return ret;
}

int ObExprTokenize::parse_parser_name(const ObExpr &expr, ObEvalCtx &ctx, TokenizeParam &param)
{
  int ret = OB_SUCCESS;
  ObDatum *parser_datum = nullptr;
  ObString raw_parser_name;

  if (expr.arg_cnt_ < 2) {
    raw_parser_name = ObString::make_string(OB_DEFAULT_FULLTEXT_PARSER_NAME);
  } else if (OB_FAIL(expr.args_[1]->eval(ctx, parser_datum))) {
    LOG_WARN("Fail to eval parser name.", K(ret));
  } else {
    if (parser_datum->is_null()) {
      raw_parser_name = ObString::make_string(OB_DEFAULT_FULLTEXT_PARSER_NAME);
    } else {
      ObString name = parser_datum->get_string();
      raw_parser_name = name.trim();
    }
  }

  if (OB_FAIL(ret)) {
    // already logged
  } else if (OB_FAIL(construct_ft_parser_inner_name(raw_parser_name, param))) {
    LOG_WARN("Fail to construct ft parser inner name.", K(ret));
  }

  return ret;
}

int ObExprTokenize::parse_parser_properties(const ObExpr &expr,
                                            const uint64_t tenant_id,
                                            const ObString &database_name,
                                            ObEvalCtx &ctx,
                                            MultimodeAlloctor &mm_alloc,
                                            TokenizeParam &param)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *base = nullptr;

  if (expr.arg_cnt_ < 3) {
    // do nothing
  } else {
    bool is_null = false;
    if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, mm_alloc, 2, base, is_null))) {
      LOG_WARN("Fail to get json doc", K(ret));
    } else if (is_null) {
      // NULL parser args use parser defaults.
    } else {
      if (ObJsonNodeType::J_ARRAY != base->json_type()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("Invalid json type", K(ret), K(base->json_type()));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parser args should be in an array.");
      } else {
        for (uint64_t i = 0; OB_SUCC(ret) && i < base->element_count(); ++i) {
          ObIJsonBase *node = nullptr;
          if (OB_FAIL(base->get_array_element(i, node))) {
            LOG_WARN("Failed to get array element", K(ret));
          } else if (ObJsonNodeType::J_OBJECT != (node->json_type())) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("Argument of json array invalid", K(ret));
          } else if (OB_FAIL(param.parse_json_param(node, database_name, tenant_id))) {
            LOG_WARN("Failed to parse json object", K(ret));
          }
        } // for
      }
    }
  }

  return ret;
}

int ObExprTokenize::TokenizeParam::reform_parser_properties(const ObString &properties,
                                                            const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  storage::ObFTParserJsonProps parser_properties;

  if (OB_FAIL(parser_properties.init())) {
    LOG_WARN("fail to init parser properties", K(ret));
  } else if (OB_FAIL(parser_properties.parse_from_valid_str(properties))) {
    LOG_WARN("fail to parse properties", K(ret));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parser properties invalid.");
  } else if (OB_FAIL(parser_properties.rebuild_props_for_ddl(parser_name_,
                                                             ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                                             true,
                                                             tenant_id))) {
    LOG_WARN("fail to serialize to string", K(ret), K(parser_properties));
  } else if (OB_FAIL(parser_properties.to_format_json(allocator_, properties_))) {
    LOG_WARN("fail to serialize to string", K(ret), K(parser_properties));
  }

  return ret;
}

int ObExprTokenize::TokenizeParam::try_load_dictionary_for_ik()
{
  int ret = OB_SUCCESS;
  bool need_to_load_dic = false;
  ObTenantDicLoaderHandle dic_loader_handle;
  if (OB_FAIL(ObFtsIndexBuilderUtil::check_need_to_load_dic(parser_name_,
                                                            need_to_load_dic))) {
    LOG_WARN("fail to check need to load dic",
        K(ret), K(parser_name_), K(need_to_load_dic));
  } else if (need_to_load_dic) {
    if (OB_FAIL(ObGenDicLoader::get_instance().get_dic_loader(
                    ObString::make_string(ObFTSLiteral::PARSER_NAME_IK), // currently only ik, use parser_name_ without version suffix
                    ObCharset::charset_type_by_coll(meta_.get_collation_type()),
                    dic_loader_handle))) {
      LOG_WARN("fail to get dic loader", K(ret));
    } else if (OB_UNLIKELY(!dic_loader_handle.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("dic loader handle is not valid", K(ret), K(dic_loader_handle));
    } else if (OB_FAIL(dic_loader_handle.get_loader()->try_load_dictionary_in_trans())) {
      LOG_WARN("fail to try load dictionary", K(ret), K(dic_loader_handle));
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
