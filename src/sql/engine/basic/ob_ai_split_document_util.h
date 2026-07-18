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

#ifndef OCEANBASE_SQL_ENGINE_BASIC_OB_AI_SPLIT_DOCUMENT_UTIL_H_
#define OCEANBASE_SQL_ENGINE_BASIC_OB_AI_SPLIT_DOCUMENT_UTIL_H_

#include "lib/string/ob_string.h"
#include "lib/container/ob_array.h"
#include "lib/allocator/ob_allocator.h"

namespace oceanbase
{
namespace sql
{

struct ObAiSplitChunk
{
  int64_t chunk_id_;
  int64_t chunk_offset_;
  int64_t chunk_length_;
  common::ObString chunk_text_;

  TO_STRING_KV(K_(chunk_id), K_(chunk_offset), K_(chunk_length), K_(chunk_text));
};

struct ObAiSplitDocumentParams
{
  enum SplitType { TEXT = 0, MARKDOWN = 1 };
  enum SplitBy { WORD = 0, SENTENCE = 1 };

  SplitType type_;
  SplitBy by_;
  int64_t max_;
  int64_t overlap_;

  ObAiSplitDocumentParams()
    : type_(MARKDOWN), by_(WORD), max_(256), overlap_(0) {}
};

struct ObAiSplitDocumentState
{
  common::ObSEArray<ObAiSplitChunk, 16> chunks_;
  int64_t current_idx_;

  ObAiSplitDocumentState() : current_idx_(-1) {}
};

class ObAiSplitDocumentUtil
{
public:
  static int parse_params(const common::ObString &params_json, ObAiSplitDocumentParams &params);
  static int split_document(const common::ObString &content,
                            const ObAiSplitDocumentParams &params,
                            common::ObIAllocator &alloc,
                            ObAiSplitDocumentState &state);
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_BASIC_OB_AI_SPLIT_DOCUMENT_UTIL_H_ */
