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

#ifndef OCEANBASE_SHARE_OB_LOCK_METADATA_SESSION_H_
#define OCEANBASE_SHARE_OB_LOCK_METADATA_SESSION_H_

#include <stdint.h>

#include "common/mysqlclient/ob_isql_client.h"
#include "lib/string/ob_sql_string.h"

namespace oceanbase
{
namespace share
{

// Neutral metadata-session port.  The table-lock data plane uses it to store
// lock ownership records; query supplies the production adapter.  Tests may
// supply an in-memory adapter without either side exposing implementation
// types across the seam.
class ObILockMetadataSession
{
public:
  virtual ~ObILockMetadataSession() = default;
  virtual bool is_valid() const = 0;
  virtual uint32_t server_session_id() const = 0;
  virtual int execute_write(const common::ObSqlString &sql,
                            int64_t &affected_rows) = 0;
  virtual int execute_read(const common::ObSqlString &sql,
                           common::ObISQLClient::ReadResult &result) = 0;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_LOCK_METADATA_SESSION_H_
