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

#ifndef OCEANBASE_SHARE_IVF_CACHE_UTIL_H_
#define OCEANBASE_SHARE_IVF_CACHE_UTIL_H_
#include "share/vector_index/ob_vector_index_ivf_cache_mgr.h"
#include "share/vector_index/ob_vector_index_util.h"

namespace oceanbase
{
namespace share
{
class ObIvfCacheUtil
{
public:
  static int is_cache_writable(const ObLSID &ls_id, int64_t table_id, const ObTabletID &tablet_id,
                               const ObVectorIndexParam &vec_param, int64_t dim, bool &is_writable);
  static int scan_and_write_ivf_cent_cache(ObPluginVectorIndexService &service,
                                           const ObTableID &table_id, const ObTabletID &tablet_id,
                                           ObIvfCentCache &cent_cache);

private:
  class ObIvfWriteCacheFunc
  {
  public:
    ObIvfWriteCacheFunc(ObIvfCentCache &cent_cache) : cent_cache_(cent_cache), cent_idx_(0) {}
    int operator()(int64_t dim, float *data);

  private:
    ObIvfCentCache &cent_cache_;
    int64_t cent_idx_;
  };
  DISALLOW_COPY_AND_ASSIGN(ObIvfCacheUtil);
};

}  // namespace share
}  // namespace oceanbase
#endif  // OCEANBASE_SHARE_IVF_CACHE_UTIL_H_
