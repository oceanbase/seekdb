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
#ifndef OCEANBASE_SHARE_VECTOR_INDEX_PARAM_H_
#define OCEANBASE_SHARE_VECTOR_INDEX_PARAM_H_

#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace share
{

struct ObVectorIndexQueryParam
{
public:
  OB_UNIS_VERSION(1);

public:
  ObVectorIndexQueryParam():
    flags_(0),
    ef_search_(0),
    refine_k_(0),
    ob_sparse_drop_ratio_search_(0),
    similarity_threshold_(0)
  {}
  virtual ~ObVectorIndexQueryParam() {}
  int assign(const ObVectorIndexQueryParam &other);
  bool is_valid() const { return flags_ > 0; }

  union {
    uint64_t flags_;
    struct {
      uint64_t is_set_ef_search_            : 1;
      uint64_t is_set_refine_k_             : 1;
      uint64_t is_set_drop_ratio_search_    : 1;
      uint64_t is_set_similarity_threshold_ : 1;
      uint64_t reserved_                    : 60;
    };
  };
  int32_t ef_search_;
  float refine_k_;
  float ob_sparse_drop_ratio_search_;
  float similarity_threshold_;

  TO_STRING_KV(K_(is_set_ef_search), K_(ef_search),
      K_(is_set_refine_k), K_(refine_k), K_(ob_sparse_drop_ratio_search), K_(is_set_similarity_threshold), K_(similarity_threshold), K_(reserved));

};

}  // namespace share
}  // namespace oceanbase

#endif
