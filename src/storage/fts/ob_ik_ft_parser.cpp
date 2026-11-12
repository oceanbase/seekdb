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

#include "lib/allocator/ob_fifo_allocator.h"
#include "lib/charset/ob_charset.h"
#include "lib/lock/ob_lock_guard.h"
#include "lib/lock/ob_mutex.h"
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
#include "storage/fts/dict/ob_ft_cache_container.h"
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
/**
 * @brief 进程级共享的 IK 词典。
 *
 * IK 词典在匹配阶段是只读的，初始化完成后也不再依赖构建它时使用的缓存容器，
 * 因此整个进程只需要构建一次，所有 IK 解析器实例直接复用同一份词典。
 *
 * 背景：TOKENIZE 每次调用都会新建一个 IK 解析器，而解析器初始化时又会为
 * main / quantifier / stopword 三个词典分别重新加载缓存并重建 range 结构。
 * 对于 IK 主词典（按首字符范围切分的上百个 range），这部分开销远大于真正的
 * 分词计算，是分词热路径上的主要瓶颈。共享词典后，每次调用只剩下分词本身。
 *
 * 词典缓存被重建时（range 数量变化），共享词典会同步重建，避免使用过期内容。
 */
class ObIKFTSharedDict final
{
public:
  ObIKFTSharedDict()
      : alloc_(),
        main_container_(alloc_),
        quan_container_(alloc_),
        stop_container_(alloc_),
        main_dict_(nullptr),
        quan_dict_(nullptr),
        stop_dict_(nullptr),
        main_range_count_(-1),
        quan_range_count_(-1),
        stop_range_count_(-1)
  {
  }

  ~ObIKFTSharedDict() { destroy(); }

  int init()
  {
    int ret = OB_SUCCESS;
    lib::ObMemAttr mem_attr;
    mem_attr.label_ = "FtIKSharedDict";
    if (OB_FAIL(alloc_.init(lib::ObMallocAllocator::get_instance(),
                            OB_MALLOC_NORMAL_BLOCK_SIZE,
                            mem_attr))) {
    }
    return ret;
  }

  int get(ObFTDictHub *hub,
          ObIFTDict *&main_dict,
          ObIFTDict *&quan_dict,
          ObIFTDict *&stop_dict)
  {
    int ret = OB_SUCCESS;
    lib::ObLockGuard guard(lock_);
    bool need_rebuild = (nullptr == main_dict_);
    if (!need_rebuild && OB_FAIL(check_cache_state(hub))) {
      need_rebuild = true;
      ret = OB_SUCCESS;
    } else if (!need_rebuild) {
      need_rebuild = (main_range_count_ != main_state_.range_count_
                      || quan_range_count_ != quan_state_.range_count_
                      || stop_range_count_ != stop_state_.range_count_);
    }
    if (need_rebuild && OB_FAIL(rebuild(hub))) {
      return ret;
    }
    main_dict = main_dict_;
    quan_dict = quan_dict_;
    stop_dict = stop_dict_;
    return ret;
  }

private:
  struct ObDictState
  {
    ObDictState() : range_count_(-1) {}
    int32_t range_count_;
  };

  int check_cache_state(ObFTDictHub *hub)
  {
    int ret = OB_SUCCESS;
    ObDictState main_state;
    ObDictState quan_state;
    ObDictState stop_state;
    if (OB_FAIL(hub->get_dict_cache_state(main_desc_, main_state.range_count_))) {
    } else if (OB_FAIL(hub->get_dict_cache_state(quan_desc_, quan_state.range_count_))) {
    } else if (OB_FAIL(hub->get_dict_cache_state(stop_desc_, stop_state.range_count_))) {
    } else {
      main_state_ = main_state;
      quan_state_ = quan_state;
      stop_state_ = stop_state;
    }
    return ret;
  }

