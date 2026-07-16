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
#include "lib/hash_func/murmur_hash.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/utility.h"
#include "storage/fts/ob_fts_struct.h"
#include "storage/fts/ob_fts_plugin_helper.h"
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
#include "plugin/sys/ob_plugin_mgr.h"

using namespace oceanbase::plugin;

namespace oceanbase
{
namespace storage
{
int ObIKFTParser::init(const ObFTParserParam &param)
{
  int ret = OB_SUCCESS;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("Parser already inited once", K(ret));
  } else {
    coll_type_ = ObCollationType::CS_TYPE_INVALID;
    if (OB_ISNULL(param.cs_) || OB_ISNULL(param.cs_->name)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Invalid parser param.", K(ret));
    } else if (CS_TYPE_INVALID == (coll_type_ = ObCharset::collation_type(param.cs_->name))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Invalid collation type.", K(ret));
    } else if (OB_FAIL(init_dict(param))) {
      LOG_WARN("Failed to init dict", K(ret));
    } else if (OB_FAIL(init_ctx(param))) {
      LOG_WARN("Failed to init ctx", K(ret));
    } else if (OB_FAIL(init_segmenter(param))) {
      LOG_WARN("Failed to init segmenters", K(ret));
    }

    if (OB_FAIL(ret)) {
      reset();
    } else {
      is_inited_ = true;
    }
  }

