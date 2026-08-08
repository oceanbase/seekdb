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

#ifndef OCEANBASE_STORAGE_VECTOR_OB_I_VECTOR_INDEX_RUNTIME_H_
#define OCEANBASE_STORAGE_VECTOR_OB_I_VECTOR_INDEX_RUNTIME_H_

#include "query/vector/ob_vector_index_adaptor.h"
#include "query/vector/ob_vector_embedding_handler.h"
#include "storage/vector_index/ob_vector_kmeans_ctx.h"

namespace oceanbase
{
namespace share
{
struct ObIvfHelperKey final
{
  ObIvfHelperKey() : tablet_id_(), context_id_(OB_INVALID_ID) {}
  ObIvfHelperKey(const ObTabletID &tablet_id, int64_t context_id)
      : tablet_id_(tablet_id), context_id_(context_id) {}

  uint64_t hash() const
  {
    return tablet_id_.hash() + murmurhash(&context_id_, sizeof(context_id_), 0);
  }
  int hash(uint64_t &hash_val) const { hash_val = hash(); return OB_SUCCESS; }
  bool is_valid() const { return tablet_id_.is_valid() && context_id_ >= 0; }
  bool operator==(const ObIvfHelperKey &other) const
  {
    return tablet_id_ == other.tablet_id_ && context_id_ == other.context_id_;
  }
  TO_STRING_KV(K_(tablet_id), K_(context_id));

  common::ObTabletID tablet_id_;
  int64_t context_id_;
};
}

namespace storage
{
// Storage-facing vector runtime. Observer owns and composes the concrete
// plugin service, while Storage owns the contract it consumes.
class ObIVectorIndexRuntime
{
public:
  virtual ~ObIVectorIndexRuntime() = default;

  virtual int acquire_adapter_guard(
      common::ObTabletID tablet_id,
      share::schema::ObIndexType type,
      share::ObPluginVectorIndexAdapterGuard &adapter_guard,
      ObString *vec_index_param,
      int64_t dim) = 0;
  virtual int acquire_ivf_build_helper_guard(
      const share::ObIvfHelperKey &key,
      share::schema::ObIndexType type,
      share::ObIvfBuildHelperGuard &helper_guard,
      ObString &vec_index_param) = 0;
  virtual int erase_ivf_build_helper(
      const share::ObIvfHelperKey &key) = 0;
  virtual int get_embedding_task_handler(
      share::ObEmbeddingTaskHandler *&handler) = 0;
  virtual share::ObKmeansBuildTaskHandler &get_kmeans_build_handler() = 0;
  virtual lib::MemoryContext &get_memory_context() = 0;
  virtual uint64_t *get_all_vsag_use_mem() = 0;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_VECTOR_OB_I_VECTOR_INDEX_RUNTIME_H_
