/**
 * OceanBase seekdb - Document AI: AI_SPLIT_DOCUMENT implementation.
 *
 * Splits text/markdown content into chunk rows. Drives an ObExprOperatorCtx
 * that materializes all chunks on first eval, then returns one chunk per call
 * (OB_ITER_END when exhausted). The ObFunctionTableOp reads 4 column values
 * (chunk_id/offset/length/text) from the rt_ctx.
 *
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_split_document.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_tree.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

// ===========================================================================
// Pure splitting logic (anonymous namespace). Operates on byte offsets into
// the original content; chunk text is deep-copied into the rt_ctx arena.
// ===========================================================================
namespace
{

// Sentence terminators: ASCII . ! ? and CJK fullwidth 。 ！ ？
// A sentence boundary = a terminator followed by whitespace (space/tab/nl/cr)
// or EOT. The terminator stays with the current sentence; the boundary
// whitespace is skipped (NOT in any unit). The whitespace requirement avoids
// mis-splitting "3.14", "Mr.", "e.g.".
struct SplitParams {
  bool is_markdown = true;   // type: markdown (default) / text
  bool by_sentence = false;  // by: word (default) / sentence
  int64_t max_units = 256;
  int64_t overlap = 0;
};

// returns true if p[0..n) is the start of a CJK fullwidth terminator
inline bool is_cjk_terminator(const char *p, int64_t n)
{
  if (n < 3) { return false; }
  // 。 = E3 80 82, ！ = EF BC 81, ？ = EF BC 9F
  if (p[0] == (char)0xE3 && p[1] == (char)0x80 && p[2] == (char)0x82) { return true; }
  if (p[0] == (char)0xEF && p[1] == (char)0xBC && (p[2] == (char)0x81 || p[2] == (char)0x9F)) { return true; }
  return false;
}

inline int64_t terminator_len(const char *p, int64_t n)
{
  if (n >= 1 && (p[0] == '.' || p[0] == '!' || p[0] == '?')) { return 1; }
  if (is_cjk_terminator(p, n)) { return 3; }
  return 0;
}

inline bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// One unit: a sentence or a word. off/len are byte offsets within the text
// passed to split_units; the unit references original bytes (no copy).
struct Unit {
  int64_t off;
  int64_t len;     // length including trailing terminator (sentence) / word bytes
  TO_STRING_KV(K(off), K(len));
};

// Split into sentences. Each sentence ends at a terminator followed by ws/EOT.
// Terminator belongs to the sentence. Boundary whitespace is NOT in any unit.
int split_sentences(const ObString &text, ObIArray<Unit> &units)
{
  int ret = OB_SUCCESS;
  int64_t n = text.length();
  const char *p = text.ptr();
  int64_t sent_start = -1;
  for (int64_t i_scan = 0; i_scan < n; ) {
    if (sent_start < 0) {
      // skip leading whitespace between sentences
      if (is_ws(p[i_scan])) { i_scan++; continue; }
      sent_start = i_scan;
    }
    int64_t tlen = terminator_len(p + i_scan, n - i_scan);
    if (tlen > 0) {
      int64_t after = i_scan + tlen;
      bool boundary = (after >= n) || is_ws(p[after]);
      if (boundary) {
        Unit u; u.off = sent_start; u.len = after - sent_start;
        OZ (units.push_back(u));
        sent_start = -1;
        i_scan = after;
        // skip the single boundary whitespace (handled at next loop top)
        continue;
      }
    }
    i_scan++;
  }
  if (sent_start >= 0) {
    // trailing text without a terminator: emit as a final sentence
    Unit u; u.off = sent_start; u.len = n - sent_start;
    OZ (units.push_back(u));
  }
  return ret;
}

// Split into words by whitespace. Word bytes are the non-ws runs.
int split_words(const ObString &text, ObIArray<Unit> &units)
{
  int ret = OB_SUCCESS;
  int64_t n = text.length();
  const char *p = text.ptr();
  int64_t i = 0;
  while (i < n) {
    while (i < n && is_ws(p[i])) { i++; }
    if (i >= n) { break; }
    int64_t start = i;
    while (i < n && !is_ws(p[i])) { i++; }
    Unit u; u.off = start; u.len = i - start;
    OZ (units.push_back(u));
  }
  return ret;
}

// Build chunk_text for a window of sentence units: the contiguous original
// bytes from first unit start to last unit end (preserves inter-sentence
// whitespace as-is, matching the .result for sentence mode).
// offset = first unit's off (+ base_off); length = window byte span.
int emit_sentence_chunks(ObIAllocator &alloc, ObIArray<Unit> &units,
                         int64_t max_u, int64_t overlap, int64_t base_off,
                         const ObString &body, int64_t &chunk_id,
                         ObIArray<ObExprAISplitDocumentCtx::ChunkInfo> &out)
{
  int ret = OB_SUCCESS;
  int64_t step = (overlap >= max_u) ? 1 : (max_u - overlap);
  int64_t n = units.count();
  for (int64_t start = 0; start < n; start += step) {
    int64_t end = start + max_u;
    if (end > n) { end = n; }
    const Unit &u0 = units.at(start);
    const Unit &uLast = units.at(end - 1);
    int64_t text_off = u0.off;
    int64_t text_len = (uLast.off + uLast.len) - u0.off;
    ObString src(text_len, body.ptr() + text_off);
    // deep copy into arena
    char *buf = static_cast<char *>(alloc.alloc(text_len));
    if (OB_ISNULL(buf)) { ret = OB_ALLOCATE_MEMORY_FAILED; LOG_WARN("alloc chunk text failed", K(ret)); }
    else {
      MEMCPY(buf, src.ptr(), text_len);
      ObExprAISplitDocumentCtx::ChunkInfo c;
      c.chunk_id_ = chunk_id++;
      c.chunk_offset_ = base_off + text_off;
      c.chunk_length_ = text_len;
      c.chunk_text_.assign_ptr(buf, text_len);
      OZ (out.push_back(c));
    }
    if (end >= n) { break; }
  }
  return ret;
}

// Build chunk_text for a window of word units: words joined by a single space.
// offset = first word's off (+ base_off); length = joined length.
int emit_word_chunks(ObIAllocator &alloc, ObIArray<Unit> &units,
                     int64_t max_u, int64_t overlap, int64_t base_off,
                     const ObString &body, int64_t &chunk_id,
                     ObIArray<ObExprAISplitDocumentCtx::ChunkInfo> &out)
{
  int ret = OB_SUCCESS;
  int64_t step = (overlap >= max_u) ? 1 : (max_u - overlap);
  int64_t n = units.count();
  for (int64_t start = 0; start < n; start += step) {
    int64_t end = start + max_u;
    if (end > n) { end = n; }
    // joined length = sum of word lens + (count-1) spaces
    int64_t joined = 0;
    for (int64_t k = start; k < end; ++k) { joined += units.at(k).len; }
    joined += (end - start - 1);
    char *buf = static_cast<char *>(alloc.alloc(joined));
    if (OB_ISNULL(buf)) { ret = OB_ALLOCATE_MEMORY_FAILED; LOG_WARN("alloc failed", K(ret)); }
    else {
      int64_t off = 0;
      for (int64_t k = start; k < end; ++k) {
        if (k > start) { buf[off++] = ' '; }
        const Unit &u = units.at(k);
        MEMCPY(buf + off, body.ptr() + u.off, u.len);
        off += u.len;
      }
      ObExprAISplitDocumentCtx::ChunkInfo c;
      c.chunk_id_ = chunk_id++;
      c.chunk_offset_ = base_off + units.at(start).off;
      c.chunk_length_ = joined;
      c.chunk_text_.assign_ptr(buf, joined);
      OZ (out.push_back(c));
    }
    if (end >= n) { break; }
  }
  return ret;
}

// Parse a markdown body into sections by ATX headings (1-6 '#' at line start,
// followed by space/tab/EOL). heading includes the trailing '\n'. For text
// before the first heading, heading is empty.
struct Section {
  ObString heading;     // includes trailing '\n', or empty
  ObString body;        // body text of this section (after heading line)
  int64_t body_off;     // body's byte offset in the original doc
  TO_STRING_KV(K(heading), K(body), K(body_off));
};

int split_markdown_sections(const ObString &doc, ObIArray<Section> &sections)
{
  int ret = OB_SUCCESS;
  int64_t n = doc.length();
  const char *p = doc.ptr();
  int64_t i = 0;
  int64_t cur_body_start = 0;
  ObString cur_heading;  // empty = no heading yet
  auto flush = [&](int64_t body_end) -> int {
    int r = OB_SUCCESS;
    Section s;
    s.heading = cur_heading;
    s.body.assign_ptr(p + cur_body_start, body_end - cur_body_start);
    s.body_off = cur_body_start;
    r = sections.push_back(s);
    return r;
  };
  while (i < n) {
    // detect heading at line start: 1-6 '#', then space/tab or EOL
    int64_t hashes = 0;
    while (i + hashes < n && p[i + hashes] == '#' && hashes < 6) { hashes++; }
    bool at_line_start = (i == 0) || (p[i - 1] == '\n');
    bool valid = at_line_start && hashes >= 1
                 && (i + hashes == n || p[i + hashes] == ' ' || p[i + hashes] == '\t'
                     || p[i + hashes] == '\n');
    if (valid) {
      // flush previous section body up to i (start of heading line)
      OZ (flush(i));
      // heading line = from i to end of line (inclusive '\n')
      int64_t line_end = i;
      while (line_end < n && p[line_end] != '\n') { line_end++; }
      if (line_end < n) { line_end++; }  // include '\n'
      cur_heading.assign_ptr(p + i, line_end - i);
      cur_body_start = line_end;
      i = line_end;
    } else {
      i++;
    }
  }
  // flush trailing section
  OZ (flush(n));
  return ret;
}

// Parse the params JSON string into SplitParams with defaults + validation.
// NULL/empty params => all defaults. Invalid => OB_INVALID_ARGUMENT.
// Missing keys keep defaults (do NOT poison ret) -- cases 1 & 3 omit overlap.
int parse_params(const ObString &params_str, SplitParams &sp)
{
  int ret = OB_SUCCESS;
  if (params_str.empty()) { return ret; }  // NULL/empty => all defaults
  ObArenaAllocator alloc;
  ObIJsonBase *jb = NULL;
  if (OB_FAIL(ObJsonBaseFactory::get_json_base(&alloc, params_str,
          ObJsonInType::JSON_TREE, ObJsonInType::JSON_TREE, jb))) {
    LOG_WARN("ai_split_document: parse params json failed", K(ret));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(jb) || jb->json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ai_split_document: params must be a json object", K(ret));
  } else {
    ObJsonObject *obj = static_cast<ObJsonObject *>(jb);
    ObJsonNode *val = NULL;
    // type: text / markdown (default markdown)
    if (OB_SUCC(ret)) {
      val = obj->get_value("type");
      if (NULL != val) {
        ObString t(val->get_data_length(), val->get_data());
        if (t.case_compare("text") == 0) { sp.is_markdown = false; }
        else if (t.case_compare("markdown") == 0) { sp.is_markdown = true; }
        else { ret = OB_INVALID_ARGUMENT; LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document, type must be text or markdown"); }
      }
    }
    // by: word (default) / sentence
    if (OB_SUCC(ret)) {
      val = obj->get_value("by");
      if (NULL != val) {
        ObString b(val->get_data_length(), val->get_data());
        if (b.case_compare("word") == 0) { sp.by_sentence = false; }
        else if (b.case_compare("sentence") == 0) { sp.by_sentence = true; }
        else { ret = OB_INVALID_ARGUMENT; LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document, by must be word or sentence"); }
      }
    }
    // max
    if (OB_SUCC(ret)) {
      val = obj->get_value("max");
      if (NULL != val) { sp.max_units = val->get_int(); }
    }
    // overlap
    if (OB_SUCC(ret)) {
      val = obj->get_value("overlap");
      if (NULL != val) { sp.overlap = val->get_int(); }
    }
    // validation: max>=1 && 0<=overlap<max
    if (OB_SUCC(ret) && (sp.max_units < 1 || sp.overlap < 0 || sp.overlap >= sp.max_units)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document, require max>=1 and 0<=overlap<max");
    }
  }
  return ret;
}

// Top-level split. Fills out (chunk text deep-copied into alloc).
int split_document(ObIAllocator &alloc, const ObString &content, const SplitParams &sp,
                   ObIArray<ObExprAISplitDocumentCtx::ChunkInfo> &out)
{
  int ret = OB_SUCCESS;
  int64_t chunk_id = 0;
  if (sp.is_markdown) {
    ObArray<Section> sections;
    OZ (split_markdown_sections(content, sections));
    for (int64_t s = 0; OB_SUCC(ret) && s < sections.count(); ++s) {
      const Section &sec = sections.at(s);
      ObArray<Unit> units;
      if (sp.by_sentence) {
        OZ (split_sentences(sec.body, units));
      } else {
        OZ (split_words(sec.body, units));
      }
      if (OB_SUCC(ret) && units.count() > 0) {
        // emit body chunks, then prepend heading to each chunk_text
        ObArray<ObExprAISplitDocumentCtx::ChunkInfo> body_chunks;
        int64_t base_id = chunk_id;
        if (sp.by_sentence) {
          OZ (emit_sentence_chunks(alloc, units, sp.max_units, sp.overlap,
                                   sec.body_off, sec.body, base_id, body_chunks));
        } else {
          OZ (emit_word_chunks(alloc, units, sp.max_units, sp.overlap,
                               sec.body_off, sec.body, base_id, body_chunks));
        }
        for (int64_t c = 0; OB_SUCC(ret) && c < body_chunks.count(); ++c) {
          ObExprAISplitDocumentCtx::ChunkInfo bc = body_chunks.at(c);
          if (sec.heading.empty()) {
            OZ (out.push_back(bc));
          } else {
            // chunk_text = heading + body_text; length = heading.len + body.len
            int64_t newlen = sec.heading.length() + bc.chunk_text_.length();
            char *buf = static_cast<char *>(alloc.alloc(newlen));
            if (OB_ISNULL(buf)) { ret = OB_ALLOCATE_MEMORY_FAILED; LOG_WARN("alloc heading chunk failed", K(ret)); }
            else {
              MEMCPY(buf, sec.heading.ptr(), sec.heading.length());
              MEMCPY(buf + sec.heading.length(), bc.chunk_text_.ptr(), bc.chunk_text_.length());
              bc.chunk_text_.assign_ptr(buf, newlen);
              bc.chunk_length_ = newlen;  // reconstructed length (heading + body)
              // chunk_offset stays = body subspan offset (heading not counted)
              OZ (out.push_back(bc));
            }
          }
          chunk_id = bc.chunk_id_ + 1;
        }
      }
    }
  } else {
    // plain text
    ObArray<Unit> units;
    if (sp.by_sentence) {
      OZ (split_sentences(content, units));
    } else {
      OZ (split_words(content, units));
    }
    if (OB_SUCC(ret) && units.count() > 0) {
      if (sp.by_sentence) {
        OZ (emit_sentence_chunks(alloc, units, sp.max_units, sp.overlap, 0, content, chunk_id, out));
      } else {
        OZ (emit_word_chunks(alloc, units, sp.max_units, sp.overlap, 0, content, chunk_id, out));
      }
    }
  }
  return ret;
}

} // anonymous namespace

// ===========================================================================
// ObExprAISplitDocument
// ===========================================================================

ObExprAISplitDocument::ObExprAISplitDocument(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_AI_SPLIT_DOCUMENT, N_AI_SPLIT_DOCUMENT,
                         ONE_OR_TWO, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

int ObExprAISplitDocument::calc_result_typeN(ObExprResType &type,
                                             ObExprResType *types,
                                             int64_t param_num,
                                             common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num < 1 || param_num > 2)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ai_split_document expects 1 or 2 args", K(ret), K(param_num));
  } else {
    // content (arg0) and params json (arg1) both as varchar. set_calc_type on
    // arg0 makes the framework insert a cast that strips LOB headers for
    // TEXT/BLOB column inputs, so eval sees a clean varchar datum.
    types[0].set_calc_type(ObVarcharType);
    if (param_num > 1) {
      types[1].set_calc_type(ObVarcharType);
    }
    // res type is int (carrier for chunk_id); the op reads 4 values from rt_ctx.
    type.set_int();
  }
  return ret;
}

int ObExprAISplitDocument::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                   const ObRawExpr &raw_expr,
                                   ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = ObExprAISplitDocument::eval_split_document;
  return OB_SUCCESS;
}

int ObExprAISplitDocument::eval_split_document(const ObExpr &expr, ObEvalCtx &ctx,
                                               ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObExecContext &exec_ctx = ctx.exec_ctx_;
  ObExprAISplitDocumentCtx *split_ctx = static_cast<ObExprAISplitDocumentCtx *>(
      exec_ctx.get_expr_op_ctx(expr.expr_ctx_id_));
  if (OB_ISNULL(split_ctx)) {
    // first call: create rt_ctx (framework default-constructs), lazy-init
    if (OB_FAIL(exec_ctx.create_expr_op_ctx(expr.expr_ctx_id_, split_ctx))) {
      LOG_WARN("ai_split_document: create rt_ctx failed", K(ret));
    } else if (OB_ISNULL(split_ctx)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("ai_split_document: rt_ctx is null after create", K(ret));
    } else {
      // chunks_ uses ObArray's default allocator (holds ChunkInfo structs only;
      // chunk_text_ bytes are owned by split_ctx->allocator_). Local ObArray
      // usage elsewhere (e.g. ob_expr_find_in_set) confirms default-construct works.
      ObDatum *content_datum = NULL;
      ObDatum *params_datum = NULL;
      if (OB_FAIL(expr.eval_param_value(ctx, content_datum, params_datum))) {
        LOG_WARN("ai_split_document: eval params failed", K(ret));
      } else if (content_datum == NULL || content_datum->is_null()) {
        // NULL content => 0 rows (fall through to OB_ITER_END)
      } else {
        ObString content = content_datum->get_string();  // calc_type=varchar strips LOB header
        SplitParams sp;
        bool has_params = (params_datum != NULL && !params_datum->is_null());
        ObString params_str = has_params ? params_datum->get_string() : ObString();
        if (OB_FAIL(parse_params(params_str, sp))) {
          LOG_WARN("ai_split_document: parse_params failed", K(ret));
        } else if (OB_FAIL(split_document(split_ctx->allocator_, content, sp,
                                          split_ctx->chunks_))) {
          LOG_WARN("ai_split_document: split_document failed", K(ret));
        }
      }
      split_ctx->initialized_ = true;
    }
  }
  if (OB_SUCC(ret)) {
    if (split_ctx->curr_idx_ >= split_ctx->chunks_.count()) {
      ret = OB_ITER_END;
    } else {
      const ObExprAISplitDocumentCtx::ChunkInfo &c = split_ctx->chunks_.at(split_ctx->curr_idx_);
      // set current-row fields BEFORE returning success (exec op reads them)
      split_ctx->curr_chunk_id_ = c.chunk_id_;
      split_ctx->curr_chunk_offset_ = c.chunk_offset_;
      split_ctx->curr_chunk_length_ = c.chunk_length_;
      split_ctx->curr_chunk_text_ = c.chunk_text_;
      res.set_int(c.chunk_id_);
      ++split_ctx->curr_idx_;
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
