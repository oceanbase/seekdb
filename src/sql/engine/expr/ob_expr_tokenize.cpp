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
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_string.h"
#include "lib/utility/alloc_assist.h"
#include "lib/utility/ob_macro_utils.h"
#include "object/ob_object.h"
#include "sql/resolver/ddl/ob_fts_index_builder_util.h"
#include "share/ob_json_access_utils.h"
#include "data_plane/fts/ob_fts_parser_helper.h"
#include "data_plane/fts/dict/ob_gen_dic_loader.h"
#include "data_plane/fts/ob_fts_parser_property.h"

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_json_func_helper.h" // file not self-contained, there're logs inside.

namespace oceanbase
{
namespace sql
{

namespace
{
class ObTokenizeParserNameCache
{
public:
  static constexpr int64_t CACHE_ENTRY_NUM = 8;

  static ObTokenizeParserNameCache &instance()
  {
    static thread_local ObTokenizeParserNameCache cache;
    return cache;
  }

  int get(const common::ObString &raw_name, common::ObString &normalized_name) const
  {
    int ret = OB_ENTRY_NOT_EXIST;
    for (int64_t i = 0; i < CACHE_ENTRY_NUM; ++i) {
      if (entries_[i].match(raw_name)) {
        normalized_name = entries_[i].normalized_name();
        ret = OB_SUCCESS;
        break;
      }
    }
    return ret;
  }

  int put(const common::ObString &raw_name, const common::ObString &normalized_name)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(raw_name.length() >= storage::OB_FT_PARSER_NAME_LENGTH
                    || normalized_name.length() >= storage::OB_FT_PARSER_NAME_LENGTH)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      entries_[next_idx_].set(raw_name, normalized_name);
      next_idx_ = (next_idx_ + 1) % CACHE_ENTRY_NUM;
    }
    return ret;
  }

private:
  struct Entry
  {
    Entry() : valid_(false), raw_name_(), normalized_name_() {}

    bool match(const common::ObString &raw_name) const
    {
      return valid_ && raw_name_.length() == raw_name.length()
             && (raw_name.length() == 0
                 || 0 == MEMCMP(raw_name_.ptr(), raw_name.ptr(), raw_name.length()));
    }

    void set(const common::ObString &raw_name, const common::ObString &normalized_name)
    {
      raw_name_ = copy(raw_name, raw_buf_);
      normalized_name_ = copy(normalized_name, normalized_buf_);
      valid_ = true;
    }

    common::ObString normalized_name() const { return normalized_name_; }

  private:
    static common::ObString copy(
        const common::ObString &src,
        char (&dst)[storage::OB_FT_PARSER_NAME_LENGTH])
    {
      if (src.length() > 0) {
        MEMCPY(dst, src.ptr(), src.length());
      }
      return common::ObString(src.length(), dst);
    }

    bool valid_;
    char raw_buf_[storage::OB_FT_PARSER_NAME_LENGTH];
    char normalized_buf_[storage::OB_FT_PARSER_NAME_LENGTH];
    common::ObString raw_name_;
    common::ObString normalized_name_;
  };

  ObTokenizeParserNameCache() : next_idx_(0) {}

  int64_t next_idx_;
  Entry entries_[CACHE_ENTRY_NUM];
};

/**
 * A small cache of the reformatted parser properties.
 *
 * The reformatted json only depends on the parser name and on the raw
 * properties given by the user, both of which are stable for a given
 * expression. Rebuilding it needs a json parse and a json print, which costs
 * more than the tokenization itself, so the result is cached and reused.
 */
class ObTokenizePropsCache
{
public:
  static constexpr int64_t CACHE_ENTRY_NUM = 4;

  static ObTokenizePropsCache &instance()
  {
    static thread_local ObTokenizePropsCache cache;
    return cache;
  }

  int get(const common::ObString &parser_name,
          const common::ObString &raw_props,
          common::ObIAllocator &alloc,
          common::ObString &formatted)
  {
    int ret = OB_ENTRY_NOT_EXIST;
    for (int64_t i = 0; i < CACHE_ENTRY_NUM; ++i) {
      if (entries_[i].match(parser_name, raw_props)) {
        if (OB_FAIL(entries_[i].copy_out(alloc, formatted))) {
        } else {
          ret = OB_SUCCESS;
        }
        break;
      }
    }
    return ret;
  }

  int put(const common::ObString &parser_name,
          const common::ObString &raw_props,
          const common::ObString &formatted)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(entries_[next_idx_].set(parser_name, raw_props, formatted))) {
    } else {
      next_idx_ = (next_idx_ + 1) % CACHE_ENTRY_NUM;
    }
    return ret;
  }

