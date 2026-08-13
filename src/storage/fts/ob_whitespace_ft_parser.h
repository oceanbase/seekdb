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

#ifndef OB_WHITESPACE_FT_PARSER_H_
#define OB_WHITESPACE_FT_PARSER_H_

#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "share/text_analysis/ob_text_analyzer.h"
#include "data_plane/fts/ob_fts_parser.h"

namespace oceanbase
{
namespace storage
{

class ObSpaceFTParser final : public ObITokenIterator
{
public:
  ObSpaceFTParser();
  virtual ~ObSpaceFTParser();

  int init(ObFTParserParam *param);
  void reset();
  virtual int get_next_token(
      const char *&word,
      int64_t &word_len,
      int64_t &char_len,
      int64_t &word_freq) override;

  VIRTUAL_TO_STRING_KV(KP_(cs), KP_(start), KP_(next), KP_(end), K_(is_inited));
private:
  const ObCharsetInfo *cs_;
  const char *start_;
  const char *next_;
  const char *end_;
  bool is_inited_;
};

class ObWhiteSpaceFTParserDesc final : public ObIFTParserDesc
{
public:
  ObWhiteSpaceFTParserDesc();
  virtual ~ObWhiteSpaceFTParserDesc() = default;
  virtual int segment(ObFTParserParam *param, ObITokenIterator *&iter) const override;
  virtual void free_token_iter(ObFTParserParam *param, ObITokenIterator *&iter) const override;
  virtual int get_add_word_flag(ObAddWordFlag &flag) const override;
};

} // end namespace storage
} // end namespace oceanbase

#endif // OB_WHITESPACE_FT_PARSER_H_
