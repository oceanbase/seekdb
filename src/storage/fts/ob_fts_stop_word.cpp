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

#include "object/ob_object.h"
#define USING_LOG_PREFIX STORAGE_FTS

#include "share/rc/ob_tenant_base.h"
#include "plugin/sys/ob_plugin_mgr.h"
#include "storage/fts/ob_fts_stop_word.h"
#include "storage/fts/ob_fts_plugin_helper.h"
#include "storage/fts/ob_fts_parser_property.h"
#include "storage/fts/ob_fts_plugin_helper.h"
#include "storage/fts/utils/ob_ft_ascii_utils.h"

namespace oceanbase
{
namespace storage
{

////////////////////////////////////////////////////////////////////////////////
// class ObStopWordChecker
ObStopWordChecker::~ObStopWordChecker()
{
  destroy();
}

int ObStopWordChecker::init()
{
  int ret = OB_SUCCESS;

  if (inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(stopword_set_.create(DEFAULT_STOPWORD_BUCKET_NUM, "StopWordSet", "StopWordSet"))) {
    LOG_WARN("fail to create stop word set", K(ret));
  } else {
    ObObjMeta stop_meta;
    stop_meta.set_varchar();
    stop_meta.set_collation_type(ObCollationType::CS_TYPE_UTF8MB4_GENERAL_CI);

    stopword_type_.set_meta(stop_meta);
    const int64_t stopword_count = sizeof(ob_stop_word_list) / sizeof(ob_stop_word_list[0]);
    for (int64_t i = 0; OB_SUCC(ret) && i < stopword_count; ++i) {
      const int64_t stopword_len = STRLEN(ob_stop_word_list[i]);
      max_stopword_len_ = MAX(max_stopword_len_, stopword_len);
      ObFTWord stopword(stopword_len, ob_stop_word_list[i], stopword_type_);
      if (OB_FAIL(stopword_set_.set_refactored(stopword))) {
        LOG_WARN("fail to set stop word", K(ret), K(stopword));
      }
    }

    if (OB_SUCC(ret)) {
      inited_ = true;
    }
  }
  return ret;
}

void ObStopWordChecker::destroy()
{
  if (inited_) {
    stopword_set_.destroy();
    max_stopword_len_ = 0;
    inited_ = false;
  }
}

int ObStopWordChecker::check_stopword_set_(const ObFTWord &word, bool &is_stopword) const
{
  int ret = OB_SUCCESS;
  ret = stopword_set_.exist_refactored(word);
  if (OB_HASH_NOT_EXIST == ret) {
    is_stopword = false;
    ret = OB_SUCCESS;
  } else if (OB_HASH_EXIST == ret) {
    is_stopword = true;
    ret = OB_SUCCESS;
  } else if (OB_SUCC(ret)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the exist of hastset shouldn't return success", K(ret), K(word));
  } else {
    LOG_WARN("fail to do exist", K(ret), K(word));
  }
  return ret;
}

bool ObStopWordChecker::is_ascii_string_(const ObString &word)
{
  return ascii::is_ascii_string(word.ptr(), word.length());
}

bool ObStopWordChecker::equals_ascii_ci_(const ObString &word, const char *literal)
{
  bool is_equal = word.length() == STRLEN(literal);
  for (int64_t i = 0; is_equal && i < word.length(); ++i) {
    is_equal = ascii::to_ascii_lower_char(word.ptr()[i]) == literal[i];
  }
  return is_equal;
}

bool ObStopWordChecker::match_ascii_stopword_(const ObString &word)
{
  bool is_match = false;
  if (word.empty()) {
  } else {
    const char lead = ascii::to_ascii_lower_char(word.ptr()[0]);
    switch (word.length()) {
      case 1:
        is_match = 'a' == lead || 'i' == lead;
        break;
      case 2:
        switch (lead) {
          case 'a':
            is_match = equals_ascii_ci_(word, "an")
                       || equals_ascii_ci_(word, "as")
                       || equals_ascii_ci_(word, "at");
            break;
          case 'b':
            is_match = equals_ascii_ci_(word, "be")
                       || equals_ascii_ci_(word, "by");
            break;
          case 'd':
            is_match = equals_ascii_ci_(word, "de");
            break;
          case 'e':
            is_match = equals_ascii_ci_(word, "en");
            break;
          case 'i':
            is_match = equals_ascii_ci_(word, "in")
                       || equals_ascii_ci_(word, "is")
                       || equals_ascii_ci_(word, "it");
            break;
          case 'l':
            is_match = equals_ascii_ci_(word, "la");
            break;
          case 'o':
            is_match = equals_ascii_ci_(word, "of")
                       || equals_ascii_ci_(word, "on")
                       || equals_ascii_ci_(word, "or");
            break;
          case 't':
            is_match = equals_ascii_ci_(word, "to");
            break;
          default:
            break;
        }
        break;
      case 3:
        switch (lead) {
          case 'a':
            is_match = equals_ascii_ci_(word, "are");
            break;
          case 'c':
            is_match = equals_ascii_ci_(word, "com");
            break;
          case 'f':
            is_match = equals_ascii_ci_(word, "for");
            break;
          case 'h':
            is_match = equals_ascii_ci_(word, "how");
            break;
          case 't':
            is_match = equals_ascii_ci_(word, "the");
            break;
          case 'u':
            is_match = equals_ascii_ci_(word, "und");
            break;
          case 'w':
            is_match = equals_ascii_ci_(word, "was")
                       || equals_ascii_ci_(word, "who")
                       || equals_ascii_ci_(word, "www");
            break;
          default:
            break;
        }
        break;
      case 4:
        switch (lead) {
          case 'f':
            is_match = equals_ascii_ci_(word, "from");
            break;
          case 't':
            is_match = equals_ascii_ci_(word, "that")
                       || equals_ascii_ci_(word, "this");
            break;
          case 'w':
            is_match = equals_ascii_ci_(word, "what")
                       || equals_ascii_ci_(word, "when")
                       || equals_ascii_ci_(word, "will")
                       || equals_ascii_ci_(word, "with");
            break;
          default:
            break;
        }
        break;
      case 5:
        switch (lead) {
          case 'a':
            is_match = equals_ascii_ci_(word, "about");
            break;
          case 'w':
            is_match = equals_ascii_ci_(word, "where");
            break;
          default:
            break;
        }
        break;
      default:
        break;
    }
  }
  return is_match;
}

int ObStopWordChecker::check_stopword(const ObFTWord &word, bool &is_stopword)
{
  int ret = OB_SUCCESS;
  is_stopword = false;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStopWordChecker hasn't been initialized", K(ret), K(inited_));
  } else if (OB_UNLIKELY(word.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("word is empty", K(ret), K(word));
  } else {
    const common::ObString word_str = word.get_word().get_string();
    if (is_ascii_string_(word_str)) {
      is_stopword = word_str.length() <= max_stopword_len_ && match_ascii_stopword_(word_str);
    } else if (word.get_obj_meta() == stopword_type_) {
      if (word_str.length() > max_stopword_len_) {
        is_stopword = false;
      } else if (OB_FAIL(check_stopword_set_(word, is_stopword))) {
        LOG_WARN("fail to check stopword set", K(ret), K(word));
      }
    } else {
      common::ObArenaAllocator allocator(lib::ObMemAttr("ChkStopWord"));
      common::ObString cmp_str;
      if (OB_FAIL(common::ObCharset::charset_convert(allocator,
                                                     word_str,
                                                     word.get_collation_type(),
                                                     stopword_type_.get_collation_type(),
                                                     cmp_str))) {
        LOG_WARN("fail to convert charset", K(ret), K(word), K(stopword_type_));
      } else if (cmp_str.length() > max_stopword_len_) {
        is_stopword = false;
      } else {
        ObFTWord converted(cmp_str.length(), cmp_str.ptr(), stopword_type_);
        if (OB_FAIL(check_stopword_set_(converted, is_stopword))) {
          LOG_WARN("fail to do exist", K(ret), K(word), K(converted));
        }
      }
    }
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
// class ObAddWord
ObAddWord::ObAddWord(
    const ObFTParserProperty &property,
    const ObObjMeta &meta,
    const ObAddWordFlag &flag,
    common::ObIAllocator &allocator,
    ObFTWordMap &word_map)
  : word_meta_(meta),
    allocator_(allocator),
    word_map_(&word_map),
    min_max_word_cnt_(0),
    non_stopword_cnt_(0),
    stopword_cnt_(0),
    min_token_size_(property.min_token_size_),
    max_token_size_(property.max_token_size_),
    flag_(flag),
    stop_word_checker_(nullptr),
    has_min_max_word_(flag.min_max_word()),
    has_stopword_(flag.stopword()),
    has_casedown_(flag.casedown()),
    has_groupby_word_(flag.groupby_word())
{
}

int ObAddWord::process_word(
    const char *word,
    const int64_t word_len,
    const int64_t char_cnt,
    const int64_t word_freq)
{
  int ret = OB_SUCCESS;
  bool is_stopword = false;
  ObFTWord src_word(word_len, word, word_meta_);
  ObFTWord dst_word;
  if (OB_ISNULL(word) || OB_UNLIKELY(0 >= word_len || 0 >= char_cnt || 0 >= word_freq)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(word), K(word_len), K(char_cnt), K(word_freq));
  } else if (is_min_max_word(char_cnt)) {
    ++min_max_word_cnt_;
    LOG_DEBUG("skip too small or large word", K(ret), K(src_word), K(char_cnt));
  } else if (OB_FAIL(casedown_word(src_word, dst_word))) {
    LOG_WARN("fail to casedown word", K(ret), K(src_word));
  } else if (OB_FAIL(check_stopword(dst_word, is_stopword))) {
    LOG_WARN("fail to check stopword", K(ret), K(dst_word));
  } else if (OB_UNLIKELY(is_stopword)) {
    ++stopword_cnt_;
    LOG_DEBUG("skip stopword", K(ret), K(dst_word));
  } else if (OB_FAIL(groupby_word(dst_word, word_freq))) {
    LOG_WARN("fail to groupby word into word map", K(ret), K(dst_word), K(word_freq));
  } else {
    non_stopword_cnt_ += word_freq;
    LOG_DEBUG("add word", K(ret), KP(word), K(word_len), K(char_cnt), K(word_freq), K(src_word), K(dst_word));
  }
  return ret;
}

bool ObAddWord::is_min_max_word(const int64_t c_len) const
{
  return has_min_max_word_ && (c_len < min_token_size_ || c_len > max_token_size_);
}

int ObAddWord::casedown_word(const ObFTWord &src, ObFTWord &dst)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(src.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid src ft word", K(ret), K(src));
  } else if (has_casedown_) {
    const ObString src_str = src.get_word().get_string();
    const ObCharsetType charset_type = ObCharset::charset_type_by_coll(word_meta_.get_collation_type());
    if (CHARSET_UTF8MB4 == charset_type && ascii::is_ascii_string(src_str.ptr(), src_str.length())) {
      if (ascii::has_ascii_upper(src_str.ptr(), src_str.length())) {
        char *buf = static_cast<char *>(allocator_.alloc(src_str.length()));
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate ascii casedown buffer", K(ret), K(src_str));
        } else {
          ascii::lowercase_ascii_copy(src_str.ptr(), src_str.length(), buf);
          ObFTWord tmp(src_str.length(), buf, word_meta_);
          dst = tmp;
        }
      } else {
        ObFTWord tmp(src_str.length(), src_str.ptr(), word_meta_);
        dst = tmp;
      }
    } else {
      ObString dst_str;
      if (OB_FAIL(ObCharset::tolower(
                      word_meta_.get_collation_type(), src_str, dst_str, allocator_))) {
        LOG_WARN("fail to tolower", K(ret), K(src), K(word_meta_));
      } else {
        ObFTWord tmp(dst_str.length(), dst_str.ptr(), word_meta_);
        dst = tmp;
      }
    }
  } else {
    dst = src;
  }
  return ret;
}

int ObAddWord::check_stopword(const ObFTWord &ft_word, bool &is_stopword)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ft_word.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(ft_word));
  } else if (has_stopword_) {
    if (OB_ISNULL(stop_word_checker_)) {
      stop_word_checker_ = ObFTParsePluginData::instance().stop_word_checker();
    }
    if (OB_ISNULL(stop_word_checker_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("got null stop word checker", K(ret));
    } else if (OB_FAIL(stop_word_checker_->check_stopword(ft_word, is_stopword))) {
      LOG_WARN("fail to check stopword", K(ret));
    }
  }
  return ret;
}

int ObAddWord::groupby_word(const ObFTWord &word, const int64_t word_freq)
{
  int ret = OB_SUCCESS;
  int64_t word_count = 0;
  if (OB_UNLIKELY(word.empty() || word_freq <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(word), K(word_freq));
  } else if (OB_ISNULL(word_map_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("word map is null", K(ret));
  } else if (!has_groupby_word_) {
    if (OB_FAIL(word_map_->set_refactored(word, word_freq))) {
      LOG_WARN("fail to set fulltext word and count", K(ret), K(word));
    }
  } else {
    int64_t *exist_word_count = word_map_->get(word);
    if (OB_ISNULL(exist_word_count)) {
      word_count = word_freq;
      if (OB_FAIL(word_map_->set_refactored(word, word_count))) {
        LOG_WARN("fail to set fulltext word and count", K(ret), K(word), K(word_count));
      }
    } else {
      *exist_word_count += word_freq;
    }
  }
  return ret;
}

} // end namespace storage
} // end namespace oceanbase
