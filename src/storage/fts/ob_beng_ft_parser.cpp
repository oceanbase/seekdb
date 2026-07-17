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
  } else if (use_ascii_fast_path_) {
    ret = get_next_ascii_token(word, word_len, char_len, word_freq);
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

bool ObBEngFTParser::is_ascii_alnum(const unsigned char ch)
{
  return ('0' <= ch && '9' >= ch)
      || ('A' <= ch && 'Z' >= ch)
      || ('a' <= ch && 'z' >= ch);
}

bool ObBEngFTParser::is_ascii_document(const char *text, const int64_t text_len)
{
  bool is_ascii = OB_NOT_NULL(text) && 0 < text_len;
  for (int64_t i = 0; is_ascii && i < text_len; ++i) {
    is_ascii = 0 == (static_cast<unsigned char>(text[i]) & 0x80);
  }
  return is_ascii;
}

int ObBEngFTParser::get_next_ascii_token(
    const char *&word,
    int64_t &word_len,
    int64_t &char_len,
    int64_t &word_freq)
{
  int ret = OB_SUCCESS;
  const char *text = doc_.ptr_;
  const int64_t text_len = doc_.len_;
  while (ascii_pos_ < text_len
      && !is_ascii_alnum(static_cast<unsigned char>(text[ascii_pos_]))) {
    ++ascii_pos_;
  }
  if (ascii_pos_ >= text_len) {
    ret = OB_ITER_END;
  } else {
    const int64_t token_start = ascii_pos_;
    bool need_lower = false;
    while (ascii_pos_ < text_len
        && is_ascii_alnum(static_cast<unsigned char>(text[ascii_pos_]))) {
      const unsigned char ch = static_cast<unsigned char>(text[ascii_pos_]);
      need_lower = need_lower || ('A' <= ch && 'Z' >= ch);
      ++ascii_pos_;
    }
    const int64_t token_len = ascii_pos_ - token_start;
    if (need_lower) {
      char *buf = static_cast<char *>(scratch_allocator_.alloc(token_len));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to allocate ascii token memory", K(ret), K(token_len));
      } else {
        for (int64_t i = 0; i < token_len; ++i) {
          const unsigned char ch = static_cast<unsigned char>(text[token_start + i]);
          buf[i] = ('A' <= ch && 'Z' >= ch)
              ? static_cast<char>(ch + ('a' - 'A')) : static_cast<char>(ch);
        }
        word = buf;
      }
    } else {
      word = text + token_start;
    }
    if (OB_SUCC(ret)) {
      word_len = token_len;
      char_len = token_len;
      word_freq = 1;
    }
  }
  return ret;
}

int ObBEngFTParser::reuse_parser(const char *fulltext, const int64_t fulltext_len)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("basic english ft parser has not been initialized", K(ret));
  } else if (OB_ISNULL(fulltext) || OB_UNLIKELY(fulltext_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fulltext for parser reuse", K(ret), KP(fulltext), K(fulltext_len));
  } else if (OB_UNLIKELY(UINT32_MAX < fulltext_len)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("document is too large for english analyzer", K(ret), K(fulltext_len));
  } else {
    doc_.set_string(fulltext, fulltext_len);
    ascii_pos_ = 0;
    use_ascii_fast_path_ = is_ascii_document(fulltext, fulltext_len);
    if (use_ascii_fast_path_) {
      token_stream_ = nullptr;
    } else if (OB_FAIL(segment(doc_, token_stream_))) {
      LOG_WARN("fail to segment fulltext", K(ret));
    } else if (OB_ISNULL(token_stream_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("token stream is null", K(ret));
    }
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
    doc_.set_string(param->fulltext_, param->ft_length_);
    analysis_ctx_.cs_ = param->cs_;
    analysis_ctx_.filter_stopword_ = false;
    analysis_ctx_.need_grouping_ = false;
    ascii_pos_ = 0;
    use_ascii_fast_path_ = is_ascii_document(param->fulltext_, param->ft_length_);
    if (OB_FAIL(english_analyzer_.init(analysis_ctx_, metadata_allocator_))) {
      LOG_WARN("fail to init english analyzer", K(ret), KPC(param), K(analysis_ctx_));
    } else if (!use_ascii_fast_path_ && OB_FAIL(segment(doc_, token_stream_))) {
      LOG_WARN("fail to segment fulltext by parser", K(ret), KP(param->fulltext_), K(param->ft_length_));
    } else if (!use_ascii_fast_path_ && OB_ISNULL(token_stream_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("token stream is nullptr", K(ret), KP(token_stream_));
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
  doc_.reset();
  token_stream_ = nullptr;
  ascii_pos_ = 0;
  use_ascii_fast_path_ = false;
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
  ObIAllocator *metadata_allocator = nullptr;
  ObIAllocator *scratch_allocator = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("default ft parser desc hasn't be initialized", K(ret), K(is_inited_));
  } else if (OB_ISNULL(param) || OB_ISNULL(param->fulltext_) || OB_UNLIKELY(!param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(param));
  } else if (FALSE_IT(metadata_allocator = OB_NOT_NULL(param->metadata_alloc_)
          ? param->metadata_alloc_ : param->allocator_)) {
  } else if (FALSE_IT(scratch_allocator = OB_NOT_NULL(param->scratch_alloc_)
          ? param->scratch_alloc_ : param->allocator_)) {
  } else if (OB_ISNULL(metadata_allocator) || OB_ISNULL(scratch_allocator)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fulltext parser allocator is null", K(ret), KPC(param));
  } else if (OB_ISNULL(parser = OB_NEWx(ObBEngFTParser,
                                         metadata_allocator,
                                         *metadata_allocator,
                                         *scratch_allocator))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate basic english ft parser", K(ret));
  } else if (OB_FAIL(parser->init(param))) {
    LOG_WARN("fail to init basic english parser", K(ret), KPC(param));
  } else {
    iter = parser;
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(metadata_allocator)) {
    OB_DELETEx(ObBEngFTParser, metadata_allocator, parser);
  }

  return ret;
}

void ObBasicEnglishFTParserDesc::free_token_iter(
    ObFTParserParam *param,
    ObITokenIterator *&iter) const
{
  if (OB_NOT_NULL(iter)) {
    abort_unless(nullptr != param);
    ObIAllocator *metadata_allocator = OB_NOT_NULL(param->metadata_alloc_)
        ? param->metadata_alloc_ : param->allocator_;
    abort_unless(nullptr != metadata_allocator);
    iter->~ObITokenIterator();
    metadata_allocator->free(iter);
    iter = nullptr;
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
