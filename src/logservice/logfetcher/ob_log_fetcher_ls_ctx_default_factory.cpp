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
#define USING_LOG_PREFIX OBLOG

#include "ob_log_fetcher_ls_ctx_default_factory.h"

namespace oceanbase
{
namespace logfetcher
{
ObLogFetcherLSCtxDefaultFactory::ObLogFetcherLSCtxDefaultFactory() :
    is_inited_(false),
    ctx_pool_()
{}

int ObLogFetcherLSCtxDefaultFactory::init(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("init twice", KR(ret));
  } else if (OB_FAIL(ctx_pool_.init(LS_CTX_MAX_CACHED_COUNT,
      ObModIds::OB_LOG_PART_FETCH_CTX_POOL,
      tenant_id,
      LS_CTX_POOL_BLOCK_SIZE))) {
    LOG_ERROR("init LSFetchCtxPool fail", KR(ret), LITERAL_K(LS_CTX_MAX_CACHED_COUNT),
        LITERAL_K(LS_CTX_POOL_BLOCK_SIZE));
  } else {
    is_inited_ = true;
  }

  return ret;
}

void ObLogFetcherLSCtxDefaultFactory::destroy()
{
  if (is_inited_) {
    ctx_pool_.destroy();
    is_inited_ = false;
  }
}

int ObLogFetcherLSCtxDefaultFactory::alloc(LSFetchCtx *&ptr)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogFetcherLSCtxDefaultFactory is not inited", KR(ret));
  } else if (OB_FAIL(ctx_pool_.alloc(ptr))) {
    LOG_ERROR("alloc LSFetchCtx fail", KR(ret));
  } else {}

  return ret;
}

int ObLogFetcherLSCtxDefaultFactory::free(LSFetchCtx *ptr)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogFetcherLSCtxDefaultFactory is not inited", KR(ret));
  } else if (OB_FAIL(ctx_pool_.free(ptr))) {
    LOG_ERROR("free LSFetchCtx fail", KR(ret));
  } else {}

  return ret;
}

} // namespace logfetcher
} // namespace oceanbase
