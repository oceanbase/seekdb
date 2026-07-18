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
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "share/ob_server_struct.h"
#include "storage/fts/dict/ob_ft_cache_dict.h"
#include "storage/fts/dict/ob_ft_dat_dict.h"
#include "storage/fts/dict/ob_ft_trie.h"
#include "storage/fts/ob_fts_literal.h"
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

// Thread-local parser cache: each thread gets its own parser + allocator,
// avoiding contention and races in multi-threaded build paths.
namespace {
thread_local common::ObArenaAllocator *tl_parser_alloc = nullptr;
thread_local ObIKFTParser *tl_cached_parser = nullptr;
}

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

int ObIKFTParser::reuse(const ObFTParserParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("Parser not inited for reuse", K(ret));
  } else {
    // Reuse ctx and processors in-place: update text pointer, reset state.
    // This avoids 5 heap allocations (ctx + 4 processors) per call.
    coll_type_ = ObCharset::collation_type(param.cs_->name);
    bool is_smart = (param.ik_param_.mode_ == ObFTIKParam::Mode::SMART);
    if (OB_FAIL(ctx_->set_text(param.fulltext_, param.ft_length_, coll_type_, is_smart))) {
      LOG_WARN("Failed to set text for reuse", K(ret));
    }
    for (ObIIKProcessor *segmenter : segmenters_) {
      if (OB_SUCC(ret) && segmenter != nullptr) {
        segmenter->reset_state();
      }
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

int ObIKFTParser::produce()
{
  int ret = OB_SUCCESS;
  // Loop until end or has data to output
  while (OB_SUCC(ret) && ctx_->result_list().empty() && !ctx_->iter_end()) {
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
        LOG_WARN("Failed to process arbitrator", K(ret));
      } else if (OB_FAIL(arbitrator_.output_result(*ctx_))) {
        LOG_WARN("Failed to make result list");
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
  // thread_local caches are cleaned up when threads exit; no global cleanup needed.
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
  } else {
    bool use_custom_dict = ObIKFTParser::is_custom_dict_table(param->ik_param_.main_dict_)
                        || ObIKFTParser::is_custom_dict_table(param->ik_param_.quan_dict_)
                        || ObIKFTParser::is_custom_dict_table(param->ik_param_.stopword_dict_);
    if (!use_custom_dict) {
      if (tl_parser_alloc == nullptr) {
        void *buf = ob_malloc(sizeof(ObArenaAllocator), "IKTLAlloc");
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc thread-local parser allocator", K(ret));
        } else {
          tl_parser_alloc = new (buf) ObArenaAllocator(ObMemAttr("IKTLAlloc"));
        }
      }
      if (OB_SUCC(ret) && tl_cached_parser != nullptr) {
        parser = tl_cached_parser;
        tl_cached_parser = nullptr;
        if (OB_FAIL(parser->reuse(*param))) {
          LOG_WARN("fail to reuse cached ik parser", K(ret), KPC(param));
          parser->reset();
          OB_DELETEx(ObIKFTParser, tl_parser_alloc, parser);
          parser = nullptr;
        }
      }
    }
    if (OB_SUCC(ret) && parser == nullptr) {
      ObIAllocator &alloc = use_custom_dict ? *(param->allocator_) : *tl_parser_alloc;
      if (OB_ISNULL(parser = OB_NEWx(ObIKFTParser, &alloc, alloc, hub))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to allocate ik ft parser", K(ret));
      } else if (OB_FAIL(parser->init(*param))) {
        LOG_WARN("fail to init ik parser", K(ret), KPC(param));
      }
      if (OB_FAIL(ret) && parser != nullptr) {
        OB_DELETEx(ObIKFTParser, &alloc, parser);
        parser = nullptr;
      }
    }
    if (OB_SUCC(ret)) {
      iter = parser;
    }
  }
  return ret;
}

void ObIKFTParserDesc::free_token_iter(ObFTParserParam *param,
                                       ObITokenIterator *&iter) const
{
  if (OB_ISNULL(iter)) {
    return;
  }
  ObIKFTParser *parser = static_cast<ObIKFTParser *>(iter);
  if (parser->owns_dicts_) {
    iter->~ObITokenIterator();
    param->allocator_->free(iter);
  } else {
    parser->reset_for_reuse();
    if (tl_cached_parser == nullptr && tl_parser_alloc != nullptr) {
      tl_cached_parser = parser;
    } else if (tl_parser_alloc != nullptr) {
      parser->~ObIKFTParser();
      tl_parser_alloc->free(parser);
    }
  }
  iter = nullptr;
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
    if (is_custom_dict_table(param.ik_param_.main_dict_)) {
      if (OB_FAIL(build_custom_dict_from_table(param.ik_param_.main_dict_, dict_main_))) {
        LOG_WARN("Failed to build custom main dict from table", K(ret), K(param.ik_param_.main_dict_));
      }
    } else if (OB_FAIL(hub_->get_cached_builtin_dict(main_dict_desc, dict_main_))) {
      LOG_WARN("Failed to get cached main dict", K(ret));
    }
    if (OB_SUCC(ret)) {
      if (is_custom_dict_table(param.ik_param_.quan_dict_)) {
        if (OB_FAIL(build_custom_dict_from_table(param.ik_param_.quan_dict_, dict_quan_))) {
          LOG_WARN("Failed to build custom quan dict from table", K(ret), K(param.ik_param_.quan_dict_));
        }
      } else if (OB_FAIL(hub_->get_cached_builtin_dict(quan_dict_desc, dict_quan_))) {
        LOG_WARN("Failed to get cached quan dict", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (is_custom_dict_table(param.ik_param_.stopword_dict_)) {
        if (OB_FAIL(build_custom_dict_from_table(param.ik_param_.stopword_dict_, dict_stop_))) {
          LOG_WARN("Failed to build custom stopword dict from table", K(ret), K(param.ik_param_.stopword_dict_));
        }
      } else if (OB_FAIL(hub_->get_cached_builtin_dict(stopword_dict_desc, dict_stop_))) {
        LOG_WARN("Failed to get cached stopword dict", K(ret));
      }
    }
  }

  if (OB_SUCC(ret)) {
    owns_dicts_ = is_custom_dict_table(param.ik_param_.main_dict_)
               || is_custom_dict_table(param.ik_param_.quan_dict_)
               || is_custom_dict_table(param.ik_param_.stopword_dict_);
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
    LOG_WARN("Illegal collation type", K(ret));
  } else if (OB_ISNULL(ctx_ = OB_NEWx(TokenizeContext,
                                      &allocator_,
                                      coll_type_,
                                      allocator_,
                                      param.fulltext_,
                                      param.ft_length_,
                                      param.ik_param_.mode_ == ObFTIKParam::Mode::SMART))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc ctx", K(ret));
  } else if (OB_FAIL(ctx_->init())) {
    LOG_WARN("Failed to init ctx", K(ret));
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

  if (!OB_ISNULL(dict_main_)) {
    if (owns_dicts_) {
      dict_main_->~ObIFTDict();
      allocator_.free(dict_main_);
    }
    dict_main_ = nullptr;
  }
  if (!OB_ISNULL(dict_quan_)) {
    if (owns_dicts_) {
      dict_quan_->~ObIFTDict();
      allocator_.free(dict_quan_);
    }
    dict_quan_ = nullptr;
  }
  if (!OB_ISNULL(dict_stop_)) {
    if (owns_dicts_) {
      dict_stop_->~ObIFTDict();
      allocator_.free(dict_stop_);
    }
    dict_stop_ = nullptr;
  }

  owns_dicts_ = false;
  is_inited_ = false;
}

bool ObIKFTParser::should_read_newest_table() const { return false; }

bool ObIKFTParser::is_custom_dict_table(const common::ObString &table_name)
{
  bool is_custom = false;
  if (!table_name.empty()) {
    // The built-in defaults point at the oceanbase.__ft_* system tables; any
    // other non-empty name is a user-owned custom dictionary table.
    if (0 == table_name.case_compare(ObString(ObFTSLiteral::FT_DEFAULT_IK_DICT_UTF8_TABLE))
        || 0 == table_name.case_compare(ObString(ObFTSLiteral::FT_DEFAULT_IK_STOPWORD_UTF8_TABLE))
        || 0 == table_name.case_compare(ObString(ObFTSLiteral::FT_DEFAULT_IK_QUANTIFIER_UTF8_TABLE))) {
      is_custom = false;
    } else {
      is_custom = true;
    }
  }
  return is_custom;
}

int ObIKFTParser::build_custom_dict_from_table(const common::ObString &table_name,
                                               ObIFTDict *&dict)
{
  int ret = OB_SUCCESS;
  dict = nullptr;
  if (OB_UNLIKELY(table_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("custom dict table name is empty", K(ret));
  } else if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", K(ret));
  } else {
    SMART_VAR(ObISQLClient::ReadResult, result)
    {
      ObSqlString sql;
      if (OB_FAIL(sql.append_fmt("SELECT word FROM %.*s ORDER BY word",
                                 table_name.length(), table_name.ptr()))) {
        LOG_WARN("fail to format sql", K(ret), K(table_name));
      } else if (OB_FAIL(GCTX.sql_proxy_->read(result, sql.ptr()))) {
        LOG_WARN("fail to read custom dict table", K(ret), K(sql));
      } else if (OB_ISNULL(result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("custom dict read result is null", K(ret));
      } else {
        // Build a trie from the words, then a DAT, then wrap as ObFTCacheDict.
        ObFTTrie<void> trie(allocator_, ObCollationType::CS_TYPE_UTF8MB4_BIN);
        int64_t word_cnt = 0;
        while (OB_SUCC(ret)) {
          if (OB_FAIL(result.get_result()->next())) {
            if (OB_ITER_END != ret) {
              LOG_WARN("fail to iterate dict rows", K(ret));
            }
          } else {
            ObString word;
            if (OB_FAIL(result.get_result()->get_varchar("word", word))) {
              LOG_WARN("fail to get word column", K(ret));
            } else if (word.empty()) {
              // skip empty words
            } else if (OB_FAIL(trie.insert(word, {}))) {
              LOG_WARN("fail to insert word into trie", K(ret), K(word));
            } else {
              ++word_cnt;
            }
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
        if (OB_FAIL(ret)) {
          // already logged
        } else if (0 == word_cnt) {
          // Empty custom dict: leave dict null so init_dict builds an empty
          // range dict (matches nothing). Avoids builder crash on empty trie.
          LOG_INFO("custom ik dict table is empty", K(table_name));
        } else {
          ObFTDATBuilder<void> builder(allocator_);
          ObFTDAT *dat_buff = nullptr;
          size_t buffer_size = 0;
          if (OB_FAIL(builder.init(trie))) {
            LOG_WARN("fail to init dat builder", K(ret));
          } else if (OB_FAIL(builder.build_from_trie(trie))) {
            LOG_WARN("fail to build dat from trie", K(ret));
          } else if (OB_FAIL(builder.get_mem_block(dat_buff, buffer_size))) {
            LOG_WARN("fail to get dat mem block", K(ret));
          } else if (OB_ISNULL(dat_buff)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("dat mem block is null", K(ret));
          } else if (OB_ISNULL(dict = OB_NEWx(ObFTCacheDict, &allocator_,
                                              ObCollationType::CS_TYPE_UTF8MB4_BIN, dat_buff))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("fail to alloc ObFTCacheDict", K(ret));
          } else if (OB_FAIL(dict->init())) {
            LOG_WARN("fail to init cache dict", K(ret));
          } else {
            LOG_INFO("succeed to build custom ik dict from table", K(table_name), K(word_cnt));
          }
          if (OB_FAIL(ret) && OB_NOT_NULL(dict)) {
            OB_DELETEx(ObIFTDict, &allocator_, dict);
          }
        }
      }
    }
  }
  return ret;
}

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
