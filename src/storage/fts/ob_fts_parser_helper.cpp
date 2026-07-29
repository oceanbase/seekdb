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
#include "storage/fts/ob_fts_struct.h"
#define USING_LOG_PREFIX STORAGE_FTS

#include "storage/fts/ob_fts_parser_helper.h"

#include "common/json_type/ob_json_tree.h"
#include "share/ob_force_print_log.h"
#include "storage/fts/dict/ob_ft_dict_hub.h"
#include "storage/fts/ob_beng_ft_parser.h"
#include "storage/fts/ob_ik_ft_parser.h"
#include "storage/fts/ob_ngram2_ft_parser.h"
#include "storage/fts/ob_ngram_ft_parser.h"
#include "storage/fts/ob_fts_parser_property.h"
#include "storage/fts/ob_fts_stop_word.h"
#include "storage/fts/ob_whitespace_ft_parser.h"

namespace oceanbase
{
namespace storage
{

const char *ObFTParser::NAME_STR[ObFTParser::ParserType::FTP_MAX] = {
#define FT_PARSER_TYPE(ftp_type, parser_name) #parser_name,
  FTS_BUILD_IN_PARSER_LIST
#undef FT_PARSER_TYPE
};

bool ObFTParser::is_builtin() const
{
  bool is_builtin = false;
#define FT_PARSER_TYPE(ftp_type, parser_name) is_builtin = is_builtin || is_##parser_name();
  FTS_BUILD_IN_PARSER_LIST
#undef FT_PARSER_TYPE
  return is_builtin;
}

int ObFTParser::init(const common::ObString &parser_name)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(parser_name_.set_name(parser_name))) {
    LOG_WARN("failed to set parser name", K(ret), K(parser_name));
  } else if (!is_builtin()) {
    ret = OB_FUNCTION_NOT_DEFINED;
    LOG_USER_ERROR(OB_FUNCTION_NOT_DEFINED, parser_name.length(), parser_name.ptr());
    LOG_WARN("fulltext parser is not supported", K(ret), K(parser_name));
  } else {
    parser_version_ = BUILTIN_VERSION;
  }
  return ret;
}

int ObFTParser::parse_from_str(const char *parser_name, const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(parser_name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parser name is null", K(ret));
  } else if (OB_UNLIKELY(buf_len >= OB_FT_PARSER_NAME_LENGTH)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parser name is too long", K(ret), K(buf_len));
  } else {
    char name[OB_FT_PARSER_NAME_LENGTH];
    char *saveptr = nullptr;
    char *token = nullptr;
    char *end_ptr = nullptr;
    MEMCPY(name, parser_name, buf_len);
    name[buf_len] = '\0';
    if (OB_ISNULL(token = STRTOK_R(name, ".", &saveptr))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fulltext parser name is invalid", K(ret), KCSTRING(name));
    } else if (OB_FAIL(parser_name_.set_name(token))) {
      LOG_WARN("fail to set parser name", K(ret), KCSTRING(token));
    } else if (OB_ISNULL(token = STRTOK_R(nullptr, ".", &saveptr))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fulltext parser name is invalid", K(ret), KCSTRING(name));
    } else if (OB_FAIL(ob_strtoll(token, end_ptr, parser_version_))) {
      LOG_WARN("failed to convert str to ll", KCSTRING(token));
    } else if (OB_NOT_NULL(token = STRTOK_R(nullptr, ".", &saveptr))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fulltext parser name is invalid", K(ret), KCSTRING(name));
    } else if (!is_builtin()) {
      ret = OB_FUNCTION_NOT_DEFINED;
      LOG_USER_ERROR(OB_FUNCTION_NOT_DEFINED, static_cast<int>(buf_len), parser_name);
      LOG_WARN("fulltext parser is not supported", K(ret), KCSTRING(parser_name));
    } else if (OB_UNLIKELY(!is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("fulltext parser name is invalid", K(ret), KCSTRING(parser_name), KPC(this));
    }
  }
  return ret;
}

// The fulltext parser name consists of two parts: name and version, e.g. default_parser.1,
// separated by dot. This function is designed to serialize them into cstring.
int ObFTParser::serialize_to_str(char *buf, const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < OB_FT_PARSER_NAME_LENGTH)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(buf), K(buf_len));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("invalid fulltext parser doesn't support to serialize_to_str", K(ret), KPC(this));
  } else if (OB_FAIL(common::databuff_printf(buf, buf_len, pos, "%.*s.%ld", parser_name_.len(), parser_name_.str(),
          parser_version_))) {
    LOG_WARN("fail to printf", K(ret), K(buf_len), K(parser_name_), K(parser_version_));
  }
  return ret;
}

