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

#include "lib/allocator/page_arena.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "share/text_analysis/ob_text_analyzer.h"
#include "plugin/interface/ob_plugin_ftparser_intf.h"

namespace oceanbase
{
namespace storage
{

class ObBEngFTParser final : public plugin::ObITokenIterator
{
public:
  static const int64_t FT_MIN_WORD_LEN = 3;
  static const int64_t FT_MAX_WORD_LEN = 84;
public:
  ObBEngFTParser()
    : analyzer_allocator_(lib::ObMemAttr("BEngParserMeta")),
      scratch_allocator_(lib::ObMemAttr("BEngParserTmp")),
      token_allocator_(&scratch_allocator_),
      analysis_ctx_(),
      english_analyzer_(),
      doc_(),
      token_stream_(nullptr),
      is_inited_(false)
  {}
  // Fallback/reentrant instances must put token bytes in the caller-owned
  // document arena; their iterator can be destroyed before the word map is
  // materialized.
  explicit ObBEngFTParser(common::ObIAllocator &token_allocator)
    : analyzer_allocator_(lib::ObMemAttr("BEngParserMeta")),
      scratch_allocator_(lib::ObMemAttr("BEngParserTmp")),
      token_allocator_(&token_allocator),
      analysis_ctx_(),
      english_analyzer_(),
      doc_(),
      token_stream_(nullptr),
      is_inited_(false)
  {}
  ~ObBEngFTParser() { reset(); }

  int init(plugin::ObFTParserParam *param);
  int reuse(plugin::ObFTParserParam *param);
  void reset();
  OB_INLINE bool is_inited() const { return is_inited_; }
  virtual int get_next_token(
      const char *&word,
      int64_t &word_len,
      int64_t &char_len,
      int64_t &word_freq) override;

  VIRTUAL_TO_STRING_KV(K_(analysis_ctx), K_(english_analyzer), KP_(token_stream), K_(is_inited));
private:
  int init_analyzer(const ObCharsetInfo *cs);
  int open_document(plugin::ObFTParserParam *param);
  int segment(
      const common::ObDatum &doc,
      share::ObITokenStream *&token_stream);
private:
  // The analyzer pipeline survives across documents. Token copies only need
  // to survive until the caller materializes the current document's word map.
  common::ObArenaAllocator analyzer_allocator_;
  common::ObArenaAllocator scratch_allocator_;
  common::ObIAllocator *token_allocator_;
  share::ObTextAnalysisCtx analysis_ctx_;
  share::ObEnglishTextAnalyzer english_analyzer_;
  common::ObDatum doc_;
  share::ObITokenStream *token_stream_;
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
