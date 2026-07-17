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

#ifndef OB_FTS_PLUGIN_HELPER_H_
#define OB_FTS_PLUGIN_HELPER_H_

#include "lib/allocator/ob_fifo_allocator.h"
#include "lib/allocator/page_arena.h"
#include "lib/charset/ob_charset.h"
#include "lib/string/ob_string.h"
#include "object/ob_object.h"
#include "plugin/interface/ob_plugin_ftparser_intf.h"
#include "share/ob_plugin_helper.h"
#include "storage/fts/ob_fts_parser_property.h"
#include "storage/fts/ob_fts_struct.h"
#include "storage/fts/ob_i_ft_parser.h"

namespace oceanbase
{
namespace common
{
class ObIJsonBase;
}

namespace plugin
{
class ObIFTParserDesc;
class ObPluginParam;
}

namespace storage
{

class ObStopTokenCheckerGen;
class ObStopTokenChecker;
class ObFTDictHub;

#define FTS_BUILD_IN_PARSER_LIST                                                                   \
  FT_PARSER_TYPE(FTP_SPACE, space)                                                                 \
  FT_PARSER_TYPE(FTP_NGRAM, ngram)                                                                 \
  FT_PARSER_TYPE(FTP_BENG, beng)                                                                   \
  FT_PARSER_TYPE(FTP_IK, ik)                                                                       \
  FT_PARSER_TYPE(FTP_NGRAM2, ngram2)

class ObFTParser final
{
public:
  enum ParserType : int64_t {
    FTP_NON_BUILDIN = 0,
#define FT_PARSER_TYPE(ftp_type, parser_name) ftp_type,
    FTS_BUILD_IN_PARSER_LIST
#undef FT_PARSER_TYPE
    FTP_MAX
  };
  static const char *NAME_STR[ParserType::FTP_MAX + 1];

public:
  ObFTParser() : parser_name_(), parser_version_(-1) {}
  ~ObFTParser() = default;
  int parse_from_str(const char *plugin_name, const int64_t buf_len);
  int serialize_to_str(char *buf, const int64_t buf_len);

#define FT_PARSER_TYPE(fts_type, parser_name)                          \
  OB_INLINE bool is_##parser_name() const {                            \
    ParserType type = fts_type;                                        \
    return share::ObPluginName(NAME_STR[type]) == parser_name_;        \
  }
  FTS_BUILD_IN_PARSER_LIST
#undef FT_PARSER_TYPE

  OB_INLINE const share::ObPluginName &get_parser_name() const { return parser_name_; }
  OB_INLINE int64_t get_parser_version() const { return parser_version_; }
  OB_INLINE bool is_valid() const { return parser_name_.is_valid() && parser_version_ >= 0; }
  OB_INLINE bool is_type_before_4_3_5_1() const { return is_space() || is_beng() || is_ngram(); }
  // 内置解析器可以安全暴露给后续复用路径；外部插件仍遵守原有一次 segment 一次释放的 ABI。
  OB_INLINE bool is_builtin_parser() const
  {
    return is_space() || is_ngram() || is_beng() || is_ik() || is_ngram2();
  }
  OB_INLINE void set_name_and_version(const share::ObPluginName &name, const int64_t version)
  {
    parser_name_ = name;
    parser_version_ = version;
  }
  OB_INLINE bool operator ==(const ObFTParser &other) const
  {
    bool is_equal = true;
    if (this != &other) {
      is_equal = parser_name_ == other.get_parser_name() && parser_version_ == other.parser_version_;
    }
    return is_equal;
  }
  OB_INLINE bool operator !=(const ObFTParser &other) const { return !(*this == other); }
  TO_STRING_KV(K_(parser_name), K_(parser_version));
private:
  share::ObPluginName parser_name_;
  int64_t parser_version_;
};

class ObFTParsePluginData final
{
public:
  ObFTParsePluginData()
      : stop_token_checker_gen_(nullptr), dict_hub_(nullptr), handler_allocator_(), is_inited_(false)
  {}
  ~ObFTParsePluginData();

  /**
   * create a process global instance
   */
  static int  init_global();
  static void deinit_global();
  static ObFTParsePluginData &instance();

  int init();
  void destroy();

public:
  // checker 只借用进程级只读表，调用方不得跨越 deinit_global 生命周期保存。
  int get_stop_token_checker(const ObCollationType coll,
                             ObStopTokenChecker &stop_token_checker);
  int get_dict_hub(ObFTDictHub *&hub);

private:
  int init_stop_token_checker_gen();
  int init_dict_hub();

private:
  ObStopTokenCheckerGen *stop_token_checker_gen_;
  ObFTDictHub *dict_hub_;
  common::ObFIFOAllocator handler_allocator_;
  bool is_inited_;
};

class ObFTParseHelper final
{
public:
  ObFTParseHelper();
  ~ObFTParseHelper();

  /**
   * initialize fulltext parse helper
   *
   * @param[in] allocator
   * @param[in] parser_name, which consists of two parts name and version.
   *                         e.g. default_parser.1
   *                                   |         |
   *                            parse name   paser version
   * @param[in] parser_properties, which is a parser configuration in JSON format.
   *                         e.g.  {
   *                                 "min_token_size":2,
   *                                 "max_token_size":84,
   *                                 "ngram_token_size":2,
   *                                 "stopword_table":"default",
   *                                 "dict_table":"none",
   *                                 "quanitfier_table":"none"
   *                               }
   *
   * @return error code
   */
  int init(
      common::ObIAllocator *allocator,
      const common::ObString &plugin_name,
      const common::ObString &plugin_properties);
  // need_position_list 是 seekdb 单机适配入口；Task 4 可直接映射 phrase 索引类型，避免引入上游分布式 schema 依赖。
  int init(
      common::ObIAllocator *allocator,
      const common::ObString &plugin_name,
      const common::ObString &plugin_properties,
      const bool need_position_list);
  /**
   * Split document into multiple words
   *
   * @param[in] type, collation type for fulltext
   * @param[in] fulltext
   * @param[in] fulltext_len, length of the fulltext
   * @param[out] doc_length, length of document by word count
   * @param[out] words, word lists after segment
   */
  int segment(
      const common::ObObjMeta &meta,
      const char *fulltext,
      const int64_t fulltext_len,
      int64_t &doc_length,
      ObFTTokenMap &ft_token_map);
  // 兼容尚由 Task 4 迁移的构建调用点；内部仍走同一 token 热路径，再投影为旧词频布局。
  int segment(
      const common::ObObjMeta &meta,
      const char *fulltext,
      const int64_t fulltext_len,
      int64_t &doc_length,
      ObFTWordMap &words);
  int check_is_the_same(
      const common::ObString &plugin_name,
      const common::ObString &plugin_properties,
      bool &is_same) const;
  int check_is_the_same(
      const common::ObString &plugin_name,
      const common::ObString &plugin_properties,
      const bool need_position_list,
      bool &is_same) const;
  /**
   * Make json document for fulltext search
   *
   * @param[in] words, word lists after segment
   * @param[in] doc_length, length of document by word count
   * @param[out] json_root, json document
   */
  int make_detail_json(
      const ObFTTokenMap &ft_token_map,
      const int64_t doc_length,
      common::ObIJsonBase *&json_root);
  int make_detail_json(
      const ObFTWordMap &words,
      const int64_t doc_length,
      common::ObIJsonBase *&json_root);

  /**
   * Make json document for fulltext search
   *
   * @param[in] words, word lists after segment
   * @param[out] json_root, json document
   */
  int make_token_array_json(
      const ObFTTokenMap &ft_token_map,
      common::ObIJsonBase *&json_root);
  int make_token_array_json(
      const ObFTWordMap &words,
      common::ObIJsonBase *&json_root);

  void reset();

  const ObFTParser &get_parser_name() const { return parser_name_; }
  plugin::ObPluginParam *get_plugin_param() const { return plugin_param_; }
  const ObFTParserProperty &get_parser_property() const { return parser_property_; }
  const plugin::ObIFTParserDesc *get_parser_desc() const { return parser_desc_; }
  const ObProcessTokenFlag &get_process_token_flags() const { return process_token_flag_; }
  bool is_builtin_parser() const { return parser_name_.is_builtin_parser(); }
  // 仅用于诊断内置解析器复用状态；返回值不转移所有权，调用方不得保存到 helper.reset() 之后。
  const ObIFTParser *get_cached_builtin_parser() const { return cached_builtin_parser_; }

  TO_STRING_KV(KP_(allocator), K_(parser_name), KP_(parser_desc), K_(need_position_list), K_(is_inited));

private:
  int set_process_token_flag(const plugin::ObIFTParserDesc &ftparser_desc);
  // 仍通过原 descriptor 释放，确保内置解析器的 allocator、词典和插件生命周期保持一致。
  void destroy_cached_builtin_parser_();
private:
  common::ObIAllocator *allocator_;
  plugin::ObIFTParserDesc *parser_desc_;
  plugin::ObPluginParam *plugin_param_;
  ObFTParser parser_name_;
  ObProcessTokenFlag process_token_flag_;
  ObFTParserProperty parser_property_;
  // 内置解析器的对象与词典元数据独立于调用方的逐文档 arena，避免行缓存复用时产生悬空指针。
  common::ObArenaAllocator parser_metadata_allocator_;
  // 只缓存经 RTTI 确认的内置解析器；外部插件继续沿用一次 segment/一次释放的 ABI。
  ObIFTParser *cached_builtin_parser_;
  bool need_position_list_;
  bool is_inited_;

private:
  static constexpr const char *ENTRY_NAME_DOC_LEN = "doc_len";
  static constexpr const char *ENTRY_NAME_TOKENS = "tokens";
  DISALLOW_COPY_AND_ASSIGN(ObFTParseHelper);
};

} // end namespace storage
} // end namespace oceanbase

#endif // OB_FTS_PLUGIN_HELPER_H_