int ObFTParser::get_desc(const ObIFTParserDesc *&parser_desc) const
{
  int ret = OB_SUCCESS;
  parser_desc = nullptr;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fulltext parser is invalid", K(ret), KPC(this));
  } else if (is_space()) {
    static const ObWhiteSpaceFTParserDesc desc;
    parser_desc = &desc;
  } else if (is_ngram()) {
    static const ObNgramFTParserDesc desc;
    parser_desc = &desc;
  } else if (is_beng()) {
    static const ObBasicEnglishFTParserDesc desc;
    parser_desc = &desc;
  } else if (is_ik()) {
    static const ObIKFTParserDesc desc;
    parser_desc = &desc;
  } else if (is_ngram2()) {
    static const ObNgram2FTParserDesc desc;
    parser_desc = &desc;
  } else {
    ret = OB_FUNCTION_NOT_DEFINED;
    LOG_WARN("fulltext parser is not supported", K(ret), KPC(this));
  }
  return ret;
}
////////////////////////////////////////////////////////////////////////////////
// ObFTParseData

static ObFTParseData *g_ftparse_data = nullptr;
static constexpr const char *FTPARSE_DATA_MEMORY_LABEL = "FtParse";

int ObFTParseData::init_global()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(g_ftparse_data)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("fulltext parser data initialized twice", K(ret));
  } else if (OB_ISNULL(g_ftparse_data = OB_NEW(ObFTParseData, FTPARSE_DATA_MEMORY_LABEL))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory", K(ret), K(sizeof(ObFTParseData)));
  } else if (OB_FAIL(g_ftparse_data->init())) {
    LOG_WARN("failed to initialize fulltext parser data", K(ret));
  }
  return ret;
}

void ObFTParseData::deinit_global()
{
  if (OB_NOT_NULL(g_ftparse_data)) {
    OB_DELETE(ObFTParseData, FTPARSE_DATA_MEMORY_LABEL, g_ftparse_data);
    g_ftparse_data = nullptr;
  }
}

ObFTParseData &ObFTParseData::instance()
{
  return *g_ftparse_data;
}

ObFTParseData::~ObFTParseData()
{
  destroy();
}

int ObFTParseData::init()
{
  int ret = OB_SUCCESS;
  lib::ObMemAttr mem_attr;
  mem_attr.label_ = FTPARSE_DATA_MEMORY_LABEL;

  if (OB_FAIL(handler_allocator_.init(lib::ObMallocAllocator::get_instance(),
                                      OB_MALLOC_NORMAL_BLOCK_SIZE,
                                      mem_attr))) {
    LOG_WARN("failed to initialize fulltext parser allocator", K(ret));
  } else if (OB_FAIL(init_and_set_stopword_list())) {
    LOG_WARN("fail to init and set stopword list", K(ret));
  } else if (OB_FAIL(init_dict_hub())) {
    LOG_WARN("fail to init dict hub", K(ret));
  } else {
    is_inited_ = true;
    FLOG_INFO("succeeded to initialize fulltext parser data", KP(this));
  }

  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;
}

int ObFTParseData::init_and_set_stopword_list()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stop_word_checker_ = OB_NEWx(ObStopWordChecker, &handler_allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to create stop word checker", K(ret));
  } else if (OB_FAIL(stop_word_checker_->init())) {
    LOG_WARN("failed to init stop word checker", K(ret));
  }

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObStopWordChecker, &handler_allocator_, stop_word_checker_);
    stop_word_checker_ = nullptr;
  }

  return ret;
}