  int rebuild(ObFTDictHub *hub)
  {
    int ret = OB_SUCCESS;
    destroy_dicts();
    main_container_.reset();
    quan_container_.reset();
    stop_container_.reset();
    if (OB_FAIL(load_dict(hub, main_desc_, main_container_, main_dict_))) {
    } else if (OB_FAIL(load_dict(hub, quan_desc_, quan_container_, quan_dict_))) {
    } else if (OB_FAIL(load_dict(hub, stop_desc_, stop_container_, stop_dict_))) {
    } else {
      ObDictState state;
      if (OB_SUCC(hub->get_dict_cache_state(main_desc_, state.range_count_))) {
        main_state_ = state;
      }
      if (OB_SUCC(hub->get_dict_cache_state(quan_desc_, state.range_count_))) {
        quan_state_ = state;
      }
      if (OB_SUCC(hub->get_dict_cache_state(stop_desc_, state.range_count_))) {
        stop_state_ = state;
      }
    }
    if (OB_FAIL(ret)) {
      destroy_dicts();
    }
    return ret;
  }

  int load_dict(ObFTDictHub *hub,
                const ObFTDictDesc &desc,
                ObFTCacheRangeContainer &container,
                ObIFTDict *&dict)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(hub->load_cache(desc, container))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        if (OB_FAIL(hub->build_cache(desc, container))) {
        }
      } else {
      }
    }
    if (OB_SUCC(ret)) {
      dict = OB_NEWx(ObFTRangeDict, &alloc_, alloc_, &container, desc);
      if (OB_ISNULL(dict)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(dict->init())) {
        OB_DELETEx(ObIFTDict, &alloc_, dict);
        dict = nullptr;
      }
    }
    return ret;
  }

  void destroy_dicts()
  {
    if (OB_NOT_NULL(main_dict_)) {
      OB_DELETEx(ObIFTDict, &alloc_, main_dict_);
      main_dict_ = nullptr;
    }
    if (OB_NOT_NULL(quan_dict_)) {
      OB_DELETEx(ObIFTDict, &alloc_, quan_dict_);
      quan_dict_ = nullptr;
    }
    if (OB_NOT_NULL(stop_dict_)) {
      OB_DELETEx(ObIFTDict, &alloc_, stop_dict_);
      stop_dict_ = nullptr;
    }
  }

  void destroy()
  {
    destroy_dicts();
    main_container_.reset();
    quan_container_.reset();
    stop_container_.reset();
  }

private:
  static const ObFTDictDesc main_desc_;
  static const ObFTDictDesc quan_desc_;
  static const ObFTDictDesc stop_desc_;

  common::ObFIFOAllocator alloc_;
  ObFTCacheRangeContainer main_container_;
  ObFTCacheRangeContainer quan_container_;
  ObFTCacheRangeContainer stop_container_;

  ObIFTDict *main_dict_;
  ObIFTDict *quan_dict_;
  ObIFTDict *stop_dict_;

  ObDictState main_state_;
  ObDictState quan_state_;
  ObDictState stop_state_;

  int32_t main_range_count_;
  int32_t quan_range_count_;
  int32_t stop_range_count_;

  lib::ObMutex lock_;
};

const ObFTDictDesc ObIKFTSharedDict::main_desc_(
    "main_dict",
    ObFTDictType::DICT_IK_MAIN,
    ObCharsetType::CHARSET_UTF8MB4,
    ObCollationType::CS_TYPE_UTF8MB4_BIN);
const ObFTDictDesc ObIKFTSharedDict::quan_desc_(
    "quan_dict",
    ObFTDictType::DICT_IK_QUAN,
    ObCharsetType::CHARSET_UTF8MB4,
    ObCollationType::CS_TYPE_UTF8MB4_BIN);
const ObFTDictDesc ObIKFTSharedDict::stop_desc_(
    "stopword",
    ObFTDictType::DICT_IK_STOP,
    ObCharsetType::CHARSET_UTF8MB4,
    ObCollationType::CS_TYPE_UTF8MB4_BIN);

ObIKFTSharedDict &get_ik_shared_dict()
{
  static ObIKFTSharedDict shared_dict;
  static bool inited = false;
  if (!inited) {
    inited = true;
    shared_dict.init();
  }
  return shared_dict;
}

} // namespace

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

  if (OB_ISNULL(hub_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(get_ik_shared_dict().get(hub_, dict_main_, dict_quan_, dict_stop_))) {
    // already logged.
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

  // the dictionaries are owned by the process wide shared dictionary cache,
  // they must not be released here
  dict_main_ = nullptr;
  dict_quan_ = nullptr;
  dict_stop_ = nullptr;

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
