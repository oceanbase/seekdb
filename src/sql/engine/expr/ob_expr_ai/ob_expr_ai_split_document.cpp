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
#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_split_document.h"
#include "sql/engine/ob_exec_context.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprAISplitDocument::ObExprAISplitDocument(common::ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_AI_SPLIT_DOCUMENT, N_AI_SPLIT_DOCUMENT, 2,
                       NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprAISplitDocument::~ObExprAISplitDocument()
{
}

int ObExprAISplitDocument::calc_result_type1(ObExprResType &type,
                                             ObExprResType &content,
                                             common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!content.is_string_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid content type for AI_SPLIT_DOCUMENT", K(ret), K(content.get_type()));
  } else {
    content.set_calc_type(ObVarcharType);
    type.set_int();
  }
  return ret;
}

int ObExprAISplitDocument::calc_result_type2(ObExprResType &type,
                                             ObExprResType &content,
                                             ObExprResType &params,
                                             common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!content.is_string_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid content type for AI_SPLIT_DOCUMENT", K(ret), K(content.get_type()));
  } else if (OB_UNLIKELY(!params.is_string_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid params type for AI_SPLIT_DOCUMENT", K(ret), K(params.get_type()));
  } else {
    content.set_calc_type(ObVarcharType);
    params.set_calc_type(ObVarcharType);
    type.set_int();
  }
  return ret;
}

int ObExprAISplitDocument::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                   const ObRawExpr &raw_expr,
                                   ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  if (OB_UNLIKELY(raw_expr.get_param_count() < 1 || raw_expr.get_param_count() > 2)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid param count for AI_SPLIT_DOCUMENT", K(ret), K(raw_expr.get_param_count()));
  } else if (OB_ISNULL(raw_expr.get_param_expr(0))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null param expr", K(ret));
  } else {
    rt_expr.eval_func_ = ObExprAISplitDocument::eval_split_document;
  }
  return ret;
}

// ---------- JSON 迷你解析：仅支持 {"key":value} 形式的扁平对象 ----------
static int64_t json_get_int_field(const ObString &json, const char *key, int64_t def_val)
{
  const char *p = json.ptr();
  int64_t len = json.length();
  int64_t key_len = static_cast<int64_t>(strlen(key));
  for (int64_t i = 0; i + key_len < len; ++i) {
    if (p[i] == '"' && 0 == strncmp(p + i + 1, key, key_len) && p[i + key_len + 1] == '"') {
      int64_t j = i + key_len + 1;
      if (j < len && p[j] == '"') { ++j; }
      while (j < len && (p[j] == ' ' || p[j] == '\t' || p[j] == ':')) { ++j; }
      int64_t val = 0;
      bool neg = false;
      bool started = false;
      if (j < len && p[j] == '-') { neg = true; ++j; }
      while (j < len && p[j] >= '0' && p[j] <= '9') {
        val = val * 10 + (p[j] - '0');
        ++j;
        started = true;
      }
      if (started) {
        return neg ? -val : val;
      }
      return def_val;
    }
  }
  return def_val;
}

static bool json_get_str_field(const ObString &json, const char *key, ObString &val)
{
  const char *p = json.ptr();
  int64_t len = json.length();
  int64_t key_len = static_cast<int64_t>(strlen(key));
  for (int64_t i = 0; i + key_len < len; ++i) {
    if (p[i] == '"' && 0 == strncmp(p + i + 1, key, key_len) && p[i + key_len + 1] == '"') {
      int64_t j = i + key_len + 1;
      if (j < len && p[j] == '"') { ++j; }
      while (j < len && (p[j] == ' ' || p[j] == '\t' || p[j] == ':')) { ++j; }
      if (j < len && p[j] == '"') {
        int64_t start = ++j;
        while (j < len && p[j] != '"') { ++j; }
        val.assign_ptr(p + start, static_cast<int32_t>(j - start));
        return true;
      }
      return false;
    }
  }
  return false;
}

