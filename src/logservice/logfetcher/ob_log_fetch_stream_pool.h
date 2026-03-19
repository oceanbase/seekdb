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
#ifndef OCEANBASE_LOG_FETCHER_FETCH_STREAM_POOL_H__
#define OCEANBASE_LOG_FETCHER_FETCH_STREAM_POOL_H__

#include "lib/objectpool/ob_small_obj_pool.h"     // ObSmallObjPool

#include "ob_log_ls_fetch_stream.h"               // FetchStream

namespace oceanbase
{
namespace logfetcher
{

class IFetchStreamPool
{
public:
  virtual ~IFetchStreamPool() {}

public:
  virtual int alloc(FetchStream *&fs) = 0;
  virtual int free(FetchStream *fs) = 0;
};

////////////////////// FetchStreamPool ///////////////////
class FetchStreamPool : public IFetchStreamPool
{
  typedef common::ObSmallObjPool<FetchStream> PoolType;
  static const int64_t DEFAULT_BLOCK_SIZE = 1L << 24;

public:
  FetchStreamPool();
  virtual ~FetchStreamPool();

public:
  int alloc(FetchStream *&fs);
  int free(FetchStream *fs);
  void print_stat();

public:
  int init(const int64_t cached_fs_count);
  void destroy();

private:
  PoolType pool_;

private:
  DISALLOW_COPY_AND_ASSIGN(FetchStreamPool);
};

}
}

#endif
