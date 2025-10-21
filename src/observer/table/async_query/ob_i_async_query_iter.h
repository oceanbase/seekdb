

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

#ifndef _OB_I_ASYNC_QUERY_ITER_H
#define _OB_I_ASYNC_QUERY_ITER_H

#include "observer/table/common/ob_table_common_struct.h"

namespace oceanbase
{
namespace table
{
class ObIAsyncQueryIter
{
public:
  ObIAsyncQueryIter() = default;
  virtual ~ObIAsyncQueryIter() = default;
  virtual int start(const ObTableQueryAsyncRequest &req, ObTableExecCtx &exec_ctx, ObTableQueryAsyncResult &result) = 0;
  virtual int next(ObTableExecCtx &exec_ctx, ObTableQueryAsyncResult &result) = 0;
  virtual int renew(ObTableQueryAsyncResult &result) = 0;
  virtual int end(ObTableQueryAsyncResult &result) = 0;
  virtual uint64_t get_session_time_out_ts() const = 0;
  virtual uint64_t get_lease_timeout_period() const = 0;
};

} // end of namespace table
} // end of namespace oceanbase

#endif // _OB_HBASE_COLUMN_FAMILY_SERVICE_H
