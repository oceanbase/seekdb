/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_ai_split_document.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
namespace
{

struct TextSpan
{
  int64_t offset_;
  int64_t length_;
  std::string text_;
};

static std::string to_std_string(const ObString &str)
{
  return std::string(str.ptr(), str.length());
}

static std::string lower_copy(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

static bool json_contains_string(const std::string &json,
                                 const char *key,
                                 const char *value)
{
  const std::string lower = lower_copy(json);
  const std::string needle1 = std::string("\"") + key + "\":\"" + value + "\"";
  const std::string needle2 = std::string("\"") + key + "\" : \"" + value + "\"";
  return lower.find(needle1) != std::string::npos || lower.find(needle2) != std::string::npos;
}

static int64_t json_int_value(const std::string &json, const char *key, int64_t default_value)
{
  const std::string lower = lower_copy(json);
  const std::string quoted_key = std::string("\"") + key + "\"";
  size_t pos = lower.find(quoted_key);
  if (pos == std::string::npos) {
    return default_value;
  }
  pos = lower.find(':', pos + quoted_key.length());
  if (pos == std::string::npos) {
    return default_value;
  }
  ++pos;
  while (pos < lower.length() && std::isspace(static_cast<unsigned char>(lower[pos]))) {
    ++pos;
  }
  int64_t value = 0;
  bool has_digit = false;
  while (pos < lower.length() && std::isdigit(static_cast<unsigned char>(lower[pos]))) {
    has_digit = true;
    value = value * 10 + lower[pos] - '0';
    ++pos;
  }
  return has_digit ? value : default_value;
}

static int copy_chunk_text(ObIAllocator &allocator,
                           const std::string &text,
                           const int64_t offset,
                           ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  if (text.empty()) {
    if (OB_FAIL(chunks.push_back(ObAISplitDocumentChunk(offset, 0, ObString::make_empty_string())))) {
      LOG_WARN("failed to push empty chunk", K(ret));
    }
  } else if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(text.length())))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate chunk text", K(ret), K(text.length()));
  } else {
    MEMCPY(buf, text.data(), text.length());
    if (OB_FAIL(chunks.push_back(ObAISplitDocumentChunk(
            offset,
            text.length(),
            ObString(static_cast<ObString::obstr_size_t>(text.length()), buf))))) {
      LOG_WARN("failed to push chunk", K(ret));
    }
  }
  return ret;
}

static std::vector<TextSpan> split_sentences(const std::string &content)
{
  std::vector<TextSpan> spans;
  size_t pos = 0;
  while (pos < content.length()) {
    while (pos < content.length() && std::isspace(static_cast<unsigned char>(content[pos]))) {
      ++pos;
    }
    if (pos >= content.length()) {
      break;
    }
    const size_t start = pos;
    while (pos < content.length()
           && content[pos] != '.'
           && content[pos] != '!'
           && content[pos] != '?') {
      ++pos;
    }
    if (pos < content.length()) {
      ++pos;
    }
    const size_t end = pos;
    if (end > start) {
      spans.push_back(TextSpan{static_cast<int64_t>(start),
                               static_cast<int64_t>(end - start),
                               content.substr(start, end - start)});
    }
  }
  return spans;
}

static std::vector<TextSpan> split_words(const std::string &content)
{
  std::vector<TextSpan> spans;
  size_t pos = 0;
  while (pos < content.length()) {
    while (pos < content.length() && std::isspace(static_cast<unsigned char>(content[pos]))) {
      ++pos;
    }
    if (pos >= content.length()) {
      break;
    }
    const size_t start = pos;
    while (pos < content.length() && !std::isspace(static_cast<unsigned char>(content[pos]))) {
      ++pos;
    }
    spans.push_back(TextSpan{static_cast<int64_t>(start),
                             static_cast<int64_t>(pos - start),
                             content.substr(start, pos - start)});
  }
  return spans;
}

static int build_window_chunks(ObIAllocator &allocator,
                               const std::vector<TextSpan> &units,
                               const int64_t max_units,
                               const int64_t overlap,
                               const bool by_word,
                               const std::string &prefix,
                               ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  const int64_t safe_max = std::max<int64_t>(1, max_units);
  const int64_t safe_overlap = std::max<int64_t>(0, std::min<int64_t>(overlap, safe_max - 1));
  const int64_t step = std::max<int64_t>(1, safe_max - safe_overlap);
  for (int64_t start = 0; OB_SUCC(ret) && start < static_cast<int64_t>(units.size()); start += step) {
    const int64_t end = std::min<int64_t>(start + safe_max, units.size());
    std::string text = prefix;
    for (int64_t i = start; i < end; ++i) {
      if (i > start) {
        text.append(by_word ? " " : " ");
      }
      text.append(units[i].text_);
    }
    const int64_t offset = units[start].offset_;
    if (OB_FAIL(copy_chunk_text(allocator, text, offset, chunks))) {
      LOG_WARN("failed to copy chunk text", K(ret));
    }
    if (end >= static_cast<int64_t>(units.size())) {
      break;
    }
  }
  return ret;
}

