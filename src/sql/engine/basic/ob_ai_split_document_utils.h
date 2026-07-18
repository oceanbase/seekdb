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

#ifndef OCEANBASE_SQL_ENGINE_BASIC_OB_AI_SPLIT_DOCUMENT_UTILS_H_
#define OCEANBASE_SQL_ENGINE_BASIC_OB_AI_SPLIT_DOCUMENT_UTILS_H_

#include "lib/container/ob_se_array.h"
#include "lib/string/ob_string.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace sql
{

struct ObAiSplitDocumentChunk
{
  ObAiSplitDocumentChunk()
    : chunk_id_(0),
      chunk_offset_(0),
      chunk_length_(0),
      chunk_text_()
  {}
  int64_t chunk_id_;
  int64_t chunk_offset_;
  int64_t chunk_length_;
  common::ObString chunk_text_;

  TO_STRING_KV(K_(chunk_id), K_(chunk_offset), K_(chunk_length), K_(chunk_text));
};

struct ObAiSplitDocumentIter
{
  common::ObSEArray<ObAiSplitDocumentChunk, 8> chunks_;
};

struct ObAiSplitDocumentParam
{
  ObAiSplitDocumentParam()
    : type_text_(false),
      by_sentence_(false),
      max_units_(256),
      overlap_(0)
  {}
  bool type_text_;
  bool by_sentence_;
  int64_t max_units_;
  int64_t overlap_;
};

class ObAiSplitDocumentUtils
{
public:
  static int split_document(const common::ObString &content,
                            const ObAiSplitDocumentParam &param,
                            common::ObIAllocator &alloc,
                            ObAiSplitDocumentIter &result);
  static int parse_param_json(const common::ObString &json_str,
                              ObAiSplitDocumentParam &param);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_BASIC_OB_AI_SPLIT_DOCUMENT_UTILS_H_
