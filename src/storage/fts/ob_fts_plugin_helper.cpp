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
#include "storage/fts/ob_fts_struct.h"  // ObFTWordMap typedef(previously hidden behind a transitive include)
#define USING_LOG_PREFIX STORAGE_FTS

#include "storage/fts/ob_fts_plugin_helper.h"

#include "common/json_type/ob_json_tree.h"
#include "lib/worker.h"
#include "plugin/interface/ob_plugin_ftparser_intf.h"
#include "plugin/sys/ob_plugin_helper.h"
#include "share/ob_force_print_log.h"
#include "storage/fts/dict/ob_ft_dict_hub.h"
#include "storage/fts/ob_fts_parser_property.h"
#include "storage/fts/ob_ft_token_processor.h"

using namespace oceanbase::plugin;

namespace oceanbase
{
namespace storage
{

const char *ObFTParser::NAME_STR[ObFTParser::ParserType::FTP_MAX + 1] = {
  "non-builtin",
#define FT_PARSER_TYPE(ftp_type, parser_name) #parser_name,
  FTS_BUILD_IN_PARSER_LIST
#undef FT_PARSER_TYPE
  "max type of parser"
};

// The plugin_name comes from index table schema and consists of two parts: name and
// version, e.g. default_parser.1, separated by dot.
int ObFTParser::parse_from_str(const char *plugin_name, const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(plugin_name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("plugin name is nullptr", K(ret), KP(plugin_name));
  } else if (OB_UNLIKELY(buf_len >= share::OB_PLUGIN_NAME_LENGTH)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("plugin name is too long", K(ret), K(buf_len));
  } else {
    char name[share::OB_PLUGIN_NAME_LENGTH];
    char *saveptr = nullptr;
    char *token = nullptr;
    char *end_ptr = nullptr;
    MEMCPY(name, plugin_name, buf_len);
    name[buf_len] = '\0';
    if (OB_ISNULL(token = STRTOK_R(name, ".", &saveptr))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error, plugin name is illegal", K(ret), KCSTRING(name));
    } else if (OB_FAIL(parser_name_.set_name(token))) {
      LOG_WARN("fail to set parser name", K(ret), KCSTRING(token));
    } else if (OB_ISNULL(token = STRTOK_R(nullptr, ".", &saveptr))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error, plugin name is illegal", K(ret), KCSTRING(name));
    } else if (OB_FAIL(ob_strtoll(token, end_ptr, parser_version_))) {
      LOG_WARN("failed to convert str to ll", KCSTRING(token));
    } else if (OB_NOT_NULL(token = STRTOK_R(nullptr, ".", &saveptr))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error, plugin name is illegal", K(ret), KCSTRING(name));
    } else if (OB_UNLIKELY(!is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("plugin name isn't valid fulltext parser", K(ret), KCSTRING(plugin_name), KPC(this));
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
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < share::OB_PLUGIN_NAME_LENGTH)) {
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
////////////////////////////////////////////////////////////////////////////////
// ObFTParsePluginData

static ObFTParsePluginData *g_ftparse_plugin_data = nullptr;
static constexpr const char *FTPARSE_PLUGIN_DATA_MEMORY_LABEL = "FtParse";

int ObFTParsePluginData::init_global()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(g_ftparse_plugin_data)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ftparse plugin data init twice", K(ret));
  } else if (OB_ISNULL(g_ftparse_plugin_data = OB_NEW(ObFTParsePluginData, FTPARSE_PLUGIN_DATA_MEMORY_LABEL))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory", K(ret), K(sizeof(ObFTParsePluginData)));
  } else if (OB_FAIL(g_ftparse_plugin_data->init())) {
    LOG_WARN("failed to init global ftparse plugin data object", K(ret));
  }
  return ret;
}

void ObFTParsePluginData::deinit_global()
{
  if (OB_NOT_NULL(g_ftparse_plugin_data)) {
    OB_DELETE(ObFTParsePluginData, FTPARSE_PLUGIN_DATA_MEMORY_LABEL, g_ftparse_plugin_data);
    g_ftparse_plugin_data = nullptr;
  }
}

ObFTParsePluginData &ObFTParsePluginData::instance()
{
  return *g_ftparse_plugin_data;
}

ObFTParsePluginData::~ObFTParsePluginData()
{
  destroy();
}

int ObFTParsePluginData::init()
{
  int ret = OB_SUCCESS;
  lib::ObMemAttr mem_attr;
  mem_attr.label_ = FTPARSE_PLUGIN_DATA_MEMORY_LABEL;

  if (OB_FAIL(handler_allocator_.init(lib::ObMallocAllocator::get_instance(),
                                      OB_MALLOC_NORMAL_BLOCK_SIZE,
                                      mem_attr))) {
    LOG_WARN("fail to init tenant plugin handler allocator", K(ret));
  } else if (OB_FAIL(init_stop_token_checker_gen())) {
    LOG_WARN("fail to init and set stopword list", K(ret));
  } else if (OB_FAIL(init_dict_hub())) {
    LOG_WARN("fail to init dict hub", K(ret));
  } else {
    is_inited_ = true;
    FLOG_INFO("succeed to initialize ObTenantFTPluginMgr", KP(this));
  }

  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;
}

int ObFTParsePluginData::get_stop_token_checker(
    const ObCollationType coll,
    ObStopTokenChecker &stop_token_checker)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the fulltext plugin data is not initialized", K(ret));
  } else if (OB_FAIL(stop_token_checker_gen_->get_stop_token_checker_by_coll(coll, stop_token_checker))) {
    LOG_WARN("failed to get stop token checker", K(ret), K(coll));
  }
  return ret;
}

int ObFTParsePluginData::init_stop_token_checker_gen()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stop_token_checker_gen_ = OB_NEWx(ObStopTokenCheckerGen, &handler_allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to create stop token checker generator", K(ret));
  } else if (OB_FAIL(stop_token_checker_gen_->init())) {
    LOG_WARN("failed to init stop token checker generator", K(ret));
  }

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObStopTokenCheckerGen, &handler_allocator_, stop_token_checker_gen_);
    stop_token_checker_gen_ = nullptr;
  }
  return ret;
}

