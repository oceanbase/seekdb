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

#ifndef _OCEANBASE_STORAGE_FTS_OB_FT_PARSER_CACHE_H_
#define _OCEANBASE_STORAGE_FTS_OB_FT_PARSER_CACHE_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/page_arena.h"
#include "lib/alloc/alloc_struct.h"
#include "plugin/interface/ob_plugin_ftparser_intf.h"

namespace oceanbase
{
namespace storage
{

class ObIKFTParser;
class ObBEngFTParser;
class ObFTDictHub;

/**
 * Thread-local cache of fully-assembled fulltext parser instances.
 *
 * Assembling an ik/beng parser (loading dict ranges from the kv cache, building
 * the dict wrapper objects and the processor chain) costs far more than running
 * one short document through it, and the assembly is identical for every call
 * with the same parser configuration. This cache keeps one assembled instance
 * per parser kind per thread and only rebuilds the tiny per-document state on
 * each segment call. Thread-local, so no locking is needed.
 *
 * Only builtin-dict configurations are cached: a custom dict table changes
 * content under ALTER SYSTEM REFRESH FULLTEXT DICT, and those calls keep using
 * the original single-shot path.
 */
class ObFTParserCache final
{
public:
  static ObFTParserCache &get_instance()
  {
    static thread_local ObFTParserCache instance;
    return instance;
  }

  // only builtin dict configurations are eligible for caching
  static bool is_ik_param_cacheable(const plugin::ObFTParserParam &param)
  {
    return param.ik_param_.main_dict_.empty()
        && param.ik_param_.quan_dict_.empty()
        && param.ik_param_.stopword_dict_.empty();
  }

  int acquire_ik_parser(ObFTDictHub *hub,
                        const plugin::ObFTParserParam &param,
                        ObIKFTParser *&parser);
  int release_ik_parser(ObIKFTParser *parser, common::ObIAllocator *doc_allocator);

  int acquire_beng_parser(const plugin::ObFTParserParam &param, ObBEngFTParser *&parser);
  int release_beng_parser(ObBEngFTParser *parser, common::ObIAllocator *doc_allocator);

private:
  ObFTParserCache() : arena_(lib::ObMemAttr("FtParserCache")), ik_parser_(NULL), beng_parser_(NULL) {}
  ~ObFTParserCache() { destroy(); }
  void destroy();

  DISALLOW_COPY_AND_ASSIGN(ObFTParserCache);

private:
  common::ObArenaAllocator arena_;
  ObIKFTParser *ik_parser_;
  ObBEngFTParser *beng_parser_;
};

} // end namespace storage
} // end namespace oceanbase

#endif /* _OCEANBASE_STORAGE_FTS_OB_FT_PARSER_CACHE_H_ */
