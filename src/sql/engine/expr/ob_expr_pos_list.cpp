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

#define USING_LOG_PREFIX STORAGE_FTS

#include "sql/engine/expr/ob_expr_pos_list.h"
#include "lib/container/ob_se_array.h"
#include "plugin/sys/ob_plugin_helper.h"
#include "objit/common/ob_item_type.h"
#include "share/ob_fts_pos_list_codec.h"
#include "sql/das/ob_das_domain_utils.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "storage/fts/ob_fts_plugin_helper.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

namespace
{

int construct_default_ft_parser_name(ObIAllocator &allocator, ObString &parser_name)
{
  int ret = OB_SUCCESS;
  char *parser_name_buf = nullptr;
  storage::ObFTParser parser;
  const ObString default_parser_name = ObString::make_string(OB_DEFAULT_FULLTEXT_PARSER_NAME);
  if (OB_ISNULL(parser_name_buf = static_cast<char *>(allocator.alloc(share::OB_PLUGIN_NAME_LENGTH)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate parser name buffer", K(ret));
  } else if (OB_FAIL(plugin::ObPluginHelper::find_ftparser(default_parser_name, parser))) {
    LOG_WARN("failed to find default ft parser", K(ret), K(default_parser_name));
  } else if (OB_FAIL(parser.serialize_to_str(parser_name_buf, share::OB_PLUGIN_NAME_LENGTH))) {
    LOG_WARN("failed to serialize ft parser", K(ret), K(parser));
  } else {
    parser_name = ObString::make_string(parser_name_buf);
  }
  return ret;
}

int build_fulltext_input(
    const ObExpr &raw_ctx,
    ObEvalCtx &eval_ctx,
    ObIAllocator &allocator,
    ObString &fulltext,
    bool &all_null)
{
  int ret = OB_SUCCESS;
  int64_t res_str_len = 0;
  const ObCharsetInfo *cs = nullptr;
  ObSEArray<ObString, 1> ft_parts;
  all_null = true;
  fulltext.reset();
  if (OB_UNLIKELY(raw_ctx.arg_cnt_ <= 0) || OB_ISNULL(raw_ctx.args_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(raw_ctx), KP(raw_ctx.args_));
  } else if (OB_ISNULL(cs = ObCharset::get_charset(raw_ctx.args_[0]->obj_meta_.get_collation_type()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null charset info", K(ret), K(raw_ctx.args_[0]->obj_meta_));
  } else {
    const int64_t mb_max_len = cs->mbmaxlen;
    char mb_separator[mb_max_len];
    int32_t length_of_separator = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < raw_ctx.arg_cnt_; ++i) {
      ObString res;
      common::ObDatum *datum = nullptr;
      if (OB_ISNULL(raw_ctx.args_[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null expr arg", K(ret), K(i), K(raw_ctx.arg_cnt_));
      } else if (OB_FAIL(raw_ctx.args_[i]->eval(eval_ctx, datum))) {
        LOG_WARN("failed to eval expr", K(ret), K(i), K(raw_ctx));
      } else if (OB_ISNULL(datum)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null datum", K(ret), K(i));
      } else if (datum->is_null()) {
      } else if (FALSE_IT(all_null = false)) {
      } else if (FALSE_IT(res = datum->get_string())) {
      } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(allocator,
                                                                   *datum,
                                                                   raw_ctx.args_[i]->datum_meta_,
                                                                   raw_ctx.args_[i]->obj_meta_.has_lob_header(),
                                                                   res))) {
        LOG_WARN("failed to read real string data", K(ret), K(i), K(res));
      } else if (OB_FAIL(ft_parts.push_back(res))) {
        LOG_WARN("failed to push fulltext part", K(ret), K(i), K(res));
      } else {
        res_str_len += ft_parts.at(ft_parts.count() - 1).length();
      }
    }
    wchar_t wide_char = L' ';
    if (OB_FAIL(ret) || all_null) {
    } else if (OB_FAIL(ObCharset::wc_mb(raw_ctx.args_[0]->obj_meta_.get_collation_type(),
                                        wide_char,
                                        mb_separator,
                                        mb_max_len,
                                        length_of_separator))) {
      LOG_WARN("failed to build separator", K(ret), K(mb_max_len));
    } else {
      res_str_len += length_of_separator * (ft_parts.count() - 1);
      if (res_str_len > 0) {
        char *ptr = static_cast<char *>(allocator.alloc(res_str_len));
        if (OB_ISNULL(ptr)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate fulltext buffer", K(ret), K(res_str_len));
        } else {
          char *cur_ptr = ptr;
          for (int64_t i = 0; OB_SUCC(ret) && i < ft_parts.count(); ++i) {
            if (0 != i) {
              MEMCPY(cur_ptr, mb_separator, length_of_separator);
              cur_ptr += length_of_separator;
            }
            MEMCPY(cur_ptr, ft_parts.at(i).ptr(), ft_parts.at(i).length());
            cur_ptr += ft_parts.at(i).length();
          }
          if (OB_SUCC(ret)) {
            fulltext.assign_ptr(ptr, static_cast<int32_t>(res_str_len));
          }
        }
      }
    }
  }
  return ret;
}

int build_dense_pos_list(
    const int64_t doc_length,
    ObIAllocator &allocator,
    ObString &encoded_pos_list)
{
  int ret = OB_SUCCESS;
  common::ObArray<int64_t, common::ObIAllocator &> positions(OB_MALLOC_NORMAL_BLOCK_SIZE, allocator);
  if (doc_length < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid doc length", K(ret), K(doc_length));
  } else if (OB_FAIL(positions.reserve(doc_length))) {
    LOG_WARN("failed to reserve position array", K(ret), K(doc_length));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < doc_length; ++i) {
      if (OB_FAIL(positions.push_back(i + 1))) {
        LOG_WARN("failed to push position", K(ret), K(i), K(doc_length));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(share::ObFTSPositionListStore::encode(positions, allocator, encoded_pos_list))) {
      LOG_WARN("failed to encode position list", K(ret), K(doc_length));
    }
  }
  return ret;
}

} // namespace

ObExprPosList::ObExprPosList(ObIAllocator &allocator)
  : ObFuncExprOperator(allocator, T_FUN_SYS_POS_LIST, N_POS_LIST, MORE_THAN_ZERO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
  need_charset_convert_ = false;
}

int ObExprPosList::calc_result_typeN(
    ObExprResType &type,
    ObExprResType *types,
    int64_t param_num,
    ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSEDx(types, type_ctx);
  if (OB_UNLIKELY(param_num < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument for pos list expr", K(ret), K(param_num));
  } else {
    type.set_varbinary();
    type.set_length(share::ObFTSPositionListStore::MAX_INLINE_ENCODED_LENGTH);
    type.set_collation_level(CS_LEVEL_COERCIBLE);
  }
  return ret;
}

int ObExprPosList::calc_resultN(
    ObObj &result,
    const ObObj *objs_array,
    int64_t param_num,
    ObExprCtx &expr_ctx) const
{
  UNUSEDx(result, objs_array, param_num, expr_ctx);
  return OB_NOT_SUPPORTED;
}

int ObExprPosList::cg_expr(
    ObExprCGCtx &expr_cg_ctx,
    const ObRawExpr &raw_expr,
    ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSEDx(expr_cg_ctx, raw_expr);
  if (OB_UNLIKELY(rt_expr.arg_cnt_ < 1) || OB_ISNULL(rt_expr.args_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(rt_expr.arg_cnt_), KP(rt_expr.args_), K(rt_expr.type_));
  } else {
    rt_expr.eval_func_ = generate_pos_list;
  }
  return ret;
}

int ObExprPosList::generate_pos_list(
    const ObExpr &raw_ctx,
    ObEvalCtx &eval_ctx,
    ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObEvalCtx::TempAllocGuard tmp_alloc_guard(eval_ctx);
  ObExprStrResAlloc res_alloc(raw_ctx, eval_ctx);
  ObString fulltext;
  ObString parser_name;
  ObString encoded_pos_list;
  ObObjMeta token_meta;
  ObFTPosListParser pos_list_parser;
  storage::ObFTWordPositionMap word_infos;
  bool all_null = true;
  int64_t doc_length = 0;
  static constexpr int64_t FT_MAX_WORD_BUCKET = 997;
  if (OB_UNLIKELY(raw_ctx.arg_cnt_ <= 0) || OB_ISNULL(raw_ctx.args_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(raw_ctx.arg_cnt_), KP(raw_ctx.args_));
  } else if (OB_FAIL(build_fulltext_input(raw_ctx,
                                          eval_ctx,
                                          tmp_alloc_guard.get_allocator(),
                                          fulltext,
                                          all_null))) {
    LOG_WARN("failed to build fulltext input", K(ret), K(raw_ctx.arg_cnt_));
  } else if (all_null) {
    expr_datum.set_null();
  } else if (fulltext.empty()) {
    if (OB_FAIL(build_dense_pos_list(0, res_alloc, encoded_pos_list))) {
      LOG_WARN("failed to encode empty position list", K(ret));
    } else {
      expr_datum.set_string(encoded_pos_list);
    }
  } else {
    const int64_t ft_word_bkt_cnt = MIN(MAX(fulltext.length() / 10, 2), FT_MAX_WORD_BUCKET);
    token_meta.set_varchar();
    token_meta.set_collation_type(raw_ctx.args_[0]->obj_meta_.get_collation_type());
    if (OB_FAIL(construct_default_ft_parser_name(tmp_alloc_guard.get_allocator(), parser_name))) {
      LOG_WARN("failed to construct default parser name", K(ret));
    } else if (OB_FAIL(pos_list_parser.init(&tmp_alloc_guard.get_allocator(), parser_name, ObString()))) {
      LOG_WARN("failed to init pos list parser", K(ret), K(parser_name));
    } else if (OB_FAIL(word_infos.create(ft_word_bkt_cnt, common::ObMemAttr("ExprPosList")))) {
      LOG_WARN("failed to create word position map", K(ret), K(ft_word_bkt_cnt));
    } else if (OB_FAIL(pos_list_parser.segment(token_meta,
                                               fulltext.ptr(),
                                               fulltext.length(),
                                               doc_length,
                                               word_infos))) {
      LOG_WARN("failed to segment fulltext for pos list", K(ret), K(parser_name), K(fulltext));
    } else if (OB_FAIL(build_dense_pos_list(doc_length, res_alloc, encoded_pos_list))) {
      LOG_WARN("failed to encode dense position list", K(ret), K(doc_length));
    } else {
      expr_datum.set_string(encoded_pos_list);
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
