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

#include "common/mysqlclient/ob_isql_client.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "lib/allocator/page_arena.h"
#include "lib/charset/ob_charset.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_sql_string.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_smart_var.h"
#include "lib/utility/utility.h"
#include "share/ob_server_struct.h"
#include "storage/fts/ob_fts_literal.h"
#include "storage/fts/ob_fts_struct.h"
#include "storage/fts/ob_fts_plugin_helper.h"
#include "storage/fts/dict/ob_ft_cache_dict.h"
#include "storage/fts/dict/ob_ft_dat_dict.h"
#include "storage/fts/dict/ob_ft_dict.h"
#include "storage/fts/dict/ob_ft_dict_def.h"
#include "storage/fts/dict/ob_ft_dict_hub.h"
#include "storage/fts/dict/ob_ft_range_dict.h"
#include "storage/fts/dict/ob_ft_trie.h"
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
  // process by char with all segmenters
  for (int64_t i = 0; OB_SUCC(ret) && i < segmenter_cnt_; ++i) {
    if (OB_FAIL(segmenters_[i]->process(ctx))) {
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
      if (OB_FAIL(ctx_->current_char_and_type(ch, char_len, type))) {
        LOG_WARN("Failed to get current char and type", K(ret));
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
      ObIKArbitrator arb;
      if (OB_FAIL(arb.process(*ctx_))) {
        LOG_WARN("Failed to process arbitrator", K(ret));
      } else if (OB_FAIL(arb.output_result(*ctx_))) {
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

// A dictionary table configured in the parser properties replaces the
// corresponding built-in dictionary; the built-in one is used when the
// property is absent or still points to the built-in table.
static bool is_custom_dict_table(const ObString &name, const char *builtin_name)
{
  return !name.empty() && 0 != name.case_compare(builtin_name);
}

int ObIKFTParser::init_dict(const plugin::ObFTParserParam &param)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(hub_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Dict hub is not inited", K(ret));
  }

  const bool custom_main = is_custom_dict_table(param.ik_param_.main_dict_,
                                                ObFTSLiteral::FT_DEFAULT_IK_DICT_UTF8_TABLE);
  const bool custom_quan = is_custom_dict_table(param.ik_param_.quan_dict_,
                                                ObFTSLiteral::FT_DEFAULT_IK_QUANTIFIER_UTF8_TABLE);
  const bool custom_stop = is_custom_dict_table(param.ik_param_.stopword_dict_,
                                                ObFTSLiteral::FT_DEFAULT_IK_STOPWORD_UTF8_TABLE);

  // When no custom dictionary is configured (the common case), borrow the
  // process-wide shared built-in dictionaries instead of rebuilding a private
  // copy for every tokenization. Only custom-table dictionaries are built and
  // owned locally.
  ObIFTDict *shared_main = nullptr;
  ObIFTDict *shared_quan = nullptr;
  ObIFTDict *shared_stop = nullptr;
  if (OB_FAIL(ret)) {
    // already logged.
  } else if ((!custom_main || !custom_quan || !custom_stop)
             && OB_FAIL(ObFTParsePluginData::instance().get_builtin_ik_dicts(
                    shared_main, shared_quan, shared_stop))) {
    LOG_WARN("Failed to get shared builtin ik dicts", K(ret));
  }

  if (OB_SUCC(ret)) {
    if (custom_main) {
      own_dict_main_ = true;
      if (OB_FAIL(build_table_dict(param.ik_param_.main_dict_, dict_main_))) {
        LOG_WARN("Failed to init main dict", K(ret), K(param.ik_param_.main_dict_));
      }
    } else {
      dict_main_ = shared_main;
    }
  }
  if (OB_SUCC(ret)) {
    if (custom_quan) {
      own_dict_quan_ = true;
      if (OB_FAIL(build_table_dict(param.ik_param_.quan_dict_, dict_quan_))) {
        LOG_WARN("Failed to init quantifier dict", K(ret), K(param.ik_param_.quan_dict_));
      }
    } else {
      dict_quan_ = shared_quan;
    }
  }
  if (OB_SUCC(ret)) {
    if (custom_stop) {
      own_dict_stop_ = true;
      if (OB_FAIL(build_table_dict(param.ik_param_.stopword_dict_, dict_stop_))) {
        LOG_WARN("Failed to init stopword dict", K(ret), K(param.ik_param_.stopword_dict_));
      }
    } else {
      dict_stop_ = shared_stop;
    }
  }

  return ret;
}

int ObIKFTParser::init_builtin_dict(const ObFTDictDesc &desc,
                                    ObFTCacheRangeContainer &container,
                                    ObIFTDict *&dict)
{
  int ret = OB_SUCCESS;
  if (should_read_newest_table()) {
    // clear dict cache, always false now
  } else if (OB_FAIL(init_single_dict(desc, container))) {
    LOG_WARN("Failed to load builtin dict cache", K(ret));
  }
  if (OB_FAIL(ret)) {
    // already logged.
  } else if (OB_FAIL(build_dict_from_cache(desc, container, dict))) {
    LOG_WARN("Failed to build dict from cache", K(ret));
  }
  return ret;
}

int ObIKFTParser::build_table_dict(const common::ObString &table_name, ObIFTDict *&dict)
{
  int ret = OB_SUCCESS;
  dict = nullptr;

  ObString tbl_name = table_name;
  const ObString db_name = tbl_name.split_on('.');

  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", K(ret));
  } else if (OB_UNLIKELY(db_name.empty() || tbl_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dict table should be written as database_name.table_name",
             K(ret), K(table_name));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "dict table name, should be database_name.table_name");
  } else if (OB_UNLIKELY(nullptr != memchr(db_name.ptr(), '`', db_name.length())
                         || nullptr != memchr(tbl_name.ptr(), '`', tbl_name.length()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid character in dict table name", K(ret), K(table_name));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "dict table name");
  } else {
    int64_t word_cnt = 0;
    ObArenaAllocator tmp_alloc(lib::ObMemAttr("IKUsrDict"));
    ObFTTrie<void> trie(tmp_alloc, ObCollationType::CS_TYPE_UTF8MB4_BIN);
    SMART_VAR(ObISQLClient::ReadResult, res)
    {
      ObSqlString sql;
      common::sqlclient::ObMySQLResult *result = nullptr;
      // the IK tokenizer is case insensitive (all words are held lowercase),
      // and the trie requires its input sorted in utf8mb4_bin order.
      if (OB_FAIL(sql.assign_fmt("SELECT LOWER(word) AS word FROM `%.*s`.`%.*s` "
                                 "ORDER BY LOWER(word) COLLATE utf8mb4_bin",
                                 db_name.length(),
                                 db_name.ptr(),
                                 tbl_name.length(),
                                 tbl_name.ptr()))) {
        LOG_WARN("Failed to build dict table query", K(ret));
      } else if (OB_FAIL(GCTX.sql_proxy_->read(res, sql.ptr()))) {
        LOG_WARN("Failed to read custom dict table", K(ret), K(sql));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to get result of dict table query", K(ret));
      } else {
        while (OB_SUCC(ret) && OB_SUCC(result->next())) {
          ObString word;
          if (OB_FAIL(result->get_varchar("word", word))) {
            LOG_WARN("Failed to get word from dict table", K(ret));
          } else if (word.empty()) {
            // skip empty word
          } else if (OB_FAIL(trie.insert(word, {}))) {
            LOG_WARN("Failed to insert word into trie", K(ret), K(word));
          } else {
            ++word_cnt;
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
      }
    }

    if (OB_FAIL(ret)) {
      // already logged.
    } else if (0 == word_cnt) {
      if (OB_ISNULL(dict = OB_NEWx(ObFTEmptyDict, &allocator_))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("Failed to alloc empty dict", K(ret));
      }
    } else {
      ObFTDATBuilder<void> builder(tmp_alloc);
      ObFTDAT *dat_buff = nullptr;
      size_t buff_size = 0;
      void *block = nullptr;
      if (OB_UNLIKELY(table_dict_block_cnt_ >= MAX_TABLE_DICT_BLOCK)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("too many custom dicts", K(ret), K(table_dict_block_cnt_));
      } else if (OB_FAIL(builder.init(trie))) {
        LOG_WARN("Failed to init DAT builder", K(ret));
      } else if (OB_FAIL(builder.build_from_trie(trie))) {
        LOG_WARN("Failed to build DAT from trie", K(ret));
      } else if (OB_FAIL(builder.get_mem_block(dat_buff, buff_size))) {
        LOG_WARN("Failed to get DAT mem block", K(ret));
      } else if (OB_ISNULL(dat_buff) || OB_UNLIKELY(buff_size <= 0)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected empty DAT block", K(ret), KP(dat_buff), K(buff_size));
      } else if (OB_ISNULL(block = allocator_.alloc(buff_size))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("Failed to alloc DAT block", K(ret), K(buff_size));
      } else {
        MEMCPY(block, dat_buff, buff_size);
        if (OB_ISNULL(dict = OB_NEWx(ObFTCacheDict,
                                     &allocator_,
                                     ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                     reinterpret_cast<ObFTDAT *>(block)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("Failed to alloc dict", K(ret));
          allocator_.free(block);
        } else {
          table_dict_blocks_[table_dict_block_cnt_++] = block;
        }
      }
    }
    LOG_TRACE("build custom dict from table", K(ret), K(table_name), K(word_cnt));
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
  } else {
    // fill by order, quantifier is before cjk
    segmenter_cnt_ = 0;
    segmenters_[segmenter_cnt_++] = letter_seg;
    segmenters_[segmenter_cnt_++] = cnqsg;
    segmenters_[segmenter_cnt_++] = cjksg;
    segmenters_[segmenter_cnt_++] = surrogate_seg;
  }

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
    ctx_ = nullptr;
  }

  for (int64_t i = 0; i < segmenter_cnt_; ++i) {
    if (!OB_ISNULL(segmenters_[i])) {
      segmenters_[i]->~ObIIKProcessor();
      allocator_.free(segmenters_[i]);
      segmenters_[i] = nullptr;
    }
  }
  segmenter_cnt_ = 0;

  cache_main_.reset();
  cache_quan_.reset();
  cache_stop_.reset();

  // Only free dictionaries this parser owns (custom-table dictionaries). The
  // built-in dictionaries are shared process-wide and must not be freed here.
  if (own_dict_main_ && !OB_ISNULL(dict_main_)) {
    dict_main_->~ObIFTDict();
    allocator_.free(dict_main_);
  }
  dict_main_ = nullptr;
  own_dict_main_ = false;
  if (own_dict_quan_ && !OB_ISNULL(dict_quan_)) {
    dict_quan_->~ObIFTDict();
    allocator_.free(dict_quan_);
  }
  dict_quan_ = nullptr;
  own_dict_quan_ = false;
  if (own_dict_stop_ && !OB_ISNULL(dict_stop_)) {
    dict_stop_->~ObIFTDict();
    allocator_.free(dict_stop_);
  }
  dict_stop_ = nullptr;
  own_dict_stop_ = false;

  for (int64_t i = 0; i < table_dict_block_cnt_; ++i) {
    if (!OB_ISNULL(table_dict_blocks_[i])) {
      allocator_.free(table_dict_blocks_[i]);
      table_dict_blocks_[i] = nullptr;
    }
  }
  table_dict_block_cnt_ = 0;

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
