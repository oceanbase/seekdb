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
#include "data_plane/fts/ob_fts_struct.h"

using namespace oceanbase::common;

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
  } else if (OB_ISNULL(token_allocator_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(token_stream_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(token_stream_->get_next(token, token_freq))) {
    if (OB_ITER_END != ret) {
    }
  } else if (OB_ISNULL(token.ptr_) || OB_UNLIKELY(0 >= token.len_ || 0 >= token_freq)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(buf = static_cast<char *>(token_allocator_->alloc(token.len_)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
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

int ObBEngFTParser::init(ObFTParserParam *param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(param) || OB_UNLIKELY(!param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(UINT32_MAX < param->ft_length_)) {
    ret = OB_NOT_SUPPORTED;
  } else {
    doc_.set_string(param->fulltext_, param->ft_length_);
    analysis_ctx_.cs_ = param->cs_;
    analysis_ctx_.filter_stopword_ = false;
    analysis_ctx_.need_grouping_ = false;
    // The token copies live in the caller's allocator: they are referenced by
    // the posting lists of the document and must outlive the parser arena,
    // which is recycled between documents.
    token_allocator_ = param->allocator_;
    // Allocate analyzer resources from the parser's own allocator: for the
    // thread-local reused parser it outlives this call, for the per-call
    // parser it is param->allocator_ anyway.
    if (OB_FAIL(english_analyzer_.init(analysis_ctx_, allocator_))) {
    } else if (OB_FAIL(segment(doc_, token_stream_))) {
    } else if (OB_ISNULL(token_stream_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      is_inited_ = true;
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
  } else if (OB_UNLIKELY(UINT32_MAX < doc.len_)) {
    ret = OB_NOT_SUPPORTED;
  } else if (OB_FAIL(english_analyzer_.analyze(doc, token_stream))) {
  }
  return ret;
}

void ObBEngFTParser::reset()
{
  analysis_ctx_.reset();
  english_analyzer_.reset();
  doc_.reset();
  token_stream_ = nullptr;
  token_allocator_ = nullptr;
  is_inited_ = false;
}

namespace
{
// The BENG parser and its analyzer pipeline are rebuilt per document today;
// keep a small per-thread parser pool so sequential AND nested segment()
// calls both reuse parsers. A borrower marks its pool entry lent and the
// reuse iterator returns it on destruction. Every pool entry owns its own
// arena: the arena of an entry is recycled only after the analyzer of that
// entry has been destroyed, so a recycled arena can never leave another
// pooled parser with dangling analyzer state.
constexpr int64_t BENG_POOL_DEPTH = 2;
struct BengPoolEntry
{
  ObArenaAllocator *allocator_;
  ObBEngFTParser *parser_;
  bool lent_;
};
thread_local BengPoolEntry tls_beng_pool[BENG_POOL_DEPTH] = {};

ObArenaAllocator *beng_pool_arena(ObBEngFTParser *parser)
{
  ObArenaAllocator *arena = nullptr;
  for (int64_t i = 0; i < BENG_POOL_DEPTH; ++i) {
    if (parser == tls_beng_pool[i].parser_) {
      arena = tls_beng_pool[i].allocator_;
      break;
    }
  }
  return arena;
}

ObBEngFTParser *acquire_beng_parser()
{
  ObBEngFTParser *parser = nullptr;
  for (int64_t i = 0; OB_ISNULL(parser) && i < BENG_POOL_DEPTH; ++i) {
    if (!tls_beng_pool[i].lent_) {
      parser = tls_beng_pool[i].parser_;
      if (OB_ISNULL(parser)) {
        if (OB_ISNULL(tls_beng_pool[i].allocator_)) {
          tls_beng_pool[i].allocator_ = new (std::nothrow) ObArenaAllocator(ObMemAttr("BEngParseRuse"), 64 * 1024);
        }
        if (OB_NOT_NULL(tls_beng_pool[i].allocator_)) {
          parser = new (std::nothrow) ObBEngFTParser(*tls_beng_pool[i].allocator_);
          tls_beng_pool[i].parser_ = parser;
        }
      }
      if (OB_NOT_NULL(parser)) {
        tls_beng_pool[i].lent_ = true;
      }
    }
  }
  return parser;
}

void release_beng_parser(ObBEngFTParser *parser)
{
  if (OB_NOT_NULL(parser)) {
    for (int64_t i = 0; i < BENG_POOL_DEPTH; ++i) {
      if (parser == tls_beng_pool[i].parser_ && tls_beng_pool[i].lent_) {
        tls_beng_pool[i].lent_ = false;
        break;
      }
    }
  }
}

class ObBEngReuseTokenIter final : public ObITokenIterator
{
public:
  explicit ObBEngReuseTokenIter(ObBEngFTParser &parser) : parser_(parser) {}
  virtual ~ObBEngReuseTokenIter() override { release_beng_parser(&parser_); }
  virtual int get_next_token(
      const char *&word,
      int64_t &word_len,
      int64_t &char_len,
      int64_t &word_freq) override
  {
    return parser_.get_next_token(word, word_len, char_len, word_freq);
  }
  VIRTUAL_TO_STRING_KV("iter", "ObBEngReuseTokenIter");

private:
  ObBEngFTParser &parser_;
};

} // end anonymous namespace

ObBasicEnglishFTParserDesc::ObBasicEnglishFTParserDesc() {}

int ObBasicEnglishFTParserDesc::segment(
    ObFTParserParam *param,
    ObITokenIterator *&iter) const
{
  int ret = OB_SUCCESS;
  ObBEngFTParser *parser = nullptr;
  ObBEngReuseTokenIter *fast_iter = nullptr;
  bool input_ok = true;
  if (OB_ISNULL(param) || OB_ISNULL(param->fulltext_) || OB_UNLIKELY(!param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    input_ok = false;
  } else if (OB_NOT_NULL(parser = acquire_beng_parser())) {
    // Destroy the analyzer of the previous document before recycling the
    // arena of this pool entry, otherwise the analyzer would keep pointers
    // into freed memory.
    parser->reset();
    ObArenaAllocator *arena = beng_pool_arena(parser);
    if (OB_NOT_NULL(arena)) {
      // reuse() keeps the pages and rewinds the allocation pointer, so the
      // next document reuses the memory of the previous one.
      arena->reuse();
    }
    if (OB_FAIL(parser->init(param))) {
      // broken parser: drop it and fall through to the per-call parser
      parser->~ObBEngFTParser();
      delete parser;
      for (int64_t i = 0; i < BENG_POOL_DEPTH; ++i) {
        if (parser == tls_beng_pool[i].parser_) {
          tls_beng_pool[i].parser_ = nullptr;
          tls_beng_pool[i].lent_ = false;
        }
      }
    } else if (OB_ISNULL(fast_iter = OB_NEWx(ObBEngReuseTokenIter, param->allocator_, *parser))) {
      // healthy parser, keep it pooled; just serve this call from a per-call parser
      release_beng_parser(parser);
    } else {
      iter = fast_iter;
      return ret;
    }
    parser = nullptr;
    ret = OB_SUCCESS;
  }

  if (input_ok) {
    parser = nullptr;
    if (OB_ISNULL(parser = OB_NEWx(ObBEngFTParser, param->allocator_, *(param->allocator_)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(parser->init(param))) {
    } else {
      iter = parser;
      return ret;
    }
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
