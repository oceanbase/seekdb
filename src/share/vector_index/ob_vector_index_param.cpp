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

#define USING_LOG_PREFIX SHARE

#include "ob_vector_index_param.h"

namespace oceanbase
{
namespace share
{

OB_SERIALIZE_MEMBER(ObVectorIndexQueryParam, flags_, ef_search_, refine_k_, ob_sparse_drop_ratio_search_, similarity_threshold_);

int ObVectorIndexQueryParam::assign(const ObVectorIndexQueryParam &other)
{
  int ret = OB_SUCCESS;
  flags_ = other.flags_;
  ef_search_ = other.ef_search_;
  refine_k_ = other.refine_k_;
  ob_sparse_drop_ratio_search_ = other.ob_sparse_drop_ratio_search_;
  similarity_threshold_ = other.similarity_threshold_;
  return ret;
}

}  // namespace share
}  // namespace oceanbase
