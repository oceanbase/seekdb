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

#ifndef OB_BENG_FT_PARSER_H_
#define OB_BENG_FT_PARSER_H_

#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/allocator/page_arena.h"
#include "share/text_analysis/ob_text_analyzer.h"
#include "plugin/interface/ob_plugin_ftparser_intf.h"
#include "storage/fts/ob_i_reusable_ft_parser.h"

namespace oceanbase
{
namespace storage
{

class ObBEngFTParser final : public ObIReusableFTParser
{
public:
  static const int64_t FT_MIN_WORD_LEN = 3;
  static const int64_t FT_MAX_WORD_LEN = 84;
public:
  explicit ObBEngFTParser(common::ObIAllocator &allocator)
    : metadata_allocator_(allocator),
      scratch_allocator_("BEngParserData"),
      analysis_ctx_(),
      english_analyzer_(),
      doc_(),
      token_stream_(nullptr),
      ascii_cur_(nullptr),
      ascii_end_(nullptr),
      use_ascii_fast_path_(false),
      analyzer_inited_(false),
      is_inited_(false)
  {}
  ~ObBEngFTParser() { reset(); }

  int init(plugin::ObFTParserParam *param);
  int reuse_parser(const char *fulltext, const int64_t fulltext_len) override;
  void reset();
  virtual int get_next_token(
      const char *&word,
      int64_t &word_len,
      int64_t &char_len,
      int64_t &word_freq) override;

  VIRTUAL_TO_STRING_KV(K_(analysis_ctx), K_(english_analyzer), KP_(token_stream), K_(is_inited));
private:
  int segment(
      const common::ObDatum &doc,
      share::ObITokenStream *&token_stream);
  int prepare_document(const char *fulltext, const int64_t fulltext_len);
  int init_analyzer();
  int get_next_ascii_token(const char *&word,
                           int64_t &word_len,
                           int64_t &char_len,
                           int64_t &word_freq);
  bool is_ascii_document(const char *fulltext, const int64_t fulltext_len) const;
  bool is_ascii_delimiter(const char ch) const;
private:
  common::ObIAllocator &metadata_allocator_;
  common::ObArenaAllocator scratch_allocator_;
  share::ObTextAnalysisCtx analysis_ctx_;
  share::ObEnglishTextAnalyzer english_analyzer_;
  common::ObDatum doc_;
  share::ObITokenStream *token_stream_;
  const char *ascii_cur_;
  const char *ascii_end_;
  bool use_ascii_fast_path_;
  bool analyzer_inited_;
  bool is_inited_;

  DISALLOW_COPY_AND_ASSIGN(ObBEngFTParser);
};

class ObBasicEnglishFTParserDesc final : public plugin::ObIFTParserDesc
{
public:
  ObBasicEnglishFTParserDesc();
  virtual ~ObBasicEnglishFTParserDesc() = default;
  virtual int init(plugin::ObPluginParam *param) override;
  virtual int deinit(plugin::ObPluginParam *param) override;
  virtual int segment(plugin::ObFTParserParam *param, plugin::ObITokenIterator *&iter) const override;
  virtual void free_token_iter(plugin::ObFTParserParam *param, plugin::ObITokenIterator *&iter) const override;
  virtual int get_add_word_flag(ObAddWordFlag &flag) const override;
  OB_INLINE void reset() { is_inited_ = false; }
private:
  bool is_inited_;
};

} // end namespace storage
} // end namespace oceanbase

#endif // OB_BENG_FT_PARSER_H_