static inline bool ai_split_is_space(char c)
{
  return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

// 切分核心：将 content 切成 chunk 数组
// 原子单元：
//   - text/sentence: 以 '.' 结尾的句子（句号后空白跳过）
//   - text/word: 空白分隔的词
//   - markdown: '# ' 开头的行作为 section 标题，非标题行按 '.' 切句子，每原子 = 标题 + '\n' + 句子
// 窗口：size = max_cnt, step = max_cnt - overlap（step < 1 时取 1），最后不足 size 也输出
int ObExprAISplitDocument::do_split(common::ObIAllocator &allocator,
                                    const common::ObString &content,
                                    bool is_markdown,
                                    bool is_word,
                                    int64_t max_cnt,
                                    int64_t overlap,
                                    ObExprAISplitDocumentCtx &out)
{
  int ret = OB_SUCCESS;
  const char *p = content.ptr();
  int64_t len = content.length();
  if (max_cnt < 1) {
    max_cnt = 1;
  }
  int64_t step = max_cnt - overlap;
  if (step < 1) {
    step = 1;
  }

  // 第一遍：收集原子（offset, 文本指针/长度, 是否需拼接）
  // 使用定长数组预分配上限，避免复杂扩容。
  // 原子文本分两类：直接引用 content 内切片（sentence/word），或由标题+句子拼接（markdown）。
  struct Atom {
    int64_t offset_;
    int64_t len_;
    ObString text_;
  };
  // 原子数量上界：len / 2 + 8（每个原子至少 1 字节 + 可能的分隔符）
  int64_t atom_cap = len / 2 + 8;
  Atom *atoms = NULL;
  if (atom_cap > 0) {
    if (OB_ISNULL(atoms = static_cast<Atom *>(allocator.alloc(sizeof(Atom) * atom_cap)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc atoms", K(ret), K(atom_cap));
    }
  }
  int64_t atom_cnt = 0;

  if (OB_SUCC(ret)) {
    if (is_markdown) {
      // 按行遍历；'#' 开头的行作为 section 标题
      ObString section;
      int64_t line_start = 0;
      while (line_start < len && OB_SUCC(ret)) {
        int64_t line_end = line_start;
        while (line_end < len && p[line_end] != '\n') { ++line_end; }
        // 行文本 [line_start, line_end)
        const char *lp = p + line_start;
        int64_t ll = line_end - line_start;
        int64_t k = 0;
        while (k < ll && ai_split_is_space(lp[k])) { ++k; }
        if (k < ll && lp[k] == '#') {
          // 标题行：整行（去掉尾部换行）作为 section
          ObString tmp(static_cast<int64_t>(ll), lp);
          if (OB_FAIL(ob_write_string(allocator, tmp, section))) {
            LOG_WARN("failed to write section", K(ret));
          }
        } else if (ll > 0) {
          // 非标题行：行内按 '.' 切句子
          int64_t pos = line_start;
          while (pos < line_end && OB_SUCC(ret)) {
            while (pos < line_end && ai_split_is_space(p[pos])) { ++pos; }
            if (pos >= line_end) { break; }
            int64_t start = pos;
            while (pos < line_end && p[pos] != '.') { ++pos; }
            int64_t end = (pos < line_end) ? (pos + 1) : line_end; // 含句号
            if (end <= start) { end = line_end; }
            if (OB_UNLIKELY(atom_cnt >= atom_cap)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("atom cap exceeded", K(ret), K(atom_cnt), K(atom_cap));
            } else {
              ObString sentence(end - start, p + start);
              // 拼接 section + "\n" + sentence
              int64_t total_len = section.length() + 1 + sentence.length();
              char *buf = static_cast<char *>(allocator.alloc(total_len));
              if (OB_ISNULL(buf)) {
                ret = OB_ALLOCATE_MEMORY_FAILED;
                LOG_WARN("failed to alloc atom text", K(ret), K(total_len));
              } else {
                MEMCPY(buf, section.ptr(), section.length());
                buf[section.length()] = '\n';
                MEMCPY(buf + section.length() + 1, sentence.ptr(), sentence.length());
                atoms[atom_cnt].offset_ = start;
                atoms[atom_cnt].len_ = total_len;
                atoms[atom_cnt].text_.assign_ptr(buf, static_cast<int32_t>(total_len));
                ++atom_cnt;
              }
            }
            pos = end;
          }
        }
        line_start = (line_end < len) ? (line_end + 1) : len;
      }
    } else if (is_word) {
      // 空白分词
      int64_t pos = 0;
      while (pos < len && OB_SUCC(ret)) {
        while (pos < len && ai_split_is_space(p[pos])) { ++pos; }
        if (pos >= len) { break; }
        int64_t start = pos;
        while (pos < len && !ai_split_is_space(p[pos])) { ++pos; }
        if (OB_UNLIKELY(atom_cnt >= atom_cap)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("atom cap exceeded", K(ret), K(atom_cnt), K(atom_cap));
        } else {
          atoms[atom_cnt].offset_ = start;
          atoms[atom_cnt].len_ = pos - start;
          atoms[atom_cnt].text_.assign_ptr(p + start, static_cast<int32_t>(pos - start));
          ++atom_cnt;
        }
      }
    } else {
      // 按句子切分：'.' 结尾（含句号），句号后空白跳过
      int64_t pos = 0;
      while (pos < len && OB_SUCC(ret)) {
        while (pos < len && ai_split_is_space(p[pos])) { ++pos; }
        if (pos >= len) { break; }
        int64_t start = pos;
        while (pos < len && p[pos] != '.') { ++pos; }
        int64_t end = (pos < len) ? (pos + 1) : len; // 含句号
        if (end <= start) { end = len; }
        if (OB_UNLIKELY(atom_cnt >= atom_cap)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("atom cap exceeded", K(ret), K(atom_cnt), K(atom_cap));
        } else {
          atoms[atom_cnt].offset_ = start;
          atoms[atom_cnt].len_ = end - start;
          atoms[atom_cnt].text_.assign_ptr(p + start, static_cast<int32_t>(end - start));
          ++atom_cnt;
        }
        pos = end;
      }
    }
  }

  // 第二遍：滑动窗口生成 chunk
  if (OB_SUCC(ret)) {
    int64_t chunk_cnt = 0;
    {
      int64_t i = 0;
      while (i < atom_cnt) {
        ++chunk_cnt;
        int64_t j = i + max_cnt;
        if (j > atom_cnt) { j = atom_cnt; }
        i += step;
      }
    }
    out.chunk_cnt_ = chunk_cnt;
    if (chunk_cnt > 0) {
      if (OB_ISNULL(out.chunk_ids_ = static_cast<int64_t *>(allocator.alloc(sizeof(int64_t) * chunk_cnt))) ||
          OB_ISNULL(out.chunk_offsets_ = static_cast<int64_t *>(allocator.alloc(sizeof(int64_t) * chunk_cnt))) ||
          OB_ISNULL(out.chunk_lengths_ = static_cast<int64_t *>(allocator.alloc(sizeof(int64_t) * chunk_cnt))) ||
          OB_ISNULL(out.chunk_texts_ = static_cast<ObString *>(allocator.alloc(sizeof(ObString) * chunk_cnt)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc chunk arrays", K(ret), K(chunk_cnt));
      } else {
        int64_t chunk_idx = 0;
        int64_t i = 0;
        while (OB_SUCC(ret) && i < atom_cnt) {
          int64_t j = i + max_cnt;
          if (j > atom_cnt) { j = atom_cnt; }
          // 拼接窗口文本
          int64_t total = 0;
          for (int64_t t = i; t < j; ++t) {
            if (t > i) {
              total += 1; // 分隔符：word/sentence 用空格，markdown 原子已含标题用换行
            }
            total += atoms[t].len_;
          }
          char *buf = static_cast<char *>(allocator.alloc(total > 0 ? total : 1));
          if (OB_ISNULL(buf)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to alloc chunk text", K(ret), K(total));
          } else {
            char *cur = buf;
            for (int64_t t = i; t < j; ++t) {
              if (t > i) {
                *cur++ = is_markdown ? '\n' : ' ';
              }
              MEMCPY(cur, atoms[t].text_.ptr(), atoms[t].text_.length());
              cur += atoms[t].text_.length();
            }
            out.chunk_ids_[chunk_idx] = chunk_idx;
            out.chunk_offsets_[chunk_idx] = atoms[i].offset_;
            out.chunk_lengths_[chunk_idx] = total;
            out.chunk_texts_[chunk_idx].assign_ptr(buf, static_cast<int32_t>(total));
            ++chunk_idx;
          }
          i += step;
        }
        if (OB_SUCC(ret) && chunk_idx != chunk_cnt) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("chunk count mismatch", K(ret), K(chunk_idx), K(chunk_cnt));
        }
      }
    }
  }
  return ret;
}

int ObExprAISplitDocument::eval_split_document(const ObExpr &expr,
                                               ObEvalCtx &ctx,
                                               ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObExprAISplitDocumentCtx *split_ctx = NULL;
  uint64_t op_id = expr.expr_ctx_id_;
  ObExecContext &exec_ctx = ctx.exec_ctx_;
  LOG_WARN("[AI_SPLIT_DBG] eval enter", K(op_id), K(expr.arg_cnt_));
  if (OB_ISNULL(split_ctx = static_cast<ObExprAISplitDocumentCtx *>(
              exec_ctx.get_expr_op_ctx(op_id)))) {
    if (OB_FAIL(exec_ctx.create_expr_op_ctx(op_id, split_ctx))) {
      LOG_WARN("failed to create expr op ctx", K(ret), K(op_id));
    }
  }
  if (OB_SUCC(ret) && !split_ctx->inited_) {
    ObDatum *content_datum = NULL;
    ObDatum *params_datum = NULL;
    if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 2)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected arg_cnt", K(ret), K(expr.arg_cnt_));
    } else if (OB_FAIL(expr.eval_param_value(ctx, content_datum, params_datum))) {
      LOG_WARN("eval params failed", K(ret));
    } else {
      bool is_markdown = false;
      bool is_word = false;
      int64_t max_cnt = 1;
      int64_t overlap = 0;
      if (expr.arg_cnt_ == 2) {
        if (!params_datum->is_null()) {
          ObString params_json(params_datum->get_string());
          ObString type_val;
          ObString by_val;
          if (json_get_str_field(params_json, "type", type_val)) {
            if (type_val.length() >= 8 && 0 == type_val.case_compare("markdown")) {
              is_markdown = true;
            }
          }
          if (json_get_str_field(params_json, "by", by_val)) {
            if (by_val.length() >= 4 && 0 == by_val.case_compare("word")) {
              is_word = true;
            }
          }
          max_cnt = json_get_int_field(params_json, "max", 1);
          overlap = json_get_int_field(params_json, "overlap", 0);
          if (max_cnt < 1) { max_cnt = 1; }
          if (overlap < 0) { overlap = 0; }
        }
      }
      if (OB_SUCC(ret)) {
        if (content_datum->is_null()) {
          split_ctx->chunk_cnt_ = 0;
        } else {
          ObString content(content_datum->get_string());
          if (OB_FAIL(do_split(exec_ctx.get_allocator(), content,
                               is_markdown, is_word, max_cnt, overlap, *split_ctx))) {
            LOG_WARN("ai split document failed", K(ret));
          }
        }
        LOG_WARN("[AI_SPLIT_DBG] init done", K(op_id), K(split_ctx->chunk_cnt_), K(is_markdown), K(is_word), K(max_cnt), K(overlap));
        split_ctx->inited_ = true;
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(split_ctx->row_idx_ >= split_ctx->chunk_cnt_)) {
      LOG_WARN("[AI_SPLIT_DBG] iter end", K(op_id), K(split_ctx->row_idx_), K(split_ctx->chunk_cnt_), K(split_ctx->inited_));
      ret = OB_ITER_END;
    } else {
      int64_t idx = split_ctx->row_idx_;
      split_ctx->cur_id_ = split_ctx->chunk_ids_[idx];
      split_ctx->cur_offset_ = split_ctx->chunk_offsets_[idx];
      split_ctx->cur_length_ = split_ctx->chunk_lengths_[idx];
      split_ctx->cur_text_ = split_ctx->chunk_texts_[idx];
      LOG_WARN("[AI_SPLIT_DBG] row", K(op_id), K(idx), K(split_ctx->chunk_cnt_), K(split_ctx->inited_), K(split_ctx->cur_id_), K(split_ctx->cur_offset_), K(split_ctx->cur_length_), K(split_ctx->cur_text_));
      expr_datum.set_int(split_ctx->cur_id_);
      ++split_ctx->row_idx_;
    }
  }
  return ret;
}

} /* namespace sql */
} /* namespace oceanbase */
