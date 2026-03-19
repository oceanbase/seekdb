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
#ifndef OCEANBASE_LOG_FETCH_LOG_RPC_SPLITTER_H_
#define OCEANBASE_LOG_FETCH_LOG_RPC_SPLITTER_H_

#include "ob_log_fetch_log_rpc_req.h"

namespace oceanbase
{

namespace logfetcher
{

class IFetchLogRpcSplitter
{
public:
  virtual ~IFetchLogRpcSplitter() = 0;
  virtual int split(RawLogFileRpcRequest &rpc_rep) = 0;
  virtual int update_stat(const int32_t cur_valid_rpc_cnt) = 0;
  DECLARE_PURE_VIRTUAL_TO_STRING;
};

class ObLogFetchLogRpcSplitter: public IFetchLogRpcSplitter
{
  static constexpr int64_t STAT_WINDOW_SIZE = 4;
public:
  virtual int split(RawLogFileRpcRequest &rpc_rep);
  virtual int update_stat(const int32_t cur_valid_rpc_cnt);
public:
  explicit ObLogFetchLogRpcSplitter(const int32_t max_sub_rpc_cnt);
  ~ObLogFetchLogRpcSplitter() { destroy(); }
  void reset();
  void destroy() { reset(); }
  VIRTUAL_TO_STRING_KV(
    K(max_sub_rpc_cnt_)
  );
private:
  // must be the power of 2, and less than 64MB / 4K
  // related to the buffer size of rpc response
  int32_t max_sub_rpc_cnt_;
  int64_t cur_pos_;
  int32_t valid_rpc_cnt_window_[STAT_WINDOW_SIZE];
};

}

}

#endif