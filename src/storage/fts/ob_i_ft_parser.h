/*
 * Copyright (c) 2026 OceanBase.
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

#ifndef OCEANBASE_STORAGE_FTS_OB_I_FT_PARSER_H_
#define OCEANBASE_STORAGE_FTS_OB_I_FT_PARSER_H_

#include "plugin/interface/ob_plugin_ftparser_intf.h"

namespace oceanbase
{
namespace storage
{

// 内置全文解析器的统一复用接口；对象、词典和元数据由长生命周期 allocator 持有。
class ObIFTParser : public plugin::ObITokenIterator
{
public:
  ObIFTParser() = default;
  virtual ~ObIFTParser() = default;

  // 复用解析器处理下一篇文档，只重置逐文档状态，不销毁长生命周期字典和元数据。
  // 调用前必须保证上一文档返回的 token 已消费完，且不再引用即将回收的 scratch 内存。
  virtual int reuse_parser(const char *fulltext, const int64_t fulltext_len) = 0;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_FTS_OB_I_FT_PARSER_H_
