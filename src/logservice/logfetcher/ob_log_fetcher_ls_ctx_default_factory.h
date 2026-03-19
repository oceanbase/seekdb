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
#ifndef OCEANBASE_LOG_FECCHER_LS_CTX_DEFAULT_FACTORY_H_
#define OCEANBASE_LOG_FECCHER_LS_CTX_DEFAULT_FACTORY_H_

#include "ob_log_fetcher_ls_ctx_factory.h"  // ObILogFetcherLSCtxFactory
#include "lib/objectpool/ob_small_obj_pool.h"  // ObSmallObjPool

namespace oceanbase
{
namespace logfetcher
{
class ObLogFetcherLSCtxDefaultFactory : public ObILogFetcherLSCtxFactory
{
public:
  ObLogFetcherLSCtxDefaultFactory();
  virtual ~ObLogFetcherLSCtxDefaultFactory() { destroy(); }
  int init(const uint64_t tenant_id);
  virtual void destroy();

public:
  virtual int alloc(LSFetchCtx *&ptr);
  virtual int free(LSFetchCtx *ptr);

private:
  typedef common::ObSmallObjPool<LSFetchCtx> LSFetchCtxPool;
  static const int64_t LS_CTX_MAX_CACHED_COUNT = 200;
  static const int64_t LS_CTX_POOL_BLOCK_SIZE = 1L << 24;

  bool is_inited_;
  LSFetchCtxPool ctx_pool_;
};

}; // end namespace logfetcher
}; // end namespace oceanbase
#endif
