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

#ifndef OCEANBASE_QUERY_VECTOR_INDEX_SERVICE_H_
#define OCEANBASE_QUERY_VECTOR_INDEX_SERVICE_H_

#include "query/vector/ob_vector_index_adaptor.h"
#include "query/vector/ob_vector_index_cache.h"

namespace oceanbase
{
namespace query
{

// SQL-facing data-plane seam.  The concrete tenant service remains owned by
// Observer; Query owns the contract consumed by SQL.
class ObIVectorIndexService
{
public:
  virtual ~ObIVectorIndexService() = default;

  virtual int acquire_adapter_guard(
      share::ObVectorIndexAcquireCtx &ctx,
      share::ObPluginVectorIndexAdapterGuard &adapter_guard,
      ObString *vec_index_param = nullptr,
      int64_t dim = 0) = 0;

  virtual int acquire_ivf_cache_mgr_guard(
      const share::ObIvfCacheMgrKey &key,
      const share::ObVectorIndexParam &vec_index_param,
      int64_t dim,
      int64_t table_id,
      share::ObIvfCacheMgrGuard &cache_mgr_guard) = 0;

  virtual int get_ivf_aux_info(
      uint64_t table_id,
      common::ObTabletID tablet_id,
      common::ObIAllocator &allocator,
      bool is_pq_type,
      common::ObIArray<float *> &aux_info,
      uint64_t &center_prefix) = 0;

  virtual int get_leader_flag(bool &is_leader) = 0;
  virtual int query_need_refresh_memdata(
      share::ObPluginVectorIndexAdaptor *adapter,
      const common::ObLobReadOptions &lob_read_options) = 0;
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_VECTOR_INDEX_SERVICE_H_