int ObFTParseData::init_dict_hub()
{
  // make dict
  int ret = OB_SUCCESS;
  if (OB_ISNULL(dict_hub_ = OB_NEWx(ObFTDictHub, &handler_allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc memory for dict hub.", K(ret));
  } else if (OB_FAIL(dict_hub_->init())) {
    LOG_WARN("Failed to init dict hub.", K(ret));
  }
  return ret;
}

void ObFTParseData::destroy()
{
  if (OB_NOT_NULL(stop_word_checker_)) {
    stop_word_checker_->destroy();
    OB_DELETEx(ObStopWordChecker, &handler_allocator_, stop_word_checker_);
    stop_word_checker_ = nullptr;
  }

  if (!OB_ISNULL(dict_hub_)) {
    dict_hub_->destroy();
    dict_hub_->~ObFTDictHub();
    handler_allocator_.free(dict_hub_);
    dict_hub_ = nullptr;
  }

  handler_allocator_.reset();
  is_inited_ = false;
}

int ObFTParseData::get_dict_hub(ObFTDictHub *&hub)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(dict_hub_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("Dict hub is null.", K(ret));
  } else {
    hub = dict_hub_;
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
// ObFTParseHelper
int ObFTParseHelper::segment(
    const ObFTParserProperty &property,
    const int64_t parser_version,
    const ObIFTParserDesc *parser_desc,
    const ObCharsetInfo *cs,
    const char *ft,
    const int64_t ft_len,
    common::ObIAllocator &allocator,
    ObAddWord &add_word)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(parser_version < 0 || nullptr == parser_desc || nullptr == cs || nullptr == ft || 0 >= ft_len)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(parser_version), KP(parser_desc), KP(cs), K(ft), K(ft_len));
  } else {
    ObFTParserParam param;
    ObITokenIterator *iter = nullptr;
    param.allocator_ = &allocator;
    param.cs_ = cs;
    param.fulltext_ = ft;
    param.ft_length_ = ft_len;
    param.parser_version_ = parser_version;
    param.ngram_token_size_ = property.ngram_token_size_;
    param.ik_param_.mode_
        = (property.ik_mode_smart_ ? ObFTIKParam::Mode::SMART : ObFTIKParam::Mode::MAX_WORD);
    param.min_ngram_size_ = property.min_ngram_token_size_;
    param.max_ngram_size_ = property.max_ngram_token_size_;

    if (OB_FAIL(parser_desc->segment(&param, iter))) {
      LOG_WARN("fail to segment", K(ret), K(param));
    } else if (OB_ISNULL(iter)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error, token iterator is nullptr", K(ret), KP(iter));
    } else {
      const char *word = nullptr;
      int64_t word_len = 0;
      int64_t char_cnt = 0;
      int64_t word_freq = 0;
      while (OB_SUCC(ret)) {
        if (OB_FAIL(iter->get_next_token(word, word_len, char_cnt, word_freq))) {
          if (OB_ITER_END != ret) {
            LOG_WARN("fail to get next token", K(ret), KPC(iter));
          }
        } else if (OB_FAIL(add_word.process_word(word, word_len, char_cnt, word_freq))) {
          LOG_WARN("fail to process one word", K(ret), KP(word), K(word_len), K(char_cnt), K(word_freq));
        }
      }
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      }
    }
    if (OB_NOT_NULL(iter)) {
      parser_desc->free_token_iter(&param, iter);
      iter = nullptr;
    }
  }
  return ret;
}

ObFTParseHelper::ObFTParseHelper()
    : allocator_(nullptr),
    parser_desc_(nullptr),
    parser_name_(),
    add_word_flag_(),
    parser_property_(),
    is_inited_(false)
{
}

ObFTParseHelper::~ObFTParseHelper()
{
  reset();
}

