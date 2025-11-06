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

#include "observer/table_load/client/ob_table_direct_load_rpc_proxy.h"
#include "observer/table_load/ob_table_load_struct.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadClientTask;
class ObTableLoadClientTaskBrief;
class ObTableDirectLoadExecContext;

class ObTableLoadClientService
{
public:
  // client task api
  static int alloc_task(ObTableLoadClientTask *&client_task);
  static void revert_task(ObTableLoadClientTask *client_task);
  static int add_task(ObTableLoadClientTask *client_task);
  static int get_task(const ObTableLoadUniqueKey &key, ObTableLoadClientTask *&client_task);

  // client task brief api
  static int get_task_brief(const ObTableLoadUniqueKey &key,
                            ObTableLoadClientTaskBrief *&client_task_brief);
  static void revert_task_brief(ObTableLoadClientTaskBrief *client_task_brief);

  // for table direct load api
  static int direct_load_operate(ObTableDirectLoadExecContext &ctx,
                                 const table::ObTableDirectLoadRequest &request,
                                 table::ObTableDirectLoadResult &result)
  {
    return ObTableDirectLoadRpcProxy::dispatch(ctx, request, result);
  }

  static int64_t generate_task_id();

private:
  static int64_t next_task_sequence_;
};

} // namespace observer
} // namespace oceanbase
