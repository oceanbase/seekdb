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

#include "storage/fts/ob_beng_ft_parser.h"
#include "storage/fts/ob_fts_struct.h"

using namespace oceanbase::common;
using namespace oceanbase::plugin;

namespace oceanbase
{
namespace storage
{

int ObBEngFTParser::get_next_token(
    const char *&word,
    int64_t &word_len,
    int64_t &char_len,
    int64_t &word_freq)
{
  int ret = OB_SUCCESS;
  ObDatum token;
  int64_t token_freq = 0;
  char *buf = nullptr;
  word = nullptr;
  word_len = 0;
  char_len = 0;
  word_freq = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("beng ft parser isn't initialized", K(ret), K(is_inited_));
  } else if (use_ascii_scanner_) {
    ret = next_ascii_token(word, word_len, char_len, word_freq);
  } else if (OB_ISNULL(token_stream_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("token stream is nullptr", K(ret), KP(token_stream_));
  } else if (OB_FAIL(token_stream_->get_next(token, token_freq))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("fail to get next token", K(ret), KPC(token_stream_));
    }
  } else if (OB_ISNULL(token.ptr_) || OB_UNLIKELY(0 >= token.len_ || 0 >= token_freq)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(token.ptr_), K(token.len_), K(token_freq));
  } else if (OB_ISNULL(buf = static_cast<char *>(scratch_allocator_.alloc(token.len_)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate word memory", K(ret), K(token.len_));
  } else {
    MEMCPY(buf, token.ptr_, token.len_);
    word = buf;
    word_len = token.len_;
    char_len = token.len_;
    word_freq = token_freq;
    LOG_DEBUG("succeed to add word", K(ObString(word_len, word)), K(word_freq));
  }
  return ret;
}

bool ObBEngFTParser::is_ascii_document(const char *text, const int64_t text_len)
{
  bool all_ascii = OB_NOT_NULL(text) && text_len > 0;
  for (int64_t idx = 0; all_ascii && idx < text_len; ++idx) {
    all_ascii = (static_cast<unsigned char>(text[idx]) & 0x80) == 0;
  }
  return all_ascii;
}

bool ObBEngFTParser::is_ascii_word_char(const unsigned char ch)
{
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

int ObBEngFTParser::next_ascii_token(const char *&word,
                                      int64_t &word_len,
                                      int64_t &char_len,
                                      int64_t &word_freq)
{
  int ret = OB_SUCCESS;
  while (ascii_cursor_ < ascii_end_
         && !is_ascii_word_char(static_cast<unsigned char>(*ascii_cursor_))) {
    ++ascii_cursor_;
  }
  if (ascii_cursor_ >= ascii_end_) {
    ret = OB_ITER_END;
  } else {
    const char *begin = ascii_cursor_;
    bool contains_uppercase = false;
    while (ascii_cursor_ < ascii_end_
           && is_ascii_word_char(static_cast<unsigned char>(*ascii_cursor_))) {
      const unsigned char ch = static_cast<unsigned char>(*ascii_cursor_);
      contains_uppercase = contains_uppercase || (ch >= 'A' && ch <= 'Z');
      ++ascii_cursor_;
    }
    const int64_t length = ascii_cursor_ - begin;
    word = begin;
    if (contains_uppercase) {
      char *normalized = static_cast<char *>(scratch_allocator_.alloc(length));
      if (OB_ISNULL(normalized)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate normalized basic-English token", K(ret), K(length));
      } else {
        for (int64_t idx = 0; idx < length; ++idx) {
          const unsigned char ch = static_cast<unsigned char>(begin[idx]);
          normalized[idx] = (ch >= 'A' && ch <= 'Z') ? ch + ('a' - 'A') : ch;
        }
        word = normalized;
      }
    }
    if (OB_SUCC(ret)) {
      word_len = length;
      char_len = length;
      word_freq = 1;
    }
  }
  return ret;
}

int ObBEngFTParser::ensure_analyzer()
{
  int ret = OB_SUCCESS;
  if (!analyzer_ready_) {
    if (OB_FAIL(english_analyzer_.init(analysis_ctx_, allocator_))) {
      LOG_WARN("failed to initialize basic-English analyzer", K(ret), K_(analysis_ctx));
    } else {
      analyzer_ready_ = true;
    }
  }
  return ret;
}

int ObBEngFTParser::prepare_document(const char *text, const int64_t text_len)
{
  int ret = OB_SUCCESS;
  token_stream_ = nullptr;
  ascii_cursor_ = nullptr;
  ascii_end_ = nullptr;
  use_ascii_scanner_ = is_ascii_document(text, text_len);
  doc_.set_string(text, text_len);
  if (use_ascii_scanner_) {
    ascii_cursor_ = text;
    ascii_end_ = text + text_len;
  } else if (OB_FAIL(ensure_analyzer())) {
    LOG_WARN("failed to prepare basic-English analyzer", K(ret));
  } else if (OB_FAIL(segment(doc_, token_stream_))) {
    LOG_WARN("failed to segment basic-English document", K(ret), K(text_len));
  } else if (OB_ISNULL(token_stream_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("basic-English analyzer returned a null token stream", K(ret));
  }
  return ret;
}

int ObBEngFTParser::init(ObFTParserParam *param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_ISNULL(param) || OB_UNLIKELY(!param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("param is nullptr", K(ret), KPC(param));
  } else if (OB_UNLIKELY(UINT32_MAX < param->ft_length_)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("too large document, english analyzer hasn't be supported", K(ret), K(param->ft_length_));
  } else {
    analysis_ctx_.cs_ = param->cs_;
    analysis_ctx_.filter_stopword_ = false;
    analysis_ctx_.need_grouping_ = false;
    if (OB_FAIL(prepare_document(param->fulltext_, param->ft_length_))) {
      LOG_WARN("fail to prepare basic-English document", K(ret), KPC(param));
    } else {
      is_inited_ = true;
      LOG_DEBUG("succeed to init beng parser", K(ret), K(english_analyzer_), KPC(token_stream_), K(doc_));
    }
  }
  if (OB_FAIL(ret) && OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

int ObBEngFTParser::reset_document(const ObFTParserParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_ || !param.is_valid() || param.cs_ != analysis_ctx_.cs_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid basic-English parser session reset", K(ret), K_(is_inited), K(param));
  } else if (OB_UNLIKELY(UINT32_MAX < param.ft_length_)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("basic-English document is too large", K(ret), K(param.ft_length_));
  } else {
    scratch_allocator_.reuse();
    if (OB_FAIL(prepare_document(param.fulltext_, param.ft_length_))) {
      LOG_WARN("failed to reset basic-English parser document", K(ret), K(param.ft_length_));
    }
  }
  return ret;
}

int ObBEngFTParser::segment(
    const common::ObDatum &doc,
    share::ObITokenStream *&token_stream)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(doc.ptr_) || OB_UNLIKELY(0 >= doc.len_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(doc.ptr_), K(doc.len_));
  } else if (OB_UNLIKELY(UINT32_MAX < doc.len_)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("too large document, english analyzer hasn't be supported", K(ret), K(doc.len_));
  } else if (OB_FAIL(english_analyzer_.analyze(doc, token_stream))) {
    LOG_WARN("fail to analyze document", K(ret), K(english_analyzer_), KP(doc.ptr_), K(doc.len_));
  }
  return ret;
}

void ObBEngFTParser::reset()
{
  analysis_ctx_.reset();
  english_analyzer_.reset();
  scratch_allocator_.reset();
  doc_.reset();
  token_stream_ = nullptr;
  ascii_cursor_ = nullptr;
  ascii_end_ = nullptr;
  use_ascii_scanner_ = false;
  analyzer_ready_ = false;
  is_inited_ = false;
}

ObBasicEnglishFTParserDesc::ObBasicEnglishFTParserDesc()
  : is_inited_(false)
{
}

int ObBasicEnglishFTParserDesc::init(ObPluginParam *param)
{
  is_inited_ = true;
  return OB_SUCCESS;
}

int ObBasicEnglishFTParserDesc::deinit(ObPluginParam *param)
{
  reset();
  return OB_SUCCESS;
}

int ObBasicEnglishFTParserDesc::segment(
    ObFTParserParam *param,
    ObITokenIterator *&iter) const
{
  int ret = OB_SUCCESS;
  ObBEngFTParser *parser = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("default ft parser desc hasn't be initialized", K(ret), K(is_inited_));
  } else if (OB_ISNULL(param) || OB_ISNULL(param->fulltext_) || OB_UNLIKELY(!param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(param));
  } else if (OB_ISNULL(parser = OB_NEWx(ObBEngFTParser, param->allocator_, *(param->allocator_)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate basic english ft parser", K(ret));
  } else if (OB_FAIL(parser->init(param))) {
    LOG_WARN("fail to init basic english parser", K(ret), KPC(param));
  } else {
    iter = parser;
  }

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObBEngFTParser, param->allocator_, parser);
  }

  return ret;
}

void ObBasicEnglishFTParserDesc::free_token_iter(
    ObFTParserParam *param,
    ObITokenIterator *&iter) const
{
  if (OB_NOT_NULL(iter)) {
    abort_unless(nullptr != param);
    abort_unless(nullptr != param->allocator_);
    iter->~ObITokenIterator();
    param->allocator_->free(iter);
  }
}

int ObBasicEnglishFTParserDesc::get_add_word_flag(ObAddWordFlag &flag) const
{
  int ret = OB_SUCCESS;
  flag.set_min_max_word();
  flag.set_stop_word();
  flag.set_groupby_word();
  return ret;
}

} // end namespace storage
} // end namespace oceanbase