int ObFTParseHelper::init(
    common::ObIAllocator *allocator,
    const common::ObString &parser_name,
    const common::ObString &parser_properties)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("this fulltext parse helper has been initialized", K(ret), KP(parser_desc_), K(is_inited_));
  } else if (OB_ISNULL(allocator) || OB_UNLIKELY(parser_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(allocator), K(parser_name));
  } else if (OB_FAIL(parser_name_.parse_from_str(parser_name.ptr(), parser_name.length()))) {
    LOG_WARN("failed to parse fulltext parser name", K(ret), K(parser_name));
  } else if (OB_FAIL(parser_property_.parse_for_parser_helper(parser_name_, parser_properties))) {
    LOG_WARN("failed to parse fulltext parser properties", K(ret), K(parser_properties), K(parser_name_));
  } else if (OB_FAIL(parser_name_.get_desc(parser_desc_))) {
    LOG_WARN("failed to get fulltext parser", K(ret), K(parser_name_));
  } else if (OB_ISNULL(parser_desc_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, parse desc is nullptr", K(ret), KP(parser_desc_));
  } else if (OB_FAIL(set_add_word_flag(*parser_desc_))) {
    LOG_WARN("fail to set add word flag", K(ret), K(parser_name_));
  } else {
    allocator_ = allocator;
    is_inited_ = true;
    LOG_TRACE("succeeded to initialize fulltext parser helper", K(ret), K(parser_name), K(parser_properties), KPC(this));
  }
  if (OB_FAIL(ret) && OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

void ObFTParseHelper::reset()
{
  parser_desc_ = nullptr;
  allocator_ = nullptr;
  add_word_flag_.clear();
  is_inited_ = false;
}

int ObFTParseHelper::segment(
    const ObObjMeta &meta,
    const char *fulltext,
    const int64_t fulltext_len,
    int64_t &doc_length,
    ObFTWordMap &words) const
{
  int ret = OB_SUCCESS;
  const ObCharsetInfo *cs = nullptr;
  ObCollationType type = meta.get_collation_type();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("this fulltext parser helper hasn't been initialized", K(ret), K(is_inited_));
  } else if (OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("allocator ptr is nullptr", K(ret), KP_(allocator), K_(is_inited));
  } else if (OB_UNLIKELY(CS_TYPE_INVALID == type || type >= CS_TYPE_PINYIN_BEGIN_MARK)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(type));
  } else if (OB_ISNULL(cs = common::ObCharset::get_charset(type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, charset info is nullptr", K(ret), K(type));
  } else {
    words.reuse();
    ObAddWord add_word(parser_property_, meta, add_word_flag_, *allocator_, words);
    if (OB_FAIL(segment(
                    parser_property_,
                    parser_name_.get_parser_version(),
                    parser_desc_,
                    cs,
                    fulltext,
                    fulltext_len,
                    *allocator_,
                    add_word))) {
      LOG_WARN("fail to segment fulltext", K(ret), K(parser_name_), KP(parser_desc_), KP(cs), KP(fulltext),
          K(fulltext_len), KP(allocator_), K(parser_property_));
    } else {
      doc_length = add_word.get_add_word_count();
    }
  }
  LOG_DEBUG("ft parse segment", K(ret), K(type), K(add_word_flag_), K(parser_name_),
      K(ObString(fulltext_len, fulltext)), K(words.size()));
  return ret;
}

int ObFTParseHelper::check_is_the_same(
    const common::ObString &parser_name_str,
    const common::ObString &parser_properties,
    bool &is_same) const
{
  int ret = OB_SUCCESS;
  is_same = false;
  if (is_inited_) {
    storage::ObFTParser parser_name;
    ObFTParserProperty parser_property;
    if (OB_UNLIKELY(parser_name_str.empty())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(parser_name_str));
    } else if (OB_FAIL(parser_name.parse_from_str(parser_name_str.ptr(), parser_name_str.length()))) {
      LOG_WARN("failed to parse fulltext parser name", K(ret), K(parser_name_str));
    } else if (OB_FAIL(parser_property.parse_for_parser_helper(parser_name, parser_properties))) {
      LOG_WARN("failed to parse fulltext parser properties", K(ret), K(parser_properties), K(parser_name_));
    } else if (parser_name == parser_name_ && parser_property.is_equal(parser_property_)) {
      is_same = true;
    }
  }
  LOG_TRACE("ft parse helper check is the same", K(is_same), K(parser_name_str), K(parser_properties),
      K(parser_name_), K(parser_property_));
  return ret;
}

int ObFTParseHelper::make_detail_json(
    const ObFTWordMap &words,
    const int64_t doc_length,
    common::ObIJsonBase *&json_root)
{
 int ret = OB_SUCCESS;

 ObJsonObject *root_obj = nullptr;

 ObJsonInt *cnt = nullptr;

 ObJsonArray *token_array = nullptr;

 if (OB_ISNULL(root_obj = OB_NEWx(ObJsonObject, allocator_, allocator_))) {
   ret = OB_ALLOCATE_MEMORY_FAILED;
   LOG_WARN("Fail to alloc memory for json", K(ret));
 } else if (OB_ISNULL(cnt = OB_NEWx(ObJsonInt, allocator_, doc_length))) {
   ret = OB_ALLOCATE_MEMORY_FAILED;
   LOG_WARN("Fail to alloc memory for json", K(ret));
 } else if (OB_ISNULL(token_array = OB_NEWx(ObJsonArray, allocator_, allocator_))) {
   ret = OB_ALLOCATE_MEMORY_FAILED;
   LOG_WARN("Fail to alloc memory for json", K(ret));
 } else {
   for (ObFTWordMap::const_iterator it = words.begin(); OB_SUCC(ret) && it != words.end(); ++it) {
     ObString key = it->first.get_word().get_string();
     ObJsonObject *node = nullptr;
     ObJsonInt *token_cnt_node = nullptr;
     if (OB_ISNULL(node = OB_NEWx(ObJsonObject, allocator_, allocator_))) {
       ret = OB_ALLOCATE_MEMORY_FAILED;
       LOG_WARN("Fail to alloc memory for json int", K(ret));
     } else if (OB_ISNULL(token_cnt_node = OB_NEWx(ObJsonInt, allocator_, it->second))) {
       ret = OB_ALLOCATE_MEMORY_FAILED;
       LOG_WARN("Fail to alloc memory for json", K(ret));
     } else if (OB_FAIL(node->add(key, token_cnt_node))) {
       LOG_WARN("Fail to add token count to json", K(ret));
     } else if (OB_FAIL(token_array->append(node))) {
       LOG_WARN("Fail to append json object", K(ret));
     } else {
       // pass
     }

     if (OB_FAIL(ret)) {
       OB_DELETEx(ObJsonObject, allocator_, node);
       OB_DELETEx(ObJsonInt, allocator_, token_cnt_node);
     }
   } // for

   if (OB_SUCC(ret)) {
     if (OB_FAIL(root_obj->add(ENTRY_NAME_TOKENS, token_array))) {
       LOG_WARN("Fail to add token array to json", K(ret));
     } else if (OB_FAIL(root_obj->add(ENTRY_NAME_DOC_LEN, cnt))) {
       LOG_WARN("Fail to add doc len to json", K(ret));
     }
   }
 }
  if (OB_SUCC(ret)) {
    json_root = root_obj;
  } else {
    OB_DELETEx(ObJsonObject, allocator_, root_obj);
    OB_DELETEx(ObJsonInt, allocator_, cnt);
    OB_DELETEx(ObJsonArray, allocator_, token_array);
  }

  return ret;
}

int ObFTParseHelper::make_token_array_json(
    const ObFTWordMap &words,
    common::ObIJsonBase *&json_root)
{
  int ret = OB_SUCCESS;
  ObJsonArray *token_array = nullptr;
  if (OB_UNLIKELY(OB_ISNULL(token_array = OB_NEWx(ObJsonArray, allocator_, allocator_)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Fail to alloc memory for json", K(ret));
  } else {
    for (ObFTWordMap::const_iterator it = words.begin(); OB_SUCC(ret) && it != words.end(); ++it) {
      ObString key = it->first.get_word().get_string();
      ObJsonString *token = nullptr;
      if (OB_UNLIKELY(OB_ISNULL(token = OB_NEWx(ObJsonString, allocator_, key)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("Fail to alloc memory for json int", K(ret));
      } else {
        if (OB_FAIL(token_array->append(token))) {
          LOG_WARN("Fail to append json string", K(ret));
          OB_DELETEx(ObJsonString, allocator_, token);
        } else {
        }
      }
    } // for
  }
  if (OB_SUCC(ret)) {
    json_root = token_array;
  } else {
    OB_DELETEx(ObJsonArray, allocator_, token_array);
  }
  return ret;
}

int ObFTParseHelper::set_add_word_flag(const ObIFTParserDesc &ftparser_desc)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ftparser_desc.get_add_word_flag(add_word_flag_))) {
    LOG_WARN("failed to set add_word_flag", K(ret));
  }
  return ret;
}

} // end namespace storage
} // end namespace oceanbase
