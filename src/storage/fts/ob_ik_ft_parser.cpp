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

namespace
{

// One snapshot of the built IK dictionaries per thread. The objects are kept in the
// arena and the cache range containers keep the kv cache handles of the dictionary
// ranges pinned, so the snapshot stays valid until it is rebuilt explicitly.
struct ObIKDictSnapshot
{
  ObIKDictSnapshot() : valid_(false), arena_(ObMemAttr("IKDictSnapshot"))
  {
    for (int64_t i = 0; i < DICT_SLOT_CNT; ++i) {
      conts_[i] = nullptr;
      dicts_[i] = nullptr;
      probes_[i] = nullptr;
    }
  }
  // No destructor on purpose: the snapshot is a thread_local of long living threads and
  // tearing it down while the kv cache is being destroyed would be unsafe.
  void clear()
  {
    for (int64_t i = 0; i < DICT_SLOT_CNT; ++i) {
      if (OB_NOT_NULL(dicts_[i])) {
        dicts_[i]->~ObFTRangeDict();
        dicts_[i] = nullptr;
      }
      if (OB_NOT_NULL(conts_[i])) {
        conts_[i]->~ObFTCacheRangeContainer();
        conts_[i] = nullptr;
      }
      probes_[i] = nullptr;
    }
    arena_.reset();
    valid_ = false;
  }
  static const int64_t DICT_SLOT_CNT = 3;
  bool valid_;
  ObArenaAllocator arena_;
  ObFTCacheRangeContainer *conts_[DICT_SLOT_CNT];
  ObFTRangeDict *dicts_[DICT_SLOT_CNT];
  const ObDictCacheValue *probes_[DICT_SLOT_CNT];
};

thread_local ObIKDictSnapshot g_ik_dict_snapshot;

} // anonymous namespace

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
  // proces by char with all segmenters
  for (ObList<ObIIKProcessor *, ObIAllocator>::iterator iter = segmenters_.begin();
       OB_SUCC(ret) && iter != segmenters_.end();
       iter++) {
    if (OB_FAIL((*iter)->process(ctx))) {
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

int ObIKFTParserDesc::segment(ObFTParserParam *param, ObITokenIterator *&iter) const
{
  int ret = OB_SUCCESS;
  ObIKFTParser *parser = nullptr;
  ObFTDictHub *hub = nullptr;
  if (OB_ISNULL(param) || OB_UNLIKELY(!param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObFTParseData::instance().get_dict_hub(hub))) {
  } else if (OB_ISNULL(parser = OB_NEWx(ObIKFTParser, param->allocator_, *(param->allocator_), hub))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(parser->init(*param))) {
  } else {
    iter = parser;
  }

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObIKFTParser, param->allocator_, parser);
  }

  return ret;
}

void ObIKFTParserDesc::free_token_iter(ObFTParserParam *param,
                                       ObITokenIterator *&iter) const
{
  iter->~ObITokenIterator();
  param->allocator_->free(iter);
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
    const ObFTDictDesc *descs[3] = {&main_dict_desc, &quan_dict_desc, &stopword_dict_desc};
    bool hit = false;
    if (OB_FAIL(check_dict_snapshot(descs, hit))) {
    } else if (!hit && OB_FAIL(build_dict_snapshot(descs))) {
    } else {
      dict_main_ = g_ik_dict_snapshot.dicts_[0];
      dict_quan_ = g_ik_dict_snapshot.dicts_[1];
      dict_stop_ = g_ik_dict_snapshot.dicts_[2];
      borrowed_dicts_ = true;
    }
  }

  return ret;
}

int ObIKFTParser::probe_dict(const ObFTDictDesc &desc, const ObDictCacheValue *&probe)
{
  int ret = OB_SUCCESS;
  common::ObKVCacheHandle handle;
  const uint64_t name = static_cast<uint64_t>(desc.type_);
  ObDictCacheKey key(name, desc.type_, 0);
  probe = nullptr;
  if (OB_FAIL(ObDictCache::get_instance().get_dict(key, probe, handle))) {
    STORAGE_FTS_LOG(WARN, "failed to probe the dict cache", K(ret));
  }
  return ret;
}

int ObIKFTParser::check_dict_snapshot(const ObFTDictDesc *descs[3], bool &hit)
{
  int ret = OB_SUCCESS;
  ObIKDictSnapshot &snapshot = g_ik_dict_snapshot;
  hit = snapshot.valid_;
  for (int64_t i = 0; OB_SUCC(ret) && hit && i < ObIKDictSnapshot::DICT_SLOT_CNT; ++i) {
    const ObDictCacheValue *probe = nullptr;
    if (OB_FAIL(probe_dict(*descs[i], probe))) {
      // a missing range means that the dictionary has to be fetched again
      hit = false;
      ret = OB_SUCCESS;
    } else if (probe != snapshot.probes_[i]) {
      // the dictionary was rebuilt, the cached range dicts are stale
      hit = false;
    }
  }
  return ret;
}