int ObFTParsePluginData::init_dict_hub()
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

void ObFTParsePluginData::destroy()
{
  if (OB_NOT_NULL(stop_token_checker_gen_)) {
    stop_token_checker_gen_->reset();
    OB_DELETEx(ObStopTokenCheckerGen, &handler_allocator_, stop_token_checker_gen_);
    stop_token_checker_gen_ = nullptr;
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

int ObFTParsePluginData::get_dict_hub(ObFTDictHub *&hub)
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

ObFTParseHelper::ObFTParseHelper()
    : allocator_(nullptr),
    parser_desc_(nullptr),
    plugin_param_(nullptr),
    parser_name_(),
    process_token_flag_(),
    parser_property_(),
    parser_metadata_allocator_(lib::ObMemAttr("FTParserMeta")),
    cached_builtin_parser_(nullptr),
    need_position_list_(false),
    is_inited_(false)
{
}

ObFTParseHelper::~ObFTParseHelper()
{
  reset();
}

int ObFTParseHelper::init(
    common::ObIAllocator *allocator,
    const common::ObString &plugin_name,
    const common::ObString &plugin_properties)
{
  return init(allocator, plugin_name, plugin_properties, false);
}

int ObFTParseHelper::init(
    common::ObIAllocator *allocator,
    const common::ObString &plugin_name,
    const common::ObString &plugin_properties,
    const bool need_position_list)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("this fulltext parse helper has been initialized", K(ret), KP(parser_desc_), K(is_inited_));
  } else if (OB_ISNULL(allocator) || OB_UNLIKELY(plugin_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(allocator), K(plugin_name));
  } else if (OB_FAIL(parser_name_.parse_from_str(plugin_name.ptr(), plugin_name.length()))) {
    LOG_WARN("fail to parse name from cstring", K(ret), K(plugin_name));
  } else if (OB_FAIL(parser_property_.parse_for_parser_helper(parser_name_, plugin_properties))) {
    LOG_WARN("fail to parse parser property from cstring", K(ret), K(plugin_properties), K(parser_name_));
  } else if (OB_FAIL(ObPluginHelper::find_ftparser(parser_name_.get_parser_name().str(),
                                                   parser_desc_, plugin_param_))) {
    if (OB_FUNCTION_NOT_DEFINED == ret) {
      LOG_DEBUG("no such parser", K(parser_name_), K(ret));
    } else {
      LOG_WARN("fail to open plugin handler", K(ret), K(plugin_name));
    }
  } else if (OB_ISNULL(parser_desc_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, parse desc is nullptr", K(ret), KP(parser_desc_));
  } else if (OB_FAIL(set_process_token_flag(*parser_desc_))) {
    LOG_WARN("fail to set process token flag", K(ret), K(parser_name_));
  } else {
    allocator_ = allocator;
    need_position_list_ = need_position_list;
    is_inited_ = true;
    LOG_TRACE("succeed to init ft parser helper", K(ret), K(plugin_name), K(plugin_properties), KPC(this));
  }
  if (OB_FAIL(ret) && OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

void ObFTParseHelper::reset()
{
  destroy_cached_builtin_parser_();
  parser_desc_ = nullptr;
  plugin_param_ = nullptr;
  allocator_ = nullptr;
  process_token_flag_.clear();
  // 缓存解析器已通过 descriptor 析构，此处才可回收其长生命周期元数据 arena。
  parser_metadata_allocator_.reset();
  need_position_list_ = false;
  is_inited_ = false;
}

void ObFTParseHelper::destroy_cached_builtin_parser_()
{
  // seekdb 的 LOG_WARN 宏会统一记录 ret；即使析构路径无返回值，也要提供成功缺省值。
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(cached_builtin_parser_)) {
    if (OB_NOT_NULL(parser_desc_) && OB_NOT_NULL(allocator_)) {
      // descriptor 的 free_token_iter 是解析器唯一的释放入口，不能跨越插件 ABI 直接 free。
      ObFTParserParam release_param;
      plugin::ObITokenIterator *iter = cached_builtin_parser_;
      // 缓存对象一定从 helper 自有 arena 分配，释放时必须使用同一 allocator。
      release_param.metadata_alloc_ = &parser_metadata_allocator_;
      parser_desc_->free_token_iter(&release_param, iter);
    } else {
      LOG_WARN("cached builtin parser has incomplete release context",
               KP(cached_builtin_parser_), KP(parser_desc_), KP(allocator_));
    }
    cached_builtin_parser_ = nullptr;
  }
}

int ObFTParseHelper::segment(
    const ObObjMeta &meta,
    const char *fulltext,
    const int64_t fulltext_len,
    int64_t &doc_length,
    ObFTTokenMap &ft_token_map)
{
  int ret = OB_SUCCESS;
  const ObCharsetInfo *cs = nullptr;
  ObCollationType type = meta.get_collation_type();
  ObString ft_str(fulltext_len, fulltext);
  ObString regularized_ft_str;
  const bool need_tolower = process_token_flag_.casedown_token();
  doc_length = 0;
  ft_token_map.reuse();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("this fulltext parser helper hasn't been initialized", K(ret), K(is_inited_));
  } else if (OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("allocator ptr is nullptr", K(ret), KP_(allocator), K_(is_inited));
  } else if (OB_UNLIKELY(CS_TYPE_INVALID == type || type >= CS_TYPE_PINYIN_BEGIN_MARK
                         || nullptr == fulltext || fulltext_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(type));
  } else if (OB_ISNULL(cs = common::ObCharset::get_charset(type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, charset info is nullptr", K(ret), K(type));
  } else if (need_tolower
             && OB_FAIL(ObCharset::tolower(type, ft_str, regularized_ft_str, *allocator_))) {
    LOG_WARN("fail to lowercase fulltext once before parsing", K(ret), K(type));
  } else if (need_tolower && OB_UNLIKELY(regularized_ft_str.empty())) {
    // 非法编码由 charset 层转为空串；保持历史行为，返回空 token 集。
  } else {
    ObFTTokenProcessor token_processor(*allocator_);
    ObFTParserParam param;
    ObITokenIterator *iter = nullptr;
    // 内置解析器会跨文档缓存，不能使用调用方每行 reset 的 arena；外部插件保持旧 ABI。
    param.metadata_alloc_ = parser_name_.is_builtin_parser()
        ? static_cast<ObIAllocator *>(&parser_metadata_allocator_) : allocator_;
    param.scratch_alloc_ = allocator_;
    param.allocator_ = allocator_; // 兼容尚未迁移的外部插件 ABI。
    param.cs_ = cs;
    param.fulltext_ = need_tolower ? regularized_ft_str.ptr() : fulltext;
    param.ft_length_ = need_tolower ? regularized_ft_str.length() : fulltext_len;
    param.parser_version_ = parser_name_.get_parser_version();
    param.plugin_param_ = plugin_param_;
    param.ngram_token_size_ = parser_property_.ngram_token_size_;
    param.ik_param_.mode_ = parser_property_.ik_mode_smart_
        ? ObFTIKParam::Mode::SMART : ObFTIKParam::Mode::MAX_WORD;
    // Task 3 的词典身份必须原样传入；表名负责加载，稳定 table id 负责 refresh generation 隔离。
    param.ik_param_.main_dict_ = parser_property_.dict_table_;
    param.ik_param_.quan_dict_ = parser_property_.quantifier_table_;
    param.ik_param_.stopword_dict_ = parser_property_.stopword_table_;
    param.ik_param_.main_dict_table_id_ = parser_property_.dict_table_id_;
    param.ik_param_.quantifier_dict_table_id_ = parser_property_.quantifier_table_id_;
    param.ik_param_.stopword_dict_table_id_ = parser_property_.stopword_table_id_;
    param.min_ngram_size_ = parser_property_.min_ngram_token_size_;
    param.max_ngram_size_ = parser_property_.max_ngram_token_size_;

    if (OB_FAIL(token_processor.init(parser_property_, meta, process_token_flag_, &ft_token_map))) {
      LOG_WARN("fail to initialize token processor", K(ret), K(token_processor));
    } else {
      if (OB_NOT_NULL(cached_builtin_parser_)) {
        // 同一 helper 只在配置未变时存活；此处只替换文档视图，词典和解析器元数据继续复用。
        iter = cached_builtin_parser_;
        if (OB_FAIL(cached_builtin_parser_->reuse_parser(param.fulltext_, param.ft_length_))) {
          LOG_WARN("failed to reuse builtin token iterator", K(ret), K(param));
        }
      } else if (OB_FAIL(parser_desc_->segment(&param, iter))) {
        LOG_WARN("fail to get token iterator", K(ret), K(param));
      } else if (parser_name_.is_builtin_parser()) {
        // 仅运行时类型确认成功才进入缓存；同名外部插件不能越过其既有 ABI 被错误复用。
        cached_builtin_parser_ = dynamic_cast<ObIFTParser *>(iter);
      }
      if (OB_SUCC(ret) && OB_ISNULL(iter)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected error, token iterator is nullptr", K(ret));
      }
      if (OB_SUCC(ret)) {
        const char *word = nullptr;
        int64_t word_len = 0;
        int64_t char_cnt = 0;
        int64_t word_freq = 0;
        int64_t simple_pos = 0;
        int64_t token_interval_cnt = 0;
        constexpr int64_t CHECK_STATUS_TOKEN_INTERVAL_CNT = 100;
        while (OB_SUCC(ret)) {
          if (OB_FAIL(iter->get_next_token(word, word_len, char_cnt, word_freq))) {
            if (OB_ITER_END != ret) {
              LOG_WARN("fail to get next token", K(ret), KPC(iter));
            }
          } else if (OB_FAIL(token_processor.process_token(
                         need_position_list_, word, word_len, char_cnt, simple_pos++))) {
            LOG_WARN("fail to process token", K(ret), KP(word), K(word_len), K(char_cnt));
          } else if (++token_interval_cnt >= CHECK_STATUS_TOKEN_INTERVAL_CNT) {
            if (OB_FAIL(THIS_WORKER.check_status())) {
              LOG_WARN("worker interrupted during fulltext segment", K(ret));
            } else {
              token_interval_cnt = 0;
            }
          }
        }
        if (OB_ITER_END == ret) {
          doc_length = token_processor.get_non_stop_token_count();
          ret = OB_SUCCESS;
        }
      }
    }
    if (OB_NOT_NULL(iter) && iter != cached_builtin_parser_) {
      parser_desc_->free_token_iter(&param, iter);
      iter = nullptr;
    }
  }
  LOG_DEBUG("ft parse segment", K(ret), K(type), K(process_token_flag_), K(parser_name_),
      K(ObString(fulltext_len, fulltext)), K(ft_token_map.size()));
  return ret;
}

int ObFTParseHelper::segment(
    const ObObjMeta &meta,
    const char *fulltext,
    const int64_t fulltext_len,
    int64_t &doc_length,
    ObFTWordMap &words)
{
  int ret = OB_SUCCESS;
  const int64_t bucket_count = MIN(MAX(fulltext_len / 10, 2), 997);
  ObFTTokenMap token_map;
  words.reuse();
  if (OB_FAIL(token_map.create(bucket_count, common::ObMemAttr("FTTokenCompat")))) {
    LOG_WARN("fail to create compatibility token map", K(ret), K(bucket_count));
  } else if (OB_FAIL(segment(meta, fulltext, fulltext_len, doc_length, token_map))) {
    LOG_WARN("fail to segment fulltext through token hot path", K(ret));
  } else {
    for (ObFTTokenMap::const_iterator it = token_map.begin(); OB_SUCC(ret) && it != token_map.end(); ++it) {
      if (OB_FAIL(words.set_refactored(it->first, it->second.count_))) {
        LOG_WARN("fail to project token info to legacy word map", K(ret), K(it->first));
      }
    }
  }
  return ret;
}

int ObFTParseHelper::check_is_the_same(
    const common::ObString &plugin_name,
    const common::ObString &plugin_properties,
    bool &is_same) const
{
  return check_is_the_same(plugin_name, plugin_properties, false, is_same);
}

int ObFTParseHelper::check_is_the_same(
    const common::ObString &plugin_name,
    const common::ObString &plugin_properties,
    const bool need_position_list,
    bool &is_same) const
{
  int ret = OB_SUCCESS;
  is_same = false;
  if (is_inited_) {
    storage::ObFTParser parser_name;
    ObFTParserProperty parser_property;
    if (OB_UNLIKELY(plugin_name.empty())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(plugin_name));
    } else if (OB_FAIL(parser_name.parse_from_str(plugin_name.ptr(), plugin_name.length()))) {
      LOG_WARN("fail to parse name from cstring", K(ret), K(plugin_name));
    } else if (OB_FAIL(parser_property.parse_for_parser_helper(parser_name, plugin_properties))) {
      LOG_WARN("fail to parse parser property from cstring", K(ret), K(plugin_properties), K(parser_name_));
    } else if (parser_name == parser_name_
               && parser_property.is_equal(parser_property_)
               && need_position_list == need_position_list_) {
      is_same = true;
    }
  }
  LOG_TRACE("ft parse helper check is the same", K(is_same), K(plugin_name), K(plugin_properties),
      K(parser_name_), K(parser_property_));
  return ret;
}

int ObFTParseHelper::make_detail_json(
    const ObFTTokenMap &ft_token_map,
    const int64_t doc_length,
    common::ObIJsonBase *&json_root)
{
  int ret = OB_SUCCESS;
  ObJsonObject *root_obj = nullptr;
  ObJsonInt *cnt = nullptr;
  ObJsonArray *token_array = nullptr;
  if (OB_ISNULL(root_obj = OB_NEWx(ObJsonObject, allocator_, allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(cnt = OB_NEWx(ObJsonInt, allocator_, doc_length))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(token_array = OB_NEWx(ObJsonArray, allocator_, allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    for (ObFTTokenMap::const_iterator it = ft_token_map.begin();
         OB_SUCC(ret) && it != ft_token_map.end(); ++it) {
      const ObString key = it->first.get_token().get_string();
      ObJsonObject *node = nullptr;
      ObJsonInt *token_cnt_node = nullptr;
      if (OB_ISNULL(node = OB_NEWx(ObJsonObject, allocator_, allocator_))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_ISNULL(token_cnt_node = OB_NEWx(ObJsonInt, allocator_, it->second.count_))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(node->add(key, token_cnt_node))) {
        LOG_WARN("fail to add token count to json", K(ret));
      } else if (OB_FAIL(token_array->append(node))) {
        LOG_WARN("fail to append token json", K(ret));
      }
      if (OB_FAIL(ret)) {
        OB_DELETEx(ObJsonObject, allocator_, node);
        OB_DELETEx(ObJsonInt, allocator_, token_cnt_node);
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(root_obj->add(ENTRY_NAME_TOKENS, token_array))) {
      LOG_WARN("fail to add token array", K(ret));
    } else if (OB_SUCC(ret) && OB_FAIL(root_obj->add(ENTRY_NAME_DOC_LEN, cnt))) {
      LOG_WARN("fail to add document length", K(ret));
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
    const ObFTTokenMap &ft_token_map,
    common::ObIJsonBase *&json_root)
{
  int ret = OB_SUCCESS;
  ObJsonArray *token_array = nullptr;
  if (OB_ISNULL(token_array = OB_NEWx(ObJsonArray, allocator_, allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    for (ObFTTokenMap::const_iterator it = ft_token_map.begin();
         OB_SUCC(ret) && it != ft_token_map.end(); ++it) {
      const ObString key = it->first.get_token().get_string();
      ObJsonString *token = nullptr;
      if (OB_ISNULL(token = OB_NEWx(ObJsonString, allocator_, key))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(token_array->append(token))) {
        LOG_WARN("fail to append token json", K(ret));
        OB_DELETEx(ObJsonString, allocator_, token);
      }
    }
  }
  if (OB_SUCC(ret)) {
    json_root = token_array;
  } else {
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

int ObFTParseHelper::set_process_token_flag(const ObIFTParserDesc &ftparser_desc)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ftparser_desc.get_add_word_flag(process_token_flag_))) {
    LOG_WARN("failed to set process token flag", K(ret));
  }
  return ret;
}

} // end namespace storage
} // end namespace oceanbase
