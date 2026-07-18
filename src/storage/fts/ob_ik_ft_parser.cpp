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
#include "storage/fts/ob_fts_struct.h"
#include "storage/fts/ob_fts_plugin_helper.h"
#include "storage/fts/ob_fts_parser_property.h"
#include "storage/fts/dict/ob_ft_dict.h"
#include "storage/fts/dict/ob_ft_dict_def.h"
#include "storage/fts/dict/ob_ft_dict_hub.h"
#include "storage/fts/dict/ob_ft_range_dict.h"
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
    } else if (OB_FAIL(arb_.prepare())) {
      LOG_WARN("Failed to prepare arbitrator", K(ret));
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
    LOG_WARN("Parser has not been inited", K(ret));
  } else {
    bool accept_token = false;
    while (OB_SUCC(ret) && !accept_token) {
      if (OB_FAIL(produce())) {
        LOG_WARN("Failed to produce new token", K(ret));
      } else if (OB_FAIL(ctx_->get_next_token(output_word, len, offset, cnt))) {
        if (OB_ITER_END == ret) {
          // ok, end this iter
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

int ObIKFTParser::reuse_parser(const char *fulltext, const int64_t fulltext_len)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("Ik ft parser has not been inited", K(ret));
  } else if (OB_UNLIKELY(nullptr == fulltext || 0 >= fulltext_len)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("There are invalid fulltext", K(ret), KP(fulltext), K(fulltext_len));
  } else {
    if (OB_FAIL(ctx_->reuse_context(fulltext, fulltext_len))) {
      LOG_WARN("Failed to reuse context", K(ret));
    }
    for (ObList<ObIIKProcessor *, ObIAllocator>::iterator iter = segmenters_.begin();
        OB_SUCC(ret) && iter != segmenters_.end();
        iter++) {
      (*iter)->reuse();
    }
    scratch_alloc_.reset_remain_one_page();
  }
  return ret;
}

int ObIKFTParser::produce()
{
  int ret = OB_SUCCESS;
  // Loop until end or has data to output
  while (OB_SUCC(ret) && ctx_->is_results_exhaust() && !ctx_->iter_end()) {
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
  // proces by char with all segmenters
  for (ObList<ObIIKProcessor *, ObIAllocator>::iterator iter = segmenters_.begin();
       OB_SUCC(ret) && iter != segmenters_.end();
       iter++) {
    if (OB_FAIL((*iter)->process(ctx))) {
      LOG_WARN("Failed to process segmenter", K(ret));
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
    ctx_->calc_buffer_start_cursor();
    while (OB_SUCC(ret) && !do_seg && !ctx_->iter_end()) {
      const char *ch;
      uint8_t char_len = 0;
      ObFTCharUtil::CharType type = ObFTCharUtil::CharType::USELESS;
      if (OB_FAIL(ctx_->current_char_and_type(ch, char_len, type))) {
        if (OB_LIKELY(OB_ITER_END == ret)) {
        } else {
          LOG_WARN("Failed to get current char and type", K(ret));
        }
      } else if (OB_FAIL(process_one_char(*ctx_, ch, char_len, type))) {
        LOG_WARN("Failed to process one char", K(ret));
      } else {
        // 1. check segmention
        if (ctx_->handle_size() >= HANDLE_SIZE_LIMIT && type == ObFTCharUtil::CharType::USELESS) {
          do_seg = true;
        }

        // 2. move to next;
        if (OB_FAIL(ctx_->step_next())) {
          if (OB_LIKELY(OB_ITER_END == ret)) {
          } else {
            LOG_WARN("Failed to step next", K(ret));
          }
        }
      } // end of one batch
    }

    if (OB_SUCC(ret) || OB_ITER_END == ret) {
      if (OB_FAIL(arb_.process(*ctx_))) {
        LOG_WARN("Failed to process arbitrator", K(ret));
      } else if (OB_FAIL(arb_.output_result(*ctx_))) {
        LOG_WARN("Failed to make result list");
      } else {
        arb_.reuse();
      }
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
  } else if (OB_ISNULL(param) || OB_ISNULL(param->metadata_alloc_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("there are invalid arguments", K(ret), KPC(param));
  } else if (OB_FAIL(ObFTParsePluginData::instance().get_dict_hub(hub))) {
    LOG_WARN("Failed to get dict hub.", K(ret));
  } else if (OB_ISNULL(parser = OB_NEWx(ObIKFTParser,
                                        param->metadata_alloc_,
                                        *(param->metadata_alloc_),
                                        hub))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate ik ft parser", K(ret));
  } else if (OB_FAIL(parser->init(*param))) {
    LOG_WARN("fail to init ik parser", K(ret), KPC(param));
  } else {
    iter = parser;
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(param) && OB_NOT_NULL(param->metadata_alloc_)) {
    OB_DELETEx(ObIKFTParser, param->metadata_alloc_, parser);
  }

  return ret;
}

void ObIKFTParserDesc::free_token_iter(ObFTParserParam *param,
                                       ObITokenIterator *&iter) const
{
  if (OB_NOT_NULL(iter)) {
    abort_unless(nullptr != param);
    abort_unless(nullptr != param->metadata_alloc_);
    iter->~ObITokenIterator();
    param->metadata_alloc_->free(iter);
  }
}


int ObIKFTParserDesc::get_add_word_flag(ObProcessTokenFlag &flag) const
{
  int ret = OB_SUCCESS;
  flag.set_casedown_token();
  flag.set_groupby_token();
  return ret;
}

int ObIKFTParser::init_dict(const plugin::ObFTParserParam &param)
{
  int ret = OB_SUCCESS;
  ObIFTDict *tmp_dict = nullptr;
  ObFTParserProperty property;

  if (OB_ISNULL(hub_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Dict hub is not inited", K(ret));
  }

  ObFTRangeDict *dict = nullptr;
  ObFTDictDesc main_dict_desc("", ObFTDictType::DICT_TYPE_INVALID,
                              ObCharsetType::CHARSET_INVALID, ObCollationType::CS_TYPE_INVALID);
  ObFTDictDesc quan_dict_desc("", ObFTDictType::DICT_TYPE_INVALID,
                              ObCharsetType::CHARSET_INVALID, ObCollationType::CS_TYPE_INVALID);
  ObFTDictDesc stopword_dict_desc("", ObFTDictType::DICT_TYPE_INVALID,
                                  ObCharsetType::CHARSET_INVALID, ObCollationType::CS_TYPE_INVALID);

  // 插件参数是 SQL 层到 IK 分词器的边界，在此恢复三类用户词典配置。
  property.dict_table_ = param.ik_param_.main_dict_;
  property.quantifier_table_ = param.ik_param_.quan_dict_;
  property.stopword_table_ = param.ik_param_.stopword_dict_;
  // 恢复 SQL 层解析出的稳定 ID，避免 IK 初始化时丢失刷新代次的查询依据。
  property.dict_table_id_ = param.ik_param_.main_dict_table_id_;
  property.quantifier_table_id_ = param.ik_param_.quantifier_dict_table_id_;
  property.stopword_table_id_ = param.ik_param_.stopword_dict_table_id_;
  if (OB_SUCC(ret) && OB_FAIL(build_dict_descs_(property,
                                                main_dict_desc,
                                                quan_dict_desc,
                                                stopword_dict_desc))) {
    LOG_WARN("Failed to build dictionary descriptors", K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (should_read_newest_table()) {
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

int ObIKFTParser::build_dict_descs_(const ObFTParserProperty &property,
                                    ObFTDictDesc &main_dict_desc,
                                    ObFTDictDesc &quantifier_dict_desc,
                                    ObFTDictDesc &stopword_dict_desc)
{
  int ret = OB_SUCCESS;
  // 空属性使用内置词典；属性重建补齐的默认内置表名也必须保持内置语义。
  const bool main_is_builtin = property.dict_table_.empty()
      || (OB_INVALID_ID == property.dict_table_id_
          && 0 == property.dict_table_.case_compare(ObFTSLiteral::FT_DEFAULT_IK_DICT_UTF8_TABLE));
  const bool quantifier_is_builtin = property.quantifier_table_.empty()
      || (OB_INVALID_ID == property.quantifier_table_id_
          && 0 == property.quantifier_table_.case_compare(ObFTSLiteral::FT_DEFAULT_IK_QUANTIFIER_UTF8_TABLE));
  const bool stopword_is_builtin = property.stopword_table_.empty()
      || (OB_INVALID_ID == property.stopword_table_id_
          && 0 == property.stopword_table_.case_compare(ObFTSLiteral::FT_DEFAULT_IK_STOPWORD_UTF8_TABLE));
  int64_t main_version = 0;
  int64_t quantifier_version = 0;
  int64_t stopword_version = 0;
  // 只有新属性携带有效 table ID 时才查询刷新代次；旧属性继续按表名隔离缓存。
  if (!main_is_builtin && OB_INVALID_ID != property.dict_table_id_ && OB_NOT_NULL(hub_)
      && OB_FAIL(hub_->get_refresh_version(property.dict_table_id_, main_version))) {
    LOG_WARN("failed to get main dictionary refresh version", K(ret), K(property.dict_table_id_));
  } else if (OB_SUCC(ret) && !quantifier_is_builtin
             && OB_INVALID_ID != property.quantifier_table_id_ && OB_NOT_NULL(hub_)
             && OB_FAIL(hub_->get_refresh_version(property.quantifier_table_id_, quantifier_version))) {
    LOG_WARN("failed to get quantifier dictionary refresh version", K(ret), K(property.quantifier_table_id_));
  } else if (OB_SUCC(ret) && !stopword_is_builtin
             && OB_INVALID_ID != property.stopword_table_id_ && OB_NOT_NULL(hub_)
             && OB_FAIL(hub_->get_refresh_version(property.stopword_table_id_, stopword_version))) {
    LOG_WARN("failed to get stopword dictionary refresh version", K(ret), K(property.stopword_table_id_));
  }
  if (OB_FAIL(ret)) {
    return ret;
  }
  main_dict_desc = ObFTDictDesc(main_is_builtin ? ObString("main_dict") : property.dict_table_,
                                ObFTDictType::DICT_IK_MAIN,
                                ObCharsetType::CHARSET_UTF8MB4,
                                ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                0, OB_INVALID_ID == property.dict_table_id_ ? 0 : property.dict_table_id_,
                                main_version, main_is_builtin);
  quantifier_dict_desc = ObFTDictDesc(quantifier_is_builtin ? ObString("quan_dict")
                                                              : property.quantifier_table_,
                                      ObFTDictType::DICT_IK_QUAN,
                                      ObCharsetType::CHARSET_UTF8MB4,
                                      ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                      0, OB_INVALID_ID == property.quantifier_table_id_ ? 0
                                                                            : property.quantifier_table_id_,
                                      quantifier_version, quantifier_is_builtin);
  stopword_dict_desc = ObFTDictDesc(stopword_is_builtin ? ObString("stopword")
                                                          : property.stopword_table_,
                                    ObFTDictType::DICT_IK_STOP,
                                    ObCharsetType::CHARSET_UTF8MB4,
                                    ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                    0, OB_INVALID_ID == property.stopword_table_id_ ? 0
                                                                        : property.stopword_table_id_,
                                    stopword_version, stopword_is_builtin);
  return ret;
}

int ObIKFTParser::init_single_dict(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container)
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
    LOG_WARN("Illegal collation type", K(ret));
  } else if (OB_ISNULL(ctx_ = OB_NEWx(TokenizeContext,
                                      &metadata_alloc_,
                                      metadata_alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc ctx", K(ret));
  } else if (OB_FAIL(ctx_->init(coll_type_,
                                param.fulltext_,
                                param.ft_length_,
                                param.ik_param_.mode_ == ObFTIKParam::Mode::SMART))) {
    LOG_WARN("Failed to init ctx", K(ret));
  }
  if (OB_FAIL(ret)) {
    OB_DELETEx(TokenizeContext, &metadata_alloc_, ctx_);
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
  if (OB_ISNULL(letter_seg = OB_NEWx(ObIKLetterProcessor, &metadata_alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc letter segmenter", K(ret));
  } else if (OB_ISNULL(dict_quan_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Dict quan is null.", K(ret));
  } else if (OB_ISNULL(cnqsg = OB_NEWx(ObIKQuantifierProcessor, &metadata_alloc_, *dict_quan_, scratch_alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc cn quantifier segmenter", K(ret));
  } else if (OB_ISNULL(dict_main_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Dict main is null.", K(ret));
  } else if (OB_ISNULL(cjksg = OB_NEWx(ObIKCJKProcessor, &metadata_alloc_, *dict_main_, scratch_alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc cjk segmenter", K(ret));
  } else if (OB_ISNULL(surrogate_seg = OB_NEWx(ObIKSurrogateProcessor, &metadata_alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc surrogate segmenter", K(ret));
  } else if (OB_FAIL(segmenters_.push_back(letter_seg))) {
    LOG_WARN("Failed to push back letter segmenter", K(ret));
  } else if (FALSE_IT(letter_seg = nullptr)) {
  } else if (OB_FAIL(segmenters_.push_back(cnqsg))) {
    LOG_WARN("Failed to push back cn quantifier segmenter", K(ret));
  } else if (FALSE_IT(cnqsg = nullptr)) {
  } else if (OB_FAIL(segmenters_.push_back(cjksg))) {
    LOG_WARN("Failed to push back cjk segmenter", K(ret));
  } else if (FALSE_IT(cjksg = nullptr)) {
  } else if (OB_FAIL(segmenters_.push_back(surrogate_seg))) {
    LOG_WARN("Failed to push back surrogate segmenter");
  } else if (OB_FALSE_IT(surrogate_seg = nullptr)) {
  }
  // push back by order, quantifier is before cjk

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObIKLetterProcessor, &metadata_alloc_, letter_seg);
    OB_DELETEx(ObIKQuantifierProcessor, &metadata_alloc_, cnqsg);
    OB_DELETEx(ObIKCJKProcessor, &metadata_alloc_, cjksg);
    OB_DELETEx(ObIKSurrogateProcessor, &metadata_alloc_, surrogate_seg);
  }
  return ret;
}

void ObIKFTParser::reset()
{
  if (!OB_ISNULL(ctx_)) {
    ctx_->~TokenizeContext();
    metadata_alloc_.free(ctx_);
  }

  for (ObIIKProcessor *segmenter : segmenters_) {
    if (!OB_ISNULL(segmenter)) {
      segmenter->~ObIIKProcessor();
      metadata_alloc_.free(segmenter);
    }
  }
  segmenters_.clear();

  cache_main_.reset();
  cache_quan_.reset();
  cache_stop_.reset();

  if (!OB_ISNULL(dict_main_)) {
    dict_main_->~ObIFTDict();
    metadata_alloc_.free(dict_main_);
  }
  if (!OB_ISNULL(dict_quan_)) {
    dict_quan_->~ObIFTDict();
    metadata_alloc_.free(dict_quan_);
  }
  if (!OB_ISNULL(dict_stop_)) {
    dict_stop_->~ObIFTDict();
    metadata_alloc_.free(dict_stop_);
  }
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
  } else if (OB_ISNULL(dict = OB_NEWx(ObFTRangeDict, &metadata_alloc_, &container, desc))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc dict", K(ret));
  } else if (OB_FAIL(dict->init())) {
    LOG_WARN("Failed to init dict", K(ret));
  }
  if (OB_FAIL(ret)) {
    OB_DELETEx(ObIFTDict, &metadata_alloc_, dict);
  }
  return ret;
}

} //  namespace storage
} //  namespace oceanbase
