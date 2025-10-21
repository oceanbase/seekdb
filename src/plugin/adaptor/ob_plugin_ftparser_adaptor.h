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

#pragma once

#include "oceanbase/ob_plugin_ftparser.h"
#include "lib/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/oblog/ob_log_module.h"
#include "plugin/adaptor/ob_plugin_adaptor.h"
#include "plugin/interface/ob_plugin_ftparser_intf.h"

class ObCharsetInfo;

namespace oceanbase {
namespace plugin {

class ObPluginParam;

class ObTokenIteratorAdaptor final : public ObITokenIterator
{
public:
  ObTokenIteratorAdaptor(const ObPluginFTParser &ftparser, ObFTParserParam *param);
  virtual ~ObTokenIteratorAdaptor() = default;

  virtual int get_next_token(
      const char *&word,
      int64_t &word_len,
      int64_t &char_cnt,
      int64_t &word_freq) override;

  TO_STRING_KV(KP(param_));

private:
  const ObPluginFTParser &ftparser_;
  ObFTParserParam        *param_ = nullptr;
};

class ObFtParserAdaptor final : public ObIFTParserDesc
{
public:
  ObFtParserAdaptor() = default;
  virtual ~ObFtParserAdaptor() = default;

  int init_adaptor(const ObPluginFTParser &ftparser, int64_t ftparser_sizeof);

  virtual int init(ObPluginParam *param) override;
  virtual int deinit(ObPluginParam *param) override;
  
  virtual int segment(ObFTParserParam *param, ObITokenIterator *&iter) const override;
  virtual void free_token_iter(ObFTParserParam *param, ObITokenIterator *&iter) const override;
  virtual int get_add_word_flag(storage::ObAddWordFlag &flag) const override;
  virtual int check_if_charset_supported(const ObCharsetInfo *cs) const override;

private:
  bool             inited_ = false;
  ObPluginFTParser ftparser_;
};

} // namespace plugin
} // namespace oceanbase
