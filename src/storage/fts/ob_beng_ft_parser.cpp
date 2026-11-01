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

#include "lib/allocator/page_arena.h"
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
  word = nullptr;
  word_len = 0;
  char_len = 0;
  word_freq = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(token_stream_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(token_stream_->get_next(token, token_freq))) {
    if (OB_ITER_END != ret) {
    }
  } else if (OB_ISNULL(token.ptr_) || OB_UNLIKELY(0 >= token.len_ || 0 >= token_freq)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // The token points into the normalizer buffer, which is reused on the
    // next get_next() call. Downstream the word is hashed immediately and the
    // map key is materialized once per distinct word on first insertion
    // (ObAddWord::groupby_word), so the per-token alloc+memcpy is unneeded.
    word = token.ptr_;
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
    if (OB_FAIL(english_analyzer_.init(analysis_ctx_, pipeline_alloc_))) {
    } else if (OB_FAIL(segment(doc_, token_stream_))) {
    } else if (OB_ISNULL(token_stream_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      is_inited_ = true;
      built_ = true;
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
  is_inited_ = false;
  built_ = false;
}

int ObBEngFTParser::reuse_parser(ObFTParserParam *param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    // stale checkout without a release: drop the per-call state first
    release_to_pool();
  }

  if (OB_UNLIKELY(!built_)) {
    // first use of the slot (or the pipeline was torn down by a failed
    // init): build it through the regular init() path
    return init(param);
  }

  if (OB_ISNULL(param) || OB_UNLIKELY(!param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(UINT32_MAX < param->ft_length_)) {
    ret = OB_NOT_SUPPORTED;
  } else {
    doc_.set_string(param->fulltext_, param->ft_length_);
    analysis_ctx_.cs_ = param->cs_;
    analysis_ctx_.filter_stopword_ = false;
    analysis_ctx_.need_grouping_ = false;
    // refresh (not re-init) the persistent pipeline: the normalizers cache
    // the charset info at init() time, so a collation change between two
    // calls must be propagated in place
    if (OB_FAIL(english_analyzer_.refresh(analysis_ctx_))) {
    } else if (OB_FAIL(segment(doc_, token_stream_))) {
    } else if (OB_ISNULL(token_stream_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

void ObBEngFTParser::release_to_pool()
{
  if (OB_UNLIKELY(!pooled_)) {
    // legacy per-call parser: destroyed by free_token_iter as before
    return;
  }
  // scalar fields only: the analyzer pipeline keeps its streams and their
  // per-document state is reset inside analyze() (tail reuse + tokenizer
  // open) on the next acquire
  analysis_ctx_.reset();
  doc_.reset();
  token_stream_ = nullptr;
  is_inited_ = false;
  // return the per-call scratch pages (see the ik release_to_pool() note for
  // why reset() instead of reuse())
  allocator_.reset();
}

namespace
{
// Thread-local reuse pool for ObBEngFTParser. Same scope/bound/storage
// contract as the ik pool in ob_ik_ft_parser.cpp: strictly per-thread,
// bounded slot count with legacy fallback, placement-new'd into a
// thread_local POD buffer and never destroyed.
constexpr int64_t BENG_REUSE_POOL_SLOT_CNT = 2;

struct ObBEngParserSlot
{
  ObArenaAllocator obj_alloc_;
  ObArenaAllocator scratch_alloc_;
  ObBEngFTParser *parser_;
  bool in_use_;
  ObBEngParserSlot()
      : obj_alloc_(lib::ObMemAttr("FTBengMeta")),
        scratch_alloc_(lib::ObMemAttr("FTBengScratch")),
        parser_(nullptr),
        in_use_(false)
  {
  }
};

struct ObBEngParserPool
{
  ObBEngParserSlot slots_[BENG_REUSE_POOL_SLOT_CNT];

  ObBEngParserSlot *find_free_slot()
  {
    ObBEngParserSlot *slot = nullptr;
    for (int64_t i = 0; OB_ISNULL(slot) && i < BENG_REUSE_POOL_SLOT_CNT; ++i) {
      if (!slots_[i].in_use_) {
        slot = &slots_[i];
      }
    }
    return slot;
  }

  void release_slot(ObBEngFTParser *parser)
  {
    for (int64_t i = 0; i < BENG_REUSE_POOL_SLOT_CNT; ++i) {
      if (slots_[i].parser_ == parser) {
        slots_[i].in_use_ = false;
      }
    }
  }
};

ObBEngParserPool *get_tl_beng_pool()
{
  static thread_local ObBEngParserPool *pool = nullptr;
  if (OB_ISNULL(pool)) {
    alignas(alignof(ObBEngParserPool)) static thread_local char buf[sizeof(ObBEngParserPool)];
    pool = new (buf) ObBEngParserPool();
  }
  return pool;
}

// Acquire a pooled beng parser for this thread; same contract as
// acquire_reuse_parser() for ik.
int acquire_reuse_beng_parser(ObFTParserParam *param,
                              ObBEngFTParser *&parser,
                              bool &pooled)
{
  int ret = OB_SUCCESS;
  parser = nullptr;
  pooled = false;
  ObBEngParserPool *pool = get_tl_beng_pool();
  ObBEngParserSlot *slot = pool->find_free_slot();
  if (OB_ISNULL(slot)) {
    // all slots busy: fall back to the legacy per-call path
  } else {
    slot->in_use_ = true;
    if (OB_ISNULL(slot->parser_)) {
      ObBEngFTParser *new_parser = OB_NEWx(ObBEngFTParser,
                                           &(slot->obj_alloc_),
                                           slot->scratch_alloc_,
                                           slot->obj_alloc_);
      if (OB_ISNULL(new_parser)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        slot->parser_ = new_parser;
        new_parser->set_pooled();
      }
    }
    if (OB_SUCC(ret)) {
      parser = slot->parser_;
      // reuse_parser() runs a full init() on the first use of the slot (or
      // after a reset()) and the cheap re-apply path afterwards. init() has
      // already reset() the partial state when it fails.
      if (OB_FAIL(parser->reuse_parser(param))) {
        parser->release_to_pool();
      }
    }
    if (OB_FAIL(ret)) {
      parser = nullptr;
      slot->in_use_ = false;
    } else {
      pooled = true;
    }
  }
  return ret;
}
} // anonymous namespace

ObBasicEnglishFTParserDesc::ObBasicEnglishFTParserDesc() {}

int ObBasicEnglishFTParserDesc::segment(
    ObFTParserParam *param,
    ObITokenIterator *&iter) const
{
  int ret = OB_SUCCESS;
  ObBEngFTParser *parser = nullptr;
  bool pooled = false;
  if (OB_ISNULL(param) || OB_ISNULL(param->fulltext_) || OB_UNLIKELY(!param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(acquire_reuse_beng_parser(param, parser, pooled))) {
    // real error from the pooled path: same visible behavior as a legacy
    // init failure (no iterator handed out, error propagated)
  } else if (pooled) {
    iter = parser;
  } else if (OB_ISNULL(parser = OB_NEWx(ObBEngFTParser, param->allocator_, *(param->allocator_)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(parser->init(param))) {
  } else {
    iter = parser;
  }

  if (OB_FAIL(ret) && !pooled) {
    OB_DELETEx(ObBEngFTParser, param->allocator_, parser);
  }

  return ret;
}

void ObBasicEnglishFTParserDesc::free_token_iter(
    ObFTParserParam *param,
    ObITokenIterator *&iter) const
{
  if (OB_NOT_NULL(iter)) {
    ObBEngFTParser *parser = static_cast<ObBEngFTParser *>(iter);
    if (parser->is_pooled()) {
      // drop the per-call state and let the parser sleep in its slot
      parser->release_to_pool();
      get_tl_beng_pool()->release_slot(parser);
    } else {
      abort_unless(nullptr != param);
      abort_unless(nullptr != param->allocator_);
      iter->~ObITokenIterator();
      param->allocator_->free(iter);
    }
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