  return ret;
}

int ObIKFTParser::start_document(const ObFTParserParam &param)
{
  int ret = OB_SUCCESS;
  ObCollationType param_coll_type = ObCollationType::CS_TYPE_INVALID;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ik parser is not initialized", K(ret));
  } else if (OB_ISNULL(param.cs_) || OB_ISNULL(param.cs_->name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parser parameter for new document", K(ret), KP(param.cs_));
  } else if (CS_TYPE_INVALID
             == (param_coll_type = ObCharset::collation_type(param.cs_->name))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid collation type for new document", K(ret));
  } else if (OB_UNLIKELY(param_coll_type != coll_type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("collation type changed when reusing ik parser",
             K(ret),
             K(param_coll_type),
             K(coll_type_));
  } else {
    reset_document_state();

    if (OB_FAIL(init_ctx(param))) {
      LOG_WARN("failed to initialize context for new document",
               K(ret),
               K(param.ft_length_),
               K(param_coll_type));
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
    LOG_WARN("Parser has not been inited", K(ret));
  } else {
    bool accept_token = false;
    while (OB_SUCC(ret) && !accept_token) {
      if (OB_FAIL(produce())) {
        LOG_WARN("Failed to produce new token", K(ret));
      } else if (OB_FAIL(ctx_->get_next_token(output_word, len, offset, cnt))) {
        if (OB_ITER_END == ret) {
          if (current_segment_cache_hit_ || ctx_->iter_end()) {
            ret = init_next_segment();
          } else {
            ret = OB_SUCCESS;
          }
        } else {
          LOG_WARN("Failed to get next token", K(ret));
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
  while (OB_SUCC(ret) && !current_segment_cache_hit_
         && !ctx_->has_pending_result() && !ctx_->iter_end()) {
    if (OB_FAIL(process_next_batch())) {
      if (OB_ITER_END == ret) {
        // ok
      } else {
        LOG_WARN("Failed to load next batch", K(ret));
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
  const bool is_letter = ObFTCharUtil::CharType::ENGLISH_LETTER == type
                         || ObFTCharUtil::CharType::ARABIC_LETTER == type;
  const bool is_surrogate = ObFTCharUtil::CharType::SURROGATE_HIGH == type
                            || ObFTCharUtil::CharType::SURROGATE_LOW == type;

  if (OB_ISNULL(letter_processor_) || OB_ISNULL(quantifier_processor_)
      || OB_ISNULL(cjk_processor_) || OB_ISNULL(surrogate_processor_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ik processor is null", K(ret));
  } else if ((is_letter || letter_processor_->has_pending_state())
             && OB_FAIL(letter_processor_->do_process(ctx, ch, char_len, type))) {
    LOG_WARN("failed to process letter", K(ret));
  } else if ((ObFTCharUtil::CharType::CHINESE == type
              || quantifier_processor_->has_pending_state())
             && OB_FAIL(quantifier_processor_->do_process(ctx, ch, char_len, type))) {
    LOG_WARN("failed to process quantifier", K(ret));
  } else if ((ObFTCharUtil::CharType::USELESS != type || cjk_processor_->has_pending_hits())
             && OB_FAIL(cjk_processor_->do_process(ctx, ch, char_len, type))) {
    LOG_WARN("failed to process cjk", K(ret));
  } else if ((is_surrogate || surrogate_processor_->has_pending_high())
             && OB_FAIL(surrogate_processor_->do_process(ctx, ch, char_len, type))) {
    LOG_WARN("failed to process surrogate", K(ret));
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
        LOG_WARN("Failed to get current char", K(ret));
      } else if (OB_FAIL(ctx_->current_char_type(type))) {
        LOG_WARN("Failed to get current char type", K(ret));
      } else if (OB_FAIL(process_one_char(*ctx_, ch, char_len, type))) {
        LOG_WARN("Failed to process one char", K(ret));
      } else {
        // 1. check segmention
        if (ctx_->handle_size() > SEGMENT_LIMIT && type == ObFTCharUtil::CharType::USELESS) {
          do_seg = true;
        }

        // 2. move to next;
        if (OB_FAIL(ctx_->step_next())) {
          if (OB_ITER_END == ret) {
          } else {
            LOG_WARN("Failed to step next", K(ret));
          }
        }
      } // end of one batch
    }

    if (OB_SUCC(ret) || OB_ITER_END == ret) {
      if (OB_FAIL(arbitrator_.process(*ctx_))) {
        LOG_WARN("failed to process arbitrator", K(ret));
      } else if (OB_FAIL(arbitrator_.output_result(*ctx_))) {
        LOG_WARN("failed to make result list", K(ret));
      } else if (ctx_->iter_end() && use_segment_cache_
                 && OB_FAIL(save_current_segment())) {
        LOG_WARN("failed to save ik segment cache", K(ret));
      }
    } else {
      // Already logged.
    }
  }

  return ret;
}

int ObIKFTParserDesc::init(ObPluginParam *param)
{
  is_inited_ = true;
  return OB_SUCCESS;
}

int ObIKFTParserDesc::deinit(ObPluginParam *param)
{
  is_inited_ = false;
  return OB_SUCCESS;
}

int ObIKFTParserDesc::segment(ObFTParserParam *param, ObITokenIterator *&iter) const
{
  int ret = OB_SUCCESS;
  ObIKFTParser *parser = nullptr;
  ObFTDictHub *hub = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("default ft parser desc hasn't be initialized", K(ret), K(is_inited_));
  } else if (OB_ISNULL(param) || OB_UNLIKELY(!param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(param));
  } else if (OB_FAIL(ObFTParsePluginData::instance().get_dict_hub(hub))) {
    LOG_WARN("Failed to get dict hub.", K(ret));
  } else if (OB_ISNULL(parser = OB_NEWx(ObIKFTParser, param->allocator_, *(param->allocator_), hub))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate ik ft parser", K(ret));
  } else if (OB_FAIL(parser->init(*param))) {
    LOG_WARN("fail to init ik parser", K(ret), KPC(param));
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

int ObIKFTParser::init_dict(const plugin::ObFTParserParam &param)
{
  int ret = OB_SUCCESS;
  ObIFTDict *tmp_dict = nullptr;

  if (OB_ISNULL(hub_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Dict hub is not inited", K(ret));
  }

  ObFTRangeDict *dict = nullptr;
  const ObString main_dict_name =
      param.ik_param_.main_dict_.empty()
          ? ObString::make_string("main_dict")
          : param.ik_param_.main_dict_;

  const ObString quan_dict_name =
      param.ik_param_.quan_dict_.empty()
          ? ObString::make_string("quan_dict")
          : param.ik_param_.quan_dict_;

  const ObString stopword_dict_name =
      param.ik_param_.stopword_dict_.empty()
          ? ObString::make_string("stopword")
          : param.ik_param_.stopword_dict_;

  ObFTDictDesc main_dict_desc(main_dict_name,
                              ObFTDictType::DICT_IK_MAIN,
                              ObCharsetType::CHARSET_UTF8MB4,
                              ObCollationType::CS_TYPE_UTF8MB4_BIN);

  ObFTDictDesc quan_dict_desc(quan_dict_name,
                              ObFTDictType::DICT_IK_QUAN,
                              ObCharsetType::CHARSET_UTF8MB4,
                              ObCollationType::CS_TYPE_UTF8MB4_BIN);

  ObFTDictDesc stopword_dict_desc(stopword_dict_name,
                                  ObFTDictType::DICT_IK_STOP,
                                  ObCharsetType::CHARSET_UTF8MB4,
                                  ObCollationType::CS_TYPE_UTF8MB4_BIN);

  if (should_read_newest_table()) {
    // clear dict cache, always false now
  } else {
    if (OB_FAIL(init_single_dict(main_dict_desc, cache_main_))) {
      LOG_WARN("Failed to init main dict", K(ret));
    } else if (OB_FAIL(init_single_dict(quan_dict_desc, cache_quan_))) {
      LOG_WARN("Failed to init quantifier dict", K(ret));
    } else if (OB_FAIL(init_single_dict(stopword_dict_desc, cache_stop_))) {
      LOG_WARN("Failed to init stopword dict", K(ret));
    }
  }

  if (OB_FAIL(ret)) {
    // already logged.
  } else if (OB_FAIL(build_dict_from_cache(main_dict_desc, cache_main_, dict_main_))) {
    LOG_WARN("Failed to build dict main", K(ret));
  } else if (OB_FAIL(build_dict_from_cache(quan_dict_desc, cache_quan_, dict_quan_))) {
    LOG_WARN("Failed to build dict quantifier", K(ret));
  } else if (OB_FAIL(build_dict_from_cache(stopword_dict_desc, cache_stop_, dict_stop_))) {
    LOG_WARN("Failed to build dict stopword", K(ret));
  }

  return ret;
}

int ObIKFTParser::init_single_dict(ObFTDictDesc desc, ObFTCacheRangeContainer &container)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(hub_->load_cache(desc, container))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      if (OB_FAIL(hub_->build_cache(desc, container))) {
        LOG_WARN("Failed to read newest main table", K(ret));
      }
    } else {
      LOG_WARN("Failed to load cache", K(ret));
    }
  }
  return ret;
}

int ObIKFTParser::init_ctx(const ObFTParserParam &param)
{
  int ret = OB_SUCCESS;

  if (coll_type_ == common::CS_TYPE_INVALID) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("illegal collation type", K(ret), K(coll_type_));
  } else {
    document_fulltext_ = param.fulltext_;
    document_fulltext_len_ = param.ft_length_;
    next_segment_offset_ = 0;
    current_segment_offset_ = 0;
    current_segment_len_ = 0;
    document_is_smart_ = param.ik_param_.mode_ == ObFTIKParam::Mode::SMART;
    use_segment_cache_ = param.ik_param_.main_dict_.empty()
                         && param.ik_param_.quan_dict_.empty()
                         && param.ik_param_.stopword_dict_.empty();
    if (OB_FAIL(init_next_segment())) {
      LOG_WARN("failed to initialize first ik segment", K(ret), K(param.ft_length_));
    }
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(ctx_)) {
    ctx_->~TokenizeContext();
    scratch_allocator_.free(ctx_);
    ctx_ = nullptr;
    scratch_allocator_.reuse();
  }

  return ret;
}

int ObIKFTParser::find_next_segment(int64_t &segment_len) const
{
  int ret = OB_SUCCESS;
  segment_len = 0;
  int64_t pos = next_segment_offset_;
  while (OB_SUCC(ret) && pos < document_fulltext_len_ && 0 == segment_len) {
    int64_t char_len = 0;
    ObFTCharUtil::CharType type = ObFTCharUtil::CharType::USELESS;
    if (OB_FAIL(ObFTCharUtil::get_first_valid_char_and_type(coll_type_,
                                                            document_fulltext_ + pos,
                                                            document_fulltext_len_ - pos,
                                                            char_len,
                                                            type))) {
      LOG_WARN("failed to scan ik segment boundary", K(ret), K(pos));
    } else {
      const char *ch = document_fulltext_ + pos;
      const bool ascii_space = 1 == char_len
                               && (' ' == ch[0] || '\n' == ch[0] || '\r' == ch[0]
                                   || '\t' == ch[0]);
      const bool cjk_sentence_mark = 3 == char_len
                                     && (0 == MEMCMP(ch, "。", 3)
                                         || 0 == MEMCMP(ch, "！", 3)
                                         || 0 == MEMCMP(ch, "？", 3)
                                         || 0 == MEMCMP(ch, "；", 3)
                                         || 0 == MEMCMP(ch, "，", 3));
      pos += char_len;
      if (ObFTCharUtil::CharType::USELESS == type
          && (ascii_space || cjk_sentence_mark)) {
        segment_len = pos - next_segment_offset_;
      }
    }
  }
  if (OB_SUCC(ret) && 0 == segment_len) {
    segment_len = document_fulltext_len_ - next_segment_offset_;
  }
  return ret;
}

int ObIKFTParser::lookup_segment_cache(const char *text,
                                       const int64_t text_len,
                                       CachedSegment *&entry) const
{
  int ret = OB_ENTRY_NOT_EXIST;
  entry = nullptr;
  const uint64_t hash = murmurhash(text, text_len, 0);
  for (int64_t i = 0; OB_ENTRY_NOT_EXIST == ret && i < segment_cache_.count(); ++i) {
    CachedSegment *candidate = segment_cache_.at(i);
    if (OB_NOT_NULL(candidate) && candidate->hash_ == hash
        && candidate->coll_type_ == coll_type_
        && candidate->is_smart_ == document_is_smart_
        && candidate->text_.length() == text_len
        && 0 == MEMCMP(candidate->text_.ptr(), text, text_len)) {
      entry = candidate;
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

int ObIKFTParser::replay_segment(const CachedSegment &entry)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < entry.tokens_.count(); ++i) {
    const CachedToken &cached = entry.tokens_.at(i);
    ObIKToken token;
    token.ptr_ = document_fulltext_ + current_segment_offset_;
    token.offset_ = cached.relative_offset_;
    token.length_ = cached.length_;
    token.char_cnt_ = cached.char_cnt_;
    token.type_ = cached.type_;
    if (OB_FAIL(ctx_->result_list().push_back(token))) {
      LOG_WARN("failed to replay cached ik token", K(ret), K(i));
    }
  }
  return ret;
}

void ObIKFTParser::reset_segment_state()
{
  if (OB_NOT_NULL(ctx_)) {
    ctx_->~TokenizeContext();
    scratch_allocator_.free(ctx_);
    ctx_ = nullptr;
  }
  for (ObIIKProcessor *segmenter : segmenters_) {
    if (OB_NOT_NULL(segmenter)) {
      segmenter->reset_document();
    }
  }
  const int ret = arbitrator_.reuse();
  if (OB_SUCCESS != ret) {
    LOG_WARN("failed to reuse arbitrator between ik segments", K(ret));
  }
  scratch_allocator_.reuse();
}

int ObIKFTParser::init_next_segment()
{
  int ret = OB_SUCCESS;
  reset_segment_state();
  current_segment_cache_hit_ = false;
  if (next_segment_offset_ >= document_fulltext_len_) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(find_next_segment(current_segment_len_))) {
    LOG_WARN("failed to find next ik segment", K(ret));
  } else {
    current_segment_offset_ = next_segment_offset_;
    next_segment_offset_ += current_segment_len_;
    if (OB_ISNULL(ctx_ = OB_NEWx(TokenizeContext,
                                 &scratch_allocator_,
                                 coll_type_,
                                 scratch_allocator_,
                                 document_fulltext_ + current_segment_offset_,
                                 current_segment_len_,
                                 document_is_smart_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate ik segment context", K(ret));
    } else if (OB_FAIL(ctx_->init())) {
      LOG_WARN("failed to initialize ik segment context", K(ret));
    } else if (use_segment_cache_) {
      CachedSegment *entry = nullptr;
      const int cache_ret = lookup_segment_cache(document_fulltext_ + current_segment_offset_,
                                                 current_segment_len_,
                                                 entry);
      if (OB_SUCCESS == cache_ret) {
        current_segment_cache_hit_ = true;
        ++segment_cache_hit_;
        if (OB_FAIL(replay_segment(*entry))) {
          LOG_WARN("failed to replay ik segment cache", K(ret));
        }
      } else if (OB_ENTRY_NOT_EXIST == cache_ret) {
        ++segment_cache_miss_;
      } else {
        ret = cache_ret;
        LOG_WARN("failed to lookup ik segment cache", K(ret));
      }
      const int64_t total = segment_cache_hit_ + segment_cache_miss_;
      if (OB_SUCC(ret) && total > 0 && 0 == total % 1000) {
        LOG_INFO("ik segment cache statistics",
                 "ik_segment_cache_hit", segment_cache_hit_,
                 "ik_segment_cache_miss", segment_cache_miss_);
      }
    }
  }
  return ret;
}

int ObIKFTParser::save_current_segment()
{
  int ret = OB_SUCCESS;
  const int64_t token_bytes = ctx_->result_list().count() * sizeof(CachedToken);
  const int64_t entry_bytes = sizeof(CachedSegment) + current_segment_len_ + token_bytes;
  if (current_segment_cache_hit_) {
  } else {
    if (segment_cache_.count() >= MAX_SEGMENT_CACHE_ENTRIES
        || segment_cache_bytes_ + entry_bytes > MAX_SEGMENT_CACHE_BYTES) {
      clear_segment_cache();
    }
    CachedSegment *entry = nullptr;
    char *text = nullptr;
    if (OB_ISNULL(entry = OB_NEWx(CachedSegment, &segment_cache_allocator_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_ISNULL(text = static_cast<char *>(
                             segment_cache_allocator_.alloc(current_segment_len_)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(text, document_fulltext_ + current_segment_offset_, current_segment_len_);
      entry->hash_ = murmurhash(text, current_segment_len_, 0);
      entry->coll_type_ = coll_type_;
      entry->is_smart_ = document_is_smart_;
      entry->text_.assign_ptr(text, current_segment_len_);
      for (int64_t i = 0; OB_SUCC(ret) && i < ctx_->result_list().count(); ++i) {
        const ObIKToken &token = ctx_->result_list().at(i);
        CachedToken cached;
        cached.relative_offset_ = token.offset_;
        cached.length_ = token.length_;
        cached.char_cnt_ = token.char_cnt_;
        cached.type_ = token.type_;
        cached.word_freq_ = 1;
        if (OB_FAIL(entry->tokens_.push_back(cached))) {
          LOG_WARN("failed to cache ik segment token", K(ret), K(i));
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(segment_cache_.push_back(entry))) {
        LOG_WARN("failed to append ik segment cache entry", K(ret));
      } else if (OB_SUCC(ret)) {
        segment_cache_bytes_ += entry_bytes;
      }
    }
    if (OB_FAIL(ret)) {
      if (OB_NOT_NULL(entry)) {
        entry->~CachedSegment();
      }
      clear_segment_cache();
    }
  }
  return ret;
}

void ObIKFTParser::clear_segment_cache()
{
  for (int64_t i = 0; i < segment_cache_.count(); ++i) {
    CachedSegment *entry = segment_cache_.at(i);
    if (OB_NOT_NULL(entry)) {
      entry->~CachedSegment();
    }
  }
  segment_cache_.reuse();
  segment_cache_allocator_.reuse();
  segment_cache_bytes_ = 0;
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
    LOG_WARN("Failed to alloc letter segmenter", K(ret));
  } else if (OB_ISNULL(dict_quan_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Dict quan is null.", K(ret));
  } else if (OB_ISNULL(cnqsg = OB_NEWx(ObIKQuantifierProcessor, &allocator_, *dict_quan_, allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc cn quantifier segmenter", K(ret));
  } else if (OB_ISNULL(dict_main_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Dict main is null.", K(ret));
  } else if (OB_ISNULL(cjksg = OB_NEWx(ObIKCJKProcessor, &allocator_, *dict_main_, allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc cjk segmenter", K(ret));
  } else if (OB_ISNULL(surrogate_seg = OB_NEWx(ObIKSurrogateProcessor, &allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc surrogate segmenter", K(ret));
  } else if (OB_FAIL(segmenters_.push_back(letter_seg))) {
    LOG_WARN("Failed to push back letter segmenter", K(ret));
  } else if (FALSE_IT(letter_processor_ = letter_seg)) {
  } else if (FALSE_IT(letter_seg = nullptr)) {
  } else if (OB_FAIL(segmenters_.push_back(cnqsg))) {
    LOG_WARN("Failed to push back cn quantifier segmenter", K(ret));
  } else if (FALSE_IT(quantifier_processor_ = cnqsg)) {
  } else if (FALSE_IT(cnqsg = nullptr)) {
  } else if (OB_FAIL(segmenters_.push_back(cjksg))) {
    LOG_WARN("Failed to push back cjk segmenter", K(ret));
  } else if (FALSE_IT(cjk_processor_ = cjksg)) {
  } else if (FALSE_IT(cjksg = nullptr)) {
  } else if (OB_FAIL(segmenters_.push_back(surrogate_seg))) {
    LOG_WARN("Failed to push back surrogate segmenter");
  } else if (FALSE_IT(surrogate_processor_ = surrogate_seg)) {
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

void ObIKFTParser::reset_document_state()
{
  reset_segment_state();
  document_fulltext_ = nullptr;
  document_fulltext_len_ = 0;
  next_segment_offset_ = 0;
  current_segment_offset_ = 0;
  current_segment_len_ = 0;
  current_segment_cache_hit_ = false;
}

void ObIKFTParser::reset()
{
  reset_document_state();

  for (ObIIKProcessor *segmenter : segmenters_) {
    if (OB_NOT_NULL(segmenter)) {
      segmenter->~ObIIKProcessor();
      allocator_.free(segmenter);
    }
  }
  segmenters_.clear();
  letter_processor_ = nullptr;
  quantifier_processor_ = nullptr;
  cjk_processor_ = nullptr;
  surrogate_processor_ = nullptr;

  cache_main_.reset();
  cache_quan_.reset();
  cache_stop_.reset();

  if (OB_NOT_NULL(dict_main_)) {
    dict_main_->~ObIFTDict();
    allocator_.free(dict_main_);
    dict_main_ = nullptr;
  }
  if (OB_NOT_NULL(dict_quan_)) {
    dict_quan_->~ObIFTDict();
    allocator_.free(dict_quan_);
    dict_quan_ = nullptr;
  }
  if (OB_NOT_NULL(dict_stop_)) {
    dict_stop_->~ObIFTDict();
    allocator_.free(dict_stop_);
    dict_stop_ = nullptr;
  }

  scratch_allocator_.reset();
  clear_segment_cache();
  segment_cache_allocator_.reset();

  coll_type_ = ObCollationType::CS_TYPE_INVALID;
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
    LOG_WARN("Hub is null", K(ret));
  } else if (OB_ISNULL(dict = OB_NEWx(ObFTRangeDict, &allocator_, allocator_, &container, desc))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc dict", K(ret));
  } else if (OB_FAIL(dict->init())) {
    LOG_WARN("Failed to init dict", K(ret));
  }
  if (OB_FAIL(ret)) {
    OB_DELETEx(ObIFTDict, &allocator_, dict);
  }
  return ret;
}

} //  namespace storage
} //  namespace oceanbase
