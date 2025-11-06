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

#pragma once

#include "sql/engine/cmd/ob_load_data_utils.h"

namespace oceanbase
{
namespace table
{
class ObTableLoadResultInfo;
} // namespace table
namespace observer
{
class ObTableLoadStoreCtx;
class ObTableLoadCoordinatorCtx;
class ObTableLoadParam;

class ObTableLoadErrorRowHandler
{
public:
  ObTableLoadErrorRowHandler();
  virtual ~ObTableLoadErrorRowHandler();
  int init(const ObTableLoadParam &param, table::ObTableLoadResultInfo &result_info,
           sql::ObLoadDataStat *job_stat);
  int handle_error_row(int error_code);
  int handle_error_row(int error_code, int64_t duplicate_row_count);
  TO_STRING_KV(K_(dup_action), K_(max_error_row_count), K_(error_row_count));
private:
  sql::ObLoadDupActionType dup_action_;
  uint64_t max_error_row_count_;
  table::ObTableLoadResultInfo *result_info_;
  sql::ObLoadDataStat *job_stat_;
  mutable lib::ObMutex mutex_;
  uint64_t error_row_count_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