private:
  struct Entry
  {
    Entry() : alloc_(), parser_name_(), raw_props_(), formatted_() {}

    bool match(const common::ObString &parser_name, const common::ObString &raw_props) const
    {
      return str_equal(parser_name_, parser_name) && str_equal(raw_props_, raw_props);
    }

    int set(const common::ObString &parser_name,
            const common::ObString &raw_props,
            const common::ObString &formatted)
    {
      int ret = OB_SUCCESS;
      alloc_.reset();
      parser_name_ = common::ObString();
      raw_props_ = common::ObString();
      formatted_ = common::ObString();
      if (OB_FAIL(copy_in(parser_name, parser_name_)) || OB_FAIL(copy_in(raw_props, raw_props_))
          || OB_FAIL(copy_in(formatted, formatted_))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        alloc_.reset();
        parser_name_ = common::ObString();
        raw_props_ = common::ObString();
        formatted_ = common::ObString();
      }
      return ret;
    }

    int copy_out(common::ObIAllocator &alloc, common::ObString &out) const
    {
      int ret = OB_SUCCESS;
      char *buf = static_cast<char *>(alloc.alloc(formatted_.length() + 1));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        if (formatted_.length() > 0) {
          MEMCPY(buf, formatted_.ptr(), formatted_.length());
        }
        buf[formatted_.length()] = '\0';
        out = common::ObString(formatted_.length(), buf);
      }
      return ret;
    }

  private:
    int copy_in(const common::ObString &src, common::ObString &dst)
    {
      int ret = OB_SUCCESS;
      if (src.length() > 0) {
        char *buf = static_cast<char *>(alloc_.alloc(src.length()));
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          MEMCPY(buf, src.ptr(), src.length());
          dst = common::ObString(src.length(), buf);
        }
      }
      return ret;
    }

    static bool str_equal(const common::ObString &a, const common::ObString &b)
    {
      return a.length() == b.length() && (a.length() == 0 || 0 == MEMCMP(a.ptr(), b.ptr(), a.length()));
    }

    common::ObArenaAllocator alloc_;
    common::ObString parser_name_;
    common::ObString raw_props_;
    common::ObString formatted_;
  };

  ObTokenizePropsCache() : next_idx_(0) {}

  int64_t next_idx_;
  Entry entries_[CACHE_ENTRY_NUM];
};
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

  // check param num, which is checked in ObExprOperator::calc_result_typeN.
  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(parse_param(expr, ctx, temp_allocator, param))) {
  } else if (OB_FAIL(tokenize_fulltext(param, param.output_mode_, temp_allocator, json_result))) {
  } else if (OB_FAIL(ObJsonExprHelper::pack_json_res(expr,
                                                     ctx,
                                                     temp_allocator,
                                                     json_result,
                                                     expr_datum))) {
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
  storage::ObFTWordMap token_map;

  if (TokenizeParam::OUTPUT_MODE::DEFAULT != mode && TokenizeParam::OUTPUT_MODE::ALL != mode) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(tokenize_helper.init(&allocator, param.parser_name_, param.properties_))) {
  } else if (OB_FAIL(token_map.create(ft_word_bkt_cnt, common::ObMemAttr("FTWordMap")))) {
  } else if (
      (0 != param.fulltext_.length())
      && OB_FAIL(tokenize_helper.segment(
                     param.meta_,
                     param.fulltext_.ptr(),
                     param.fulltext_.length(),
                     doc_len,
                     token_map))) {
  } else {
    switch (param.output_mode_) {
    case TokenizeParam::OUTPUT_MODE::DEFAULT: {
      if (OB_FAIL(tokenize_helper.make_token_array_json(token_map, result))) {
      } else {
        // pass
      }
      break;
    }
    case TokenizeParam::OUTPUT_MODE::ALL: {
      if (OB_FAIL(tokenize_helper.make_detail_json(token_map, doc_len, result))) {
      } else {
        // pass
      }
      break;
    }
    default:
      ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}

ObExprTokenize::TokenizeParam ::TokenizeParam()
  : allocator_(nullptr),
    parser_name_(ObString(OB_DEFAULT_FULLTEXT_PARSER_NAME)),
    meta_(),
    fulltext_(),
    output_mode_(OUTPUT_MODE::DEFAULT)
{
}

int ObExprTokenize::TokenizeParam::parse_json_param(const ObIJsonBase *obj)
{
  int ret = OB_SUCCESS;
  ObString str;
  ObIJsonBase *val;

  if (OB_UNLIKELY(nullptr == obj)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (ObJsonNodeType::J_OBJECT != obj->json_type()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (obj->element_count() == 0) {
    // no data
  } else if (OB_FAIL(obj->get_object_value(0, str, val))) {
  } else if (0 == str.case_compare(CASE_INDICATOR_STR)) {
    if (ObJsonNodeType::J_STRING != val->json_type()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (0 == ObString(val->get_data_length(), val->get_data()).case_compare("UPPER")) {
    } else if (0 == ObString(val->get_data_length(), val->get_data()).case_compare("LOWER")) {
    } else {
      ret = OB_INVALID_ARGUMENT;
    }
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "case indentifier");
  } else if (0 == str.case_compare(OUTPUT_MODE_STR)) {
    if (ObJsonNodeType::J_STRING != val->json_type()) {
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
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parser arguments");
    } else {
      ObString json_str;
      if (OB_ISNULL(allocator_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(storage::ObFTParserJsonProps::tokenize_array_to_props_json(*allocator_,
                                                                                    val,
                                                                                    json_str))) {
        ObSqlString message;
        message.append_fmt("format in %s form", ADDITIONAL_ARGS_STR);
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, message.ptr());
      } else {
        properties_ = json_str;
      }
    }
  } else {
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

  MultimodeAlloctor temp_allocator(allocator);
  param.allocator_ = &allocator;

  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(parse_fulltext(expr, ctx, param))) {
  } else if (OB_FAIL(parse_parser_name(expr, ctx, param))) {
  } else if (OB_FAIL(parse_parser_properties(expr, ctx, temp_allocator, param))) {
  } else if (OB_FAIL(param.reform_parser_properties(param.properties_))) {
  } else if (OB_FAIL(param.try_load_dictionary_for_ik())) {
  }
  return ret;
}

int ObExprTokenize::construct_ft_parser_inner_name(const ObString &input_str, TokenizeParam &param)
{
  int ret = OB_SUCCESS;
  storage::ObFTParser parser;

  if (OB_SUCC(ObTokenizeParserNameCache::instance().get(input_str, param.parser_name_))) {
  } else if (OB_FAIL(parser.init(input_str))) {
  } else {
    char parser_name_buf[storage::OB_FT_PARSER_NAME_LENGTH];
    if (OB_FAIL(parser.serialize_to_str(parser_name_buf, storage::OB_FT_PARSER_NAME_LENGTH))) {
    } else {
      const ObString normalized_name = ObString::make_string(parser_name_buf);
      if (OB_SUCC(ObTokenizeParserNameCache::instance().put(input_str, normalized_name))
          && OB_SUCC(ObTokenizeParserNameCache::instance().get(input_str, param.parser_name_))) {
      } else if (OB_ISNULL(param.allocator_)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ret = OB_SUCCESS;
        char *parser_name_ptr = static_cast<char *>(
            param.allocator_->alloc(normalized_name.length() + 1));
        if (OB_ISNULL(parser_name_ptr)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          MEMCPY(parser_name_ptr, normalized_name.ptr(), normalized_name.length());
          parser_name_ptr[normalized_name.length()] = '\0';
          param.parser_name_ = ObString(normalized_name.length(), parser_name_ptr);
        }
      }
    }
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
  }

  return ret;
}

int ObExprTokenize::parse_parser_properties(const ObExpr &expr,
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
    } else {
      if (ObJsonNodeType::J_ARRAY != base->json_type()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parser args should be in an array.");
      } else {
        for (uint64_t i = 0; OB_SUCC(ret) && i < base->element_count(); ++i) {
          ObIJsonBase *node = nullptr;
          if (OB_FAIL(base->get_array_element(i, node))) {
          } else if (ObJsonNodeType::J_OBJECT != (node->json_type())) {
            ret = OB_INVALID_ARGUMENT;
          } else if (OB_FAIL(param.parse_json_param(node))) {
          }
        } // for
      }
    }
  }

  return ret;
}

int ObExprTokenize::TokenizeParam::reform_parser_properties(const ObString &properties)
{
  int ret = OB_SUCCESS;
  ObString formatted;

  if (OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObTokenizePropsCache::instance().get(parser_name_,
                                                          properties,
                                                          *allocator_,
                                                          formatted))) {
    // The standard format of the properties has not been built yet, build it
    // and remember the result, the same properties are used again and again.
    storage::ObFTParserJsonProps parser_properties;
    if (OB_FAIL(parser_properties.init())) {
    } else if (OB_FAIL(parser_properties.parse_from_valid_str(properties))) {
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parser properties invalid.");
    } else if (OB_FAIL(parser_properties.rebuild_props_for_ddl(parser_name_,
                                                               ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                                               true))) {
    } else if (OB_FAIL(parser_properties.to_format_json(*allocator_, formatted))) {
    } else if (OB_FAIL(ObTokenizePropsCache::instance().put(parser_name_, properties, formatted))) {
      // The cache is only an optimization, failing to cache is not an error.
      ret = OB_SUCCESS;
    }
  }

  if (OB_SUCC(ret)) {
    properties_ = formatted;
  }
  return ret;
}

int ObExprTokenize::TokenizeParam::try_load_dictionary_for_ik()
{
  int ret = OB_SUCCESS;
  static thread_local bool dictionary_loaded = false;
  bool need_to_load_dic = false;
  storage::ObDicLoaderHandle dic_loader_handle;
  if (OB_FAIL(share::ObFtsIndexBuilderUtil::check_need_to_load_dic(
          parser_name_, need_to_load_dic))) {
  } else if (need_to_load_dic && !dictionary_loaded) {
    if (OB_FAIL(storage::ObGenDicLoader::get_instance().get_dic_loader(
                    ObString::make_string(storage::ObFTSLiteral::PARSER_NAME_IK), // currently only ik, use parser_name_ without version suffix
                    ObCharset::charset_type_by_coll(meta_.get_collation_type()),
                    dic_loader_handle))) {
    } else if (OB_UNLIKELY(!dic_loader_handle.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(dic_loader_handle.get_loader()->try_load_dictionary_in_trans())) {
    } else {
      dictionary_loaded = true;
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
