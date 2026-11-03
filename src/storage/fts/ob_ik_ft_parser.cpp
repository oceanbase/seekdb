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

#include "src/storage/fts/ob_ik_ft_parser.h"

#include "lib/charset/ob_charset.h"
#include "lib/allocator/page_arena.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/utility.h"
#include "data_plane/fts/ob_fts_struct.h"
#include "data_plane/fts/ob_fts_parser_helper.h"
#include "storage/fts/dict/ob_ft_dict.h"
#include "storage/fts/dict/ob_ft_dict_def.h"
#include "storage/fts/dict/ob_ft_dict_hub.h"
#include "storage/fts/dict/ob_ft_range_dict.h"
#include "storage/fts/ik/ob_ik_arbitrator.h"
#include "storage/fts/ik/ob_ik_cjk_processor.h"
#include "storage/fts/ik/ob_ik_letter_processor.h"
#include "storage/fts/ik/ob_ik_processor.h"
#include "storage/fts/ik/ob_ik_quantifier_processor.h"
#include "storage/fts/ik/ob_ik_surrogate_processor.h"

namespace oceanbase
{
namespace storage
{
int ObIKFTParser::init(const ObFTParserParam &param)
{
  int ret = OB_SUCCESS;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else {
    coll_type_ = ObCollationType::CS_TYPE_INVALID;
    if (OB_ISNULL(param.cs_) || OB_ISNULL(param.cs_->name)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (CS_TYPE_INVALID == (coll_type_ = ObCharset::collation_type(param.cs_->name))) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(init_dict(param))) {
    } else if (OB_FAIL(init_ctx(param))) {
    } else if (OB_FAIL(init_segmenter(param))) {
    }

    if (OB_FAIL(ret)) {
      reset();
    } else {
      is_inited_ = true;
      built_ = true;
    }
  }

  return ret;
}

int ObIKFTParser::get_next_token(const char *&word,
                                 int64_t &word_len,
                                 int64_t &char_cnt,
                                 int64_t &word_freq)
{
  int ret = OB_SUCCESS;
  const char *output_word;
  int64_t len;
  int64_t offset;
  int64_t cnt;

  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else {
    bool accept_token = false;
    while (OB_SUCC(ret) && !accept_token) {
      if (OB_FAIL(produce())) {
      } else if (OB_FAIL(ctx_->get_next_token(output_word, len, offset, cnt))) {
        if (OB_ITER_END == ret) {
          // ok, end this iter
        } else {
        }
      } else {
        bool is_stop = false;
        // if (!OB_ISNULL(dict_stop_)
        //     && OB_FAIL(dict_stop_->match(ObString(len, output_word + offset), is_stop))) {
        //   LOG_WARN("Failed to match stopwords", K(ret));
        // } else
        if (!is_stop) {
          word = output_word + offset;
          word_len = len;
          char_cnt = cnt;
          word_freq = 1;
          accept_token = true;
        } else {
        }
      }
    }
  }

  return ret;
}

int ObIKFTParser::produce()
{
  int ret = OB_SUCCESS;
  // Loop until end or has data to output
  while (OB_SUCC(ret) && ctx_->result_list().empty() && !ctx_->iter_end()) {
    if (OB_FAIL(process_next_batch())) {
      if (OB_ITER_END == ret) {
        // ok
      } else {
      }
    }
  }
  if (OB_ITER_END == ret) {
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObIKFTParser::process_one_char(TokenizeContext &ctx,
                                   const char *ch,
                                   const uint8_t char_len,
                                   const ObFTCharUtil::CharType type)
{
  int ret = OB_SUCCESS;
  // proces by char with all segmenters (fixed insertion order preserved)
  for (int64_t si = 0; OB_SUCC(ret) && si < SEGMENTER_SLOT_CNT; ++si) {
    if (OB_NOT_NULL(segmenters_[si])) {
      if (OB_FAIL(segmenters_[si]->process(ctx, ch, char_len, type))) {
      }
    }
  }
  return ret;
}

int ObIKFTParser::process_next_batch()
{
  int ret = OB_SUCCESS;
  ctx_->reset_resource();

  // handle next segmenter
  bool do_seg = false;

  if (ctx_->iter_end()) {
    ret = OB_ITER_END;
  } else {
    while (OB_SUCC(ret) && !do_seg && !ctx_->iter_end()) {
      const char *ch;
      uint8_t char_len = 0;
      ObFTCharUtil::CharType type = ObFTCharUtil::CharType::USELESS;
      if (OB_FAIL(ctx_->current_char(ch, char_len))) {
      } else if (OB_FAIL(ctx_->current_char_type(type))) {
      } else if (OB_FAIL(process_one_char(*ctx_, ch, char_len, type))) {
      } else {
        // 1. check segmention
        if (ctx_->handle_size() > SEGMENT_LIMIT && type == ObFTCharUtil::CharType::USELESS) {
          do_seg = true;
        }

        // 2. move to next;
        if (OB_FAIL(ctx_->step_next())) {
          if (OB_ITER_END == ret) {
          } else {
          }
        }
      } // end of one batch
    }

    if (OB_SUCC(ret) || OB_ITER_END == ret) {
      ObIKArbitrator arb;
      if (OB_FAIL(arb.process(*ctx_))) {
      } else if (OB_FAIL(arb.output_result(*ctx_))) {
      }
    } else {
      // Already logged.
    }
  }

  return ret;
}


namespace
{
// Thread-local reuse pool for ObIKFTParser (official blueprint reuse_parser).
//
// Scope: strictly per-thread (thread_local storage), so a pooled parser is
// never touched by two threads at once and no lock is needed on the hot path.
// Bound: IK_REUSE_POOL_SLOT_CNT slots per thread per parser kind; when all
// slots are busy the descriptor falls back to the legacy per-call path, so
// the pool bounds reuse, never availability. The pool is placement-new'd
// into a thread_local POD buffer and intentionally never destroyed: its
// purpose is to outlive calls, and skipping the destructor avoids the
// thread-exit ordering hazards against the global module allocator and the
// dict KVCache; the pages are reclaimed by the OS with the thread/process.
constexpr int64_t IK_REUSE_POOL_SLOT_CNT = 2;

struct ObIKParserSlot
{
  ObArenaAllocator obj_alloc_;
  ObArenaAllocator scratch_alloc_;
  ObIKFTParser *parser_;
  bool in_use_;
  ObIKParserSlot()
      : obj_alloc_(lib::ObMemAttr("FTIkParserMeta")),
        scratch_alloc_(lib::ObMemAttr("FTIkScratch")),
        parser_(nullptr),
        in_use_(false)
  {
  }
};

struct ObIKParserPool
{
  ObIKParserSlot slots_[IK_REUSE_POOL_SLOT_CNT];

  ObIKParserSlot *find_free_slot()
  {
    ObIKParserSlot *slot = nullptr;
    for (int64_t i = 0; OB_ISNULL(slot) && i < IK_REUSE_POOL_SLOT_CNT; ++i) {
      if (!slots_[i].in_use_) {
        slot = &slots_[i];
      }
    }
    return slot;
  }

  void release_slot(ObIKFTParser *parser)
  {
    for (int64_t i = 0; i < IK_REUSE_POOL_SLOT_CNT; ++i) {
      if (slots_[i].parser_ == parser) {
        slots_[i].in_use_ = false;
      }
    }
  }
};

ObIKParserPool *get_tl_ik_pool()
{
  static thread_local ObIKParserPool *pool = nullptr;
  if (OB_ISNULL(pool)) {
    alignas(alignof(ObIKParserPool)) static thread_local char buf[sizeof(ObIKParserPool)];
    pool = new (buf) ObIKParserPool();
  }
  return pool;
}

// Acquire a pooled parser for this thread. Returns:
//   OB_SUCCESS with pooled=true  - parser ready for use (fresh or reused)
//   OB_SUCCESS with pooled=false - pool busy/full: caller uses the legacy
//                                  per-call path
//   other ret                    - the pooled path failed; the slot is free
//                                  for the next acquire
int acquire_reuse_parser(ObFTParserParam *param, ObFTDictHub *hub,
                         ObIKFTParser *&parser, bool &pooled)
{
  int ret = OB_SUCCESS;
  parser = nullptr;
  pooled = false;
  ObIKParserPool *pool = get_tl_ik_pool();
  ObIKParserSlot *slot = pool->find_free_slot();
  if (OB_ISNULL(slot)) {
    // all slots busy: fall back to the legacy per-call path
  } else {
    slot->in_use_ = true;
    if (OB_ISNULL(slot->parser_)) {
      ObIKFTParser *new_parser = OB_NEWx(ObIKFTParser,
                                         &(slot->obj_alloc_),
                                         slot->scratch_alloc_,
                                         slot->obj_alloc_,
                                         hub);
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
      if (OB_FAIL(parser->reuse_parser(*param))) {
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

int ObIKFTParserDesc::segment(ObFTParserParam *param, ObITokenIterator *&iter) const
{
  int ret = OB_SUCCESS;
  ObIKFTParser *parser = nullptr;
  ObFTDictHub *hub = nullptr;
  bool pooled = false;
  if (OB_ISNULL(param) || OB_UNLIKELY(!param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObFTParseData::instance().get_dict_hub(hub))) {
  } else if (OB_FAIL(acquire_reuse_parser(param, hub, parser, pooled))) {
    // real error from the pooled path: same visible behavior as a legacy
    // init failure (no iterator handed out, error propagated)
  } else if (pooled) {
    iter = parser;
  } else if (OB_ISNULL(parser = OB_NEWx(ObIKFTParser, param->allocator_, *(param->allocator_), hub))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(parser->init(*param))) {
  } else {
    iter = parser;
  }

  if (OB_FAIL(ret) && !pooled) {
    OB_DELETEx(ObIKFTParser, param->allocator_, parser);
  }

  return ret;
}

void ObIKFTParserDesc::free_token_iter(ObFTParserParam *param,
                                       ObITokenIterator *&iter) const
{
  if (OB_NOT_NULL(iter)) {
    ObIKFTParser *parser = static_cast<ObIKFTParser *>(iter);
    if (parser->is_pooled()) {
      // drop the per-call scratch state and let the parser sleep in its slot
      parser->release_to_pool();
      get_tl_ik_pool()->release_slot(parser);
    } else {
      iter->~ObITokenIterator();
      param->allocator_->free(iter);
    }
  }
}


int ObIKFTParserDesc::get_add_word_flag(ObAddWordFlag &flag) const
{
  int ret = OB_SUCCESS;
  flag.set_casedown();
  flag.set_groupby_word();
  return ret;
}

int ObIKFTParser::init_dict(const ObFTParserParam &param)
{
  int ret = OB_SUCCESS;
  ObIFTDict *tmp_dict = nullptr;

  if (OB_ISNULL(hub_)) {
    ret = OB_ERR_UNEXPECTED;
  }

  ObFTRangeDict *dict = nullptr;
  ObFTDictDesc main_dict_desc("main_dict",
                              ObFTDictType::DICT_IK_MAIN,
                              ObCharsetType::CHARSET_UTF8MB4,
                              ObCollationType::CS_TYPE_UTF8MB4_BIN);

  ObFTDictDesc quan_dict_desc("quan_dict",
                              ObFTDictType::DICT_IK_QUAN,
                              ObCharsetType::CHARSET_UTF8MB4,
                              ObCollationType::CS_TYPE_UTF8MB4_BIN);

  ObFTDictDesc stopword_dict_desc("stopword",
                                  ObFTDictType::DICT_IK_STOP,
                                  ObCharsetType::CHARSET_UTF8MB4,
                                  ObCollationType::CS_TYPE_UTF8MB4_BIN);

  if (should_read_newest_table()) {
    // clear dict cache, always false now
  } else {
    if (OB_FAIL(init_single_dict(main_dict_desc, cache_main_))) {
    } else if (OB_FAIL(init_single_dict(quan_dict_desc, cache_quan_))) {
    } else if (OB_FAIL(init_single_dict(stopword_dict_desc, cache_stop_))) {
    }
  }

  if (OB_FAIL(ret)) {
    // already logged.
  } else if (OB_FAIL(build_dict_from_cache(main_dict_desc, cache_main_, dict_main_))) {
  } else if (OB_FAIL(build_dict_from_cache(quan_dict_desc, cache_quan_, dict_quan_))) {
  } else if (OB_FAIL(build_dict_from_cache(stopword_dict_desc, cache_stop_, dict_stop_))) {
  }

  return ret;
}

int ObIKFTParser::init_single_dict(ObFTDictDesc desc, ObFTCacheRangeContainer &container)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(hub_->load_cache(desc, container))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      if (OB_FAIL(hub_->build_cache(desc, container))) {
      }
    } else {
    }
  }
  return ret;
}

int ObIKFTParser::init_ctx(const ObFTParserParam &param)
{
  int ret = OB_SUCCESS;

  if (coll_type_ == common::CS_TYPE_INVALID) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(ctx_ = OB_NEWx(TokenizeContext,
                                      &obj_alloc_,
                                      coll_type_,
                                      allocator_,
                                      param.fulltext_,
                                      param.ft_length_,
                                      param.ik_param_.mode_ == ObFTIKParam::Mode::SMART))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(ctx_->init())) {
  }
  if (OB_FAIL(ret)) {
    OB_DELETEx(TokenizeContext, &obj_alloc_, ctx_);
  }
  return ret;
}

int ObIKFTParser::init_segmenter(const ObFTParserParam &param)
{
  int ret = OB_SUCCESS;
  // do have an order
  ObIKLetterProcessor *letter_seg = nullptr;
  ObIKQuantifierProcessor *cnqsg = nullptr;
  ObIKCJKProcessor *cjksg = nullptr;
  ObIKSurrogateProcessor *surrogate_seg = nullptr;
  if (OB_ISNULL(letter_seg = OB_NEWx(ObIKLetterProcessor, &obj_alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(dict_quan_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(cnqsg = OB_NEWx(ObIKQuantifierProcessor, &obj_alloc_, *dict_quan_, allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(dict_main_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(cjksg = OB_NEWx(ObIKCJKProcessor, &obj_alloc_, *dict_main_, allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(surrogate_seg = OB_NEWx(ObIKSurrogateProcessor, &obj_alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    // push back by order, quantifier is before cjk (same order as the old
    // ObList: letter -> quantifier -> cjk -> surrogate)
    segmenters_[0] = letter_seg;
    segmenters_[1] = cnqsg;
    segmenters_[2] = cjksg;
    segmenters_[3] = surrogate_seg;
    letter_seg = nullptr;
    cnqsg = nullptr;
    cjksg = nullptr;
    surrogate_seg = nullptr;
  }
  // push back by order, quantifier is before cjk

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObIKLetterProcessor, &obj_alloc_, letter_seg);
    OB_DELETEx(ObIKQuantifierProcessor, &obj_alloc_, cnqsg);
    OB_DELETEx(ObIKCJKProcessor, &obj_alloc_, cjksg);
    OB_DELETEx(ObIKSurrogateProcessor, &obj_alloc_, surrogate_seg);
  }
  return ret;
}

void ObIKFTParser::reset()
{
  if (!OB_ISNULL(ctx_)) {
    ctx_->~TokenizeContext();
    obj_alloc_.free(ctx_);
  }

  for (int64_t si = 0; si < SEGMENTER_SLOT_CNT; ++si) {
    if (!OB_ISNULL(segmenters_[si])) {
      segmenters_[si]->~ObIIKProcessor();
      obj_alloc_.free(segmenters_[si]);
      segmenters_[si] = nullptr;
    }
  }

  cache_main_.reset();
  cache_quan_.reset();
  cache_stop_.reset();

  if (!OB_ISNULL(dict_main_)) {
    dict_main_->~ObIFTDict();
    obj_alloc_.free(dict_main_);
  }
  if (!OB_ISNULL(dict_quan_)) {
    dict_quan_->~ObIFTDict();
    obj_alloc_.free(dict_quan_);
  }
  if (!OB_ISNULL(dict_stop_)) {
    dict_stop_->~ObIFTDict();
    obj_alloc_.free(dict_stop_);
  }

  // null the torn-down pointers: release_to_pool() guards on ctx_ after a
  // failed init(), and a dangling non-null ctx_ there would re-enter the
  // destroyed object (formal UB on the pooled failure path)
  ctx_ = nullptr;
  dict_main_ = nullptr;
  dict_quan_ = nullptr;
  dict_stop_ = nullptr;

  is_inited_ = false;
  built_ = false;
}

int ObIKFTParser::reuse_parser(const ObFTParserParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(IS_INIT)) {
    // stale checkout without a release: drop the per-call state first
    release_to_pool();
  }

  if (OB_UNLIKELY(!built_)) {
    // first use of the slot (or the persistent objects were torn down by a
    // failed init): build them through the regular init() path
    return init(param);
  }

  if (OB_ISNULL(param.cs_) || OB_ISNULL(param.cs_->name)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (CS_TYPE_INVALID == (coll_type_ = ObCharset::collation_type(param.cs_->name))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(ctx_) || OB_ISNULL(segmenters_[0])) {
    // persistent objects were never built (or were torn down): fall back to
    // a full init() on the next acquire
    built_ = false;
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ctx_->reinit(coll_type_,
                                  param.fulltext_,
                                  param.ft_length_,
                                  param.ik_param_.mode_ == ObFTIKParam::Mode::SMART))) {
  } else {
    // the pending-token state of every segmenter was already dropped by
    // release_to_pool(); reset it here as well so reuse_parser alone never
    // carries cross-call state even when called without a prior release.
    for (int64_t si = 0; OB_SUCC(ret) && si < SEGMENTER_SLOT_CNT; ++si) {
      if (OB_NOT_NULL(segmenters_[si])) {
        segmenters_[si]->reset_state();
      }
    }
    is_inited_ = true;
  }

  return ret;
}

void ObIKFTParser::release_to_pool()
{
  if (OB_UNLIKELY(!pooled_)) {
    // legacy per-call parser: its scratch is the eval arena and the object
    // is destroyed by free_token_iter as before; nothing to recycle here.
    return;
  }

  // 1. segmenters first: drop the pending-token state; the quantifier/cjk
  //    hit lists live in the scratch arena and are walked here while their
  //    memory is still valid.
  for (int64_t si = 0; si < SEGMENTER_SLOT_CNT; ++si) {
    if (OB_NOT_NULL(segmenters_[si])) {
      segmenters_[si]->reset_state();
    }
  }

  // 2. context: walk-and-clear the per-call token/result containers and drop
  //    the per-character cache table (char_infos_) so no stale entry can leak
  //    into the next fulltext.
  if (OB_NOT_NULL(ctx_)) {
    ctx_->release_scratch();
  }

  // 3. recycle the per-call scratch memory. reset() (not reuse()): the
  //    retained-page path of reuse() was measured to grow RSS linearly with
  //    large documents (page list bookkeeping vs. the per-call big-page
  //    churn), while reset() returns every page to the module allocator, so
  //    the per-call footprint is exactly what a fresh parser would have
  //    allocated. The pool still saves the parser/ctx/segmenter/dict builds.
  allocator_.reset();

  is_inited_ = false;
}

bool ObIKFTParser::should_read_newest_table() const { return false; }
int ObIKFTParser::build_dict_from_cache(const ObFTDictDesc &desc,
                                        ObFTCacheRangeContainer &container,
                                        ObIFTDict *&dict)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(hub_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(dict = OB_NEWx(ObFTRangeDict, &obj_alloc_, allocator_, &container, desc))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(dict->init())) {
  }
  if (OB_FAIL(ret)) {
    OB_DELETEx(ObIFTDict, &obj_alloc_, dict);
  }
  return ret;
}

} //  namespace storage
} //  namespace oceanbase