static int build_markdown_sentence_chunks(ObIAllocator &allocator,
                                          const std::string &content,
                                          const int64_t max_units,
                                          const int64_t overlap,
                                          ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  size_t line_start = 0;
  std::string heading;
  std::string section_text;
  int64_t section_offset = 0;
  auto flush_section = [&]() -> int {
    int tmp_ret = OB_SUCCESS;
    if (!section_text.empty()) {
      std::vector<TextSpan> sentences = split_sentences(section_text);
      for (size_t i = 0; i < sentences.size(); ++i) {
        sentences[i].offset_ += section_offset;
      }
      const std::string prefix = heading.empty() ? std::string() : heading + "\n";
      tmp_ret = build_window_chunks(allocator, sentences, max_units, overlap, false, prefix, chunks);
    }
    section_text.clear();
    return tmp_ret;
  };

  while (OB_SUCC(ret) && line_start <= content.length()) {
    size_t line_end = content.find('\n', line_start);
    if (line_end == std::string::npos) {
      line_end = content.length();
    }
    const std::string line = content.substr(line_start, line_end - line_start);
    if (!line.empty() && line[0] == '#') {
      if (OB_FAIL(flush_section())) {
        LOG_WARN("failed to flush markdown section", K(ret));
      } else {
        heading = line;
        section_offset = static_cast<int64_t>(line_end < content.length() ? line_end + 1 : line_end);
      }
    } else {
      if (section_text.empty()) {
        section_offset = static_cast<int64_t>(line_start);
      } else {
        section_text.append("\n");
      }
      section_text.append(line);
    }
    if (line_end >= content.length()) {
      break;
    }
    line_start = line_end + 1;
  }
  if (OB_SUCC(ret) && OB_FAIL(flush_section())) {
    LOG_WARN("failed to flush last markdown section", K(ret));
  }
  return ret;
}

} // namespace

ObExprAISplitDocument::ObExprAISplitDocument(ObIAllocator &alloc)
  : ObStringExprOperator(alloc, T_FUN_SYS_AI_SPLIT_DOCUMENT, N_AI_SPLIT_DOCUMENT,
                         MORE_THAN_ZERO, NOT_VALID_FOR_GENERATED_COL)
{
}

ObExprAISplitDocument::~ObExprAISplitDocument()
{
}

int ObExprAISplitDocument::calc_result_typeN(ObExprResType &type,
                                             ObExprResType *types,
                                             int64_t param_num,
                                             ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  if (OB_UNLIKELY(param_num < 1 || param_num > 2)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ai_split_document argument count", K(ret), K(param_num));
  } else {
    type.set_varchar();
    type.set_collation_type(ObCharset::get_system_collation());
    type.set_length(OB_MAX_VARCHAR_LENGTH);
    for (int64_t i = 0; i < param_num; ++i) {
      types[i].set_calc_type(ObVarcharType);
      types[i].set_calc_collation_type(ObCharset::get_system_collation());
    }
  }
  return ret;
}

int ObExprAISplitDocument::eval_ai_split_document(const ObExpr &expr,
                                                  ObEvalCtx &ctx,
                                                  ObDatum &expr_datum)
{
  UNUSED(expr);
  UNUSED(ctx);
  expr_datum.set_null();
  return OB_SUCCESS;
}

int ObExprAISplitDocument::build_chunks(ObIAllocator &allocator,
                                        const ObString &content,
                                        const ObString &params,
                                        ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  const std::string content_str = to_std_string(content);
  const std::string params_str = to_std_string(params);
  const bool is_text = json_contains_string(params_str, "type", "text");
  const bool by_sentence = json_contains_string(params_str, "by", "sentence");
  const int64_t max_units = json_int_value(params_str, "max", 256);
  const int64_t overlap = json_int_value(params_str, "overlap", 0);

  if (!is_text && by_sentence) {
    if (OB_FAIL(build_markdown_sentence_chunks(allocator, content_str, max_units, overlap, chunks))) {
      LOG_WARN("failed to split markdown document", K(ret));
    }
  } else if (by_sentence) {
    std::vector<TextSpan> sentences = split_sentences(content_str);
    if (OB_FAIL(build_window_chunks(allocator, sentences, max_units, overlap, false,
                                    std::string(), chunks))) {
      LOG_WARN("failed to split text sentences", K(ret));
    }
  } else {
    std::vector<TextSpan> words = split_words(content_str);
    if (OB_FAIL(build_window_chunks(allocator, words, max_units, overlap, true,
                                    std::string(), chunks))) {
      LOG_WARN("failed to split text words", K(ret));
    }
  }
  return ret;
}

int ObExprAISplitDocument::cg_expr(ObExprCGCtx &op_cg_ctx,
                                   const ObRawExpr &raw_expr,
                                   ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = ObExprAISplitDocument::eval_ai_split_document;
  return OB_SUCCESS;
}

}
}
