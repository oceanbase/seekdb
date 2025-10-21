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

#ifndef OB_NGRAM_FT_PARSER_H_
#define OB_NGRAM_FT_PARSER_H_

#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "plugin/interface/ob_plugin_ftparser_intf.h"
#include "storage/fts/utils/ob_ft_ngram_impl.h"

namespace oceanbase
{
namespace storage
{

class ObNgramFTParser final : public plugin::ObITokenIterator
{
public:
  ObNgramFTParser();
  virtual ~ObNgramFTParser();

  int init(plugin::ObFTParserParam *param);
  void reset();
  virtual int get_next_token(
      const char *&word,
      int64_t &word_len,
      int64_t &char_len,
      int64_t &word_freq) override;

  VIRTUAL_TO_STRING_KV(K_(is_inited));

private:
  ObFTNgramImpl ngram_impl_;
  bool is_inited_;
private:
  DISABLE_COPY_ASSIGN(ObNgramFTParser);
};

class ObNgramFTParserDesc final : public plugin::ObIFTParserDesc
{
public:
  ObNgramFTParserDesc();
  virtual ~ObNgramFTParserDesc() = default;
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

#endif // OB_NGRAM_FT_PARSER_H_
