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
#ifndef OCEANBASE_LOG_FECCHER_LS_CTX_FACTORY_H_
#define OCEANBASE_LOG_FECCHER_LS_CTX_FACTORY_H_

#include "ob_log_ls_fetch_ctx.h"  // LSFetchCtx

namespace oceanbase
{
namespace logfetcher
{
class ObILogFetcherLSCtxFactory
{
public:
  virtual ~ObILogFetcherLSCtxFactory() {}
  virtual void destroy() = 0;

public:
  virtual int alloc(LSFetchCtx *&ptr) = 0;
  virtual int free(LSFetchCtx *ptr) = 0;
};

}; // end namespace logfetcher
}; // end namespace oceanbase
#endif
