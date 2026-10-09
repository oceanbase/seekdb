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

#define USING_LOG_PREFIX STORAGE_FTS

#include "storage/fts/ob_ft_parser_cache.h"
#include "storage/fts/ob_ik_ft_parser.h"
#include "storage/fts/ob_beng_ft_parser.h"
#include "storage/fts/dict/ob_ft_dict_hub.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace storage
{

void ObFTParserCache::destroy()
{
  if (OB_NOT_NULL(ik_parser_)) {
    OB_DELETEx(ObIKFTParser, &arena_, ik_parser_);
  }
  if (OB_NOT_NULL(beng_parser_)) {
    OB_DELETEx(ObBEngFTParser, &arena_, beng_parser_);
  }
  arena_.reset();
}

int ObFTParserCache::acquire_ik_parser(ObFTDictHub *hub,
                                       const plugin::ObFTParserParam &param,
                                       ObIKFTParser *&parser)
{
  INIT_SUCC(ret);
  parser = NULL;
  for (int64_t attempt = 0; OB_SUCC(ret) && OB_ISNULL(parser) && attempt < 2; ++attempt) {
    if (OB_ISNULL(ik_parser_)) {
      ik_parser_ = OB_NEWx(ObIKFTParser, &arena_, arena_, hub);
      if (OB_ISNULL(ik_parser_)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to alloc ik parser", K(ret));
      } else if (OB_FAIL(ik_parser_->init_metadata(param))) {
        LOG_WARN("fail to init ik parser metadata", K(ret));
        OB_DELETEx(ObIKFTParser, &arena_, ik_parser_);
      }
    }
    if (OB_FAIL(ret) || OB_ISNULL(ik_parser_)) {
    } else if (OB_FAIL(ik_parser_->start_document(param))) {
      if (OB_NOT_SUPPORTED == ret) {
        // collation changed since the metadata was assembled, rebuild it once
        ret = OB_SUCCESS;
        OB_DELETEx(ObIKFTParser, &arena_, ik_parser_);
      } else {
        LOG_WARN("fail to start ik parser document", K(ret));
      }
    } else {
      parser = ik_parser_;
    }
  }
  return ret;
}

int ObFTParserCache::release_ik_parser(ObIKFTParser *parser, common::ObIAllocator *doc_allocator)
{
  INIT_SUCC(ret);
  if (OB_ISNULL(parser)) {
    // nothing to do
  } else if (parser == ik_parser_) {
    if (OB_FAIL(parser->end_document(doc_allocator))) {
      LOG_WARN("fail to end ik parser document", K(ret));
    }
  } else {
    // not the cached instance (defensive), destroy it like the original path
    parser->~ObIKFTParser();
    if (OB_NOT_NULL(doc_allocator)) {
      doc_allocator->free(parser);
    }
  }
  return ret;
}

int ObFTParserCache::acquire_beng_parser(const plugin::ObFTParserParam &param, ObBEngFTParser *&parser)
{
  INIT_SUCC(ret);
  parser = NULL;
  if (OB_ISNULL(beng_parser_)) {
    beng_parser_ = OB_NEWx(ObBEngFTParser, &arena_, arena_);
    if (OB_ISNULL(beng_parser_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc beng parser", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(beng_parser_->start_document(const_cast<plugin::ObFTParserParam *>(&param)))) {
    LOG_WARN("fail to start beng parser document", K(ret));
  } else {
    parser = beng_parser_;
  }
  return ret;
}

int ObFTParserCache::release_beng_parser(ObBEngFTParser *parser, common::ObIAllocator *doc_allocator)
{
  INIT_SUCC(ret);
  if (OB_ISNULL(parser)) {
    // nothing to do
  } else if (parser == beng_parser_) {
    if (OB_FAIL(parser->end_document())) {
      LOG_WARN("fail to end beng parser document", K(ret));
    }
  } else {
    parser->~ObBEngFTParser();
    if (OB_NOT_NULL(doc_allocator)) {
      doc_allocator->free(parser);
    }
  }
  return ret;
}

} // end namespace storage
} // end namespace oceanbase