int ObIKFTParser::build_dict_snapshot(const ObFTDictDesc *descs[3])
{
  int ret = OB_SUCCESS;
  ObIKDictSnapshot &snapshot = g_ik_dict_snapshot;
  snapshot.clear();
  for (int64_t i = 0; OB_SUCC(ret) && i < ObIKDictSnapshot::DICT_SLOT_CNT; ++i) {
    ObFTCacheRangeContainer *container = nullptr;
    if (OB_ISNULL(container = OB_NEWx(ObFTCacheRangeContainer, &snapshot.arena_, snapshot.arena_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_FTS_LOG(WARN, "failed to allocate the dict cache container", K(ret));
    } else if (FALSE_IT(snapshot.conts_[i] = container)) {
    } else if (OB_FAIL(init_single_dict(*descs[i], *container))) {
    } else if (OB_ISNULL(snapshot.dicts_[i] = OB_NEWx(ObFTRangeDict, &snapshot.arena_,
                                                     snapshot.arena_, container, *descs[i]))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_FTS_LOG(WARN, "failed to allocate the range dict", K(ret));
    } else if (OB_FAIL(snapshot.dicts_[i]->init())) {
    } else if (OB_FAIL(probe_dict(*descs[i], snapshot.probes_[i]))) {
    }
  }
  if (OB_SUCC(ret)) {
    snapshot.valid_ = true;
  } else {
    snapshot.clear();
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
                                      &allocator_,
                                      coll_type_,
                                      allocator_,
                                      param.fulltext_,
                                      param.ft_length_,
                                      param.ik_param_.mode_ == ObFTIKParam::Mode::SMART))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(ctx_->init())) {
  }
  if (OB_FAIL(ret)) {
    OB_DELETEx(TokenizeContext, &allocator_, ctx_);
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
  if (OB_ISNULL(letter_seg = OB_NEWx(ObIKLetterProcessor, &allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(dict_quan_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(cnqsg = OB_NEWx(ObIKQuantifierProcessor, &allocator_, *dict_quan_, allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(dict_main_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(cjksg = OB_NEWx(ObIKCJKProcessor, &allocator_, *dict_main_, allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(surrogate_seg = OB_NEWx(ObIKSurrogateProcessor, &allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(segmenters_.push_back(letter_seg))) {
  } else if (FALSE_IT(letter_seg = nullptr)) {
  } else if (OB_FAIL(segmenters_.push_back(cnqsg))) {
  } else if (FALSE_IT(cnqsg = nullptr)) {
  } else if (OB_FAIL(segmenters_.push_back(cjksg))) {
  } else if (FALSE_IT(cjksg = nullptr)) {
  } else if (OB_FAIL(segmenters_.push_back(surrogate_seg))) {
  } else if (OB_FALSE_IT(surrogate_seg = nullptr)) {
  }
  // push back by order, quantifier is before cjk

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObIKLetterProcessor, &allocator_, letter_seg);
    OB_DELETEx(ObIKQuantifierProcessor, &allocator_, cnqsg);
    OB_DELETEx(ObIKCJKProcessor, &allocator_, cjksg);
    OB_DELETEx(ObIKSurrogateProcessor, &allocator_, surrogate_seg);
  }
  return ret;
}

void ObIKFTParser::reset()
{
  if (!OB_ISNULL(ctx_)) {
    ctx_->~TokenizeContext();
    allocator_.free(ctx_);
  }

  for (ObIIKProcessor *segmenter : segmenters_) {
    if (!OB_ISNULL(segmenter)) {
      segmenter->~ObIIKProcessor();
      allocator_.free(segmenter);
    }
  }
  segmenters_.clear();

  cache_main_.reset();
  cache_quan_.reset();
  cache_stop_.reset();

  if (!borrowed_dicts_) {
    if (!OB_ISNULL(dict_main_)) {
      dict_main_->~ObIFTDict();
      allocator_.free(dict_main_);
    }
    if (!OB_ISNULL(dict_quan_)) {
      dict_quan_->~ObIFTDict();
      allocator_.free(dict_quan_);
    }
    if (!OB_ISNULL(dict_stop_)) {
      dict_stop_->~ObIFTDict();
      allocator_.free(dict_stop_);
    }
  }
  dict_main_ = nullptr;
  dict_quan_ = nullptr;
  dict_stop_ = nullptr;
  borrowed_dicts_ = false;

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
  } else if (OB_ISNULL(dict = OB_NEWx(ObFTRangeDict, &allocator_, allocator_, &container, desc))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(dict->init())) {
  }
  if (OB_FAIL(ret)) {
    OB_DELETEx(ObIFTDict, &allocator_, dict);
  }
  return ret;
}

} //  namespace storage
} //  namespace oceanbase
