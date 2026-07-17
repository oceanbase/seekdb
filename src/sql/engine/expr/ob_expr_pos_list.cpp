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
#include "lib/checksum/ob_crc64.h"
#include "lib/container/ob_se_array.h"
#include "lib/utility/serialization.h"
#include "plugin/sys/ob_plugin_helper.h"
#include "objit/common/ob_item_type.h"
#include "share/ob_fts_pos_list_codec.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "storage/fts/ob_fts_plugin_helper.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

namespace
{

constexpr int64_t POS_LIST_INLINE_ENCODED_LIMIT = 16 * 1024;
static_assert(POS_LIST_INLINE_ENCODED_LIMIT == share::ObFTSPositionListStore::MAX_INLINE_ENCODED_LENGTH,
              "pos list inline length limit mismatch");

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

int calc_dense_pos_list_payload_len(
    const int64_t doc_length,
    int64_t &payload_len)
{
  int ret = OB_SUCCESS;
  if (doc_length < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid doc length", K(ret), K(doc_length));
  } else {
    payload_len = 0;
    int64_t range_begin = 1;
    for (int64_t byte_len = 1; byte_len <= 9 && range_begin <= doc_length; ++byte_len) {
      const int64_t range_end = MIN(doc_length, (1LL << (7 * byte_len)) - 1);
      payload_len += (range_end - range_begin + 1) * byte_len;
      range_begin = range_end + 1;
    }
    if (range_begin <= doc_length) {
      payload_len += (doc_length - range_begin + 1) * 10;
    }
  }
  return ret;
}

int encode_dense_pos_list(
    const int64_t doc_length,
    ObIAllocator &allocator,
    ObString &encoded_pos_list)
{
  int ret = OB_SUCCESS;
  int64_t payload_len = 0;
  int64_t total_len = 0;
  int64_t pos = 0;
  int64_t checksum_pos = 0;
  const share::ObFTSPositionListStore::CodecType codec_type = share::ObFTSPositionListStore::VARIABLE_INT64;
  if (OB_FAIL(calc_dense_pos_list_payload_len(doc_length, payload_len))) {
    LOG_WARN("failed to calculate dense pos list payload length", K(ret), K(doc_length));
  } else if (FALSE_IT(total_len =
                          serialization::encoded_length_i16(share::ObFTSPositionListStore::MAGIC_NUMBER)
                        + serialization::encoded_length_i16(share::ObFTSPositionListStore::VERSION)
                        + serialization::encoded_length_i16(codec_type)
                        + serialization::encoded_length_vi64(payload_len)
                        + serialization::encoded_length_i64(static_cast<int64_t>(0))
                        + serialization::encoded_length_vi64(doc_length)
                        + payload_len)) {
  } else if (OB_UNLIKELY(total_len > POS_LIST_INLINE_ENCODED_LIMIT)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("dense pos list exceeds inline length limit",
             K(ret),
             K(doc_length),
             K(payload_len),
             K(total_len),
             "max_inline_len",
             POS_LIST_INLINE_ENCODED_LIMIT);
  } else {
    char *buf = static_cast<char *>(allocator.alloc(total_len));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate dense pos list buffer", K(ret), K(total_len), K(doc_length));
    } else if (OB_FAIL(serialization::encode_i16(buf, total_len, pos, share::ObFTSPositionListStore::MAGIC_NUMBER))
               || OB_FAIL(serialization::encode_i16(buf, total_len, pos, share::ObFTSPositionListStore::VERSION))
               || OB_FAIL(serialization::encode_i16(buf, total_len, pos, codec_type))
               || OB_FAIL(serialization::encode_vi64(buf, total_len, pos, payload_len))) {
      LOG_WARN("failed to encode dense pos list header", K(ret), K(total_len), K(doc_length));
    } else {
      checksum_pos = pos;
      int64_t checksum_placeholder = 0;
      if (OB_FAIL(serialization::encode_i64(buf, total_len, pos, checksum_placeholder))
          || OB_FAIL(serialization::encode_vi64(buf, total_len, pos, doc_length))) {
        LOG_WARN("failed to encode dense pos list metadata", K(ret), K(total_len), K(doc_length));
      } else {
        for (int64_t i = 1; OB_SUCC(ret) && i <= doc_length; ++i) {
          if (OB_FAIL(serialization::encode_vi64(buf, total_len, pos, i))) {
            LOG_WARN("failed to encode dense pos list element", K(ret), K(i), K(doc_length), K(total_len), K(pos));
          }
        }
      }
      if (OB_SUCC(ret)) {
        const int64_t header_len = checksum_pos + serialization::encoded_length_i64(static_cast<int64_t>(0))
            + serialization::encoded_length_vi64(doc_length);
        const int64_t actual_payload_len = pos - header_len;
        if (OB_UNLIKELY(actual_payload_len != payload_len)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected dense pos list payload length", K(ret), K(actual_payload_len), K(payload_len), K(doc_length));
        } else {
          const int64_t checksum = payload_len <= 0 ? 0 : static_cast<int64_t>(common::ob_crc64(buf + header_len, payload_len));
          int64_t tmp_pos = checksum_pos;
          if (OB_FAIL(serialization::encode_i64(buf, total_len, tmp_pos, checksum))) {
            LOG_WARN("failed to patch dense pos list checksum", K(ret), K(checksum), K(total_len), K(doc_length));
          } else {
            encoded_pos_list.assign_ptr(buf, static_cast<int32_t>(pos));
          }
        }
      }
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
    type.set_length(POS_LIST_INLINE_ENCODED_LIMIT);
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
  storage::ObFTParseHelper parse_helper;
  storage::ObFTWordMap word_counts;
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
    if (OB_FAIL(encode_dense_pos_list(0, res_alloc, encoded_pos_list))) {
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
    } else if (OB_FAIL(parse_helper.init(&tmp_alloc_guard.get_allocator(), parser_name, ObString()))) {
      LOG_WARN("failed to init parse helper", K(ret), K(parser_name));
    } else if (OB_FAIL(word_counts.create(ft_word_bkt_cnt, common::ObMemAttr("ExprPosList")))) {
      LOG_WARN("failed to create word count map", K(ret), K(ft_word_bkt_cnt));
    } else if (OB_FAIL(parse_helper.segment(token_meta,
                                            fulltext.ptr(),
                                            fulltext.length(),
                                            doc_length,
                                            word_counts))) {
      LOG_WARN("failed to segment fulltext for pos list", K(ret), K(parser_name), K(fulltext));
    } else if (OB_FAIL(encode_dense_pos_list(doc_length, res_alloc, encoded_pos_list))) {
      LOG_WARN("failed to encode dense position list", K(ret), K(doc_length));
    } else {
      expr_datum.set_string(encoded_pos_list);
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
