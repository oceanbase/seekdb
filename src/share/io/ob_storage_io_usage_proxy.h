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

#ifndef OCEANBASE_LIB_OB_STORAGE_IO_USAGE_PROXY_H
#define OCEANBASE_LIB_OB_STORAGE_IO_USAGE_PROXY_H

#include "lib/ob_define.h"
namespace oceanbase
{
namespace common
{
class ObMySQLTransaction;
class ObString;
class ObMySQLProxy;
}
namespace share
{
class ObStorageIOUsageProxy
{
public:
    ObStorageIOUsageProxy() {}
    ~ObStorageIOUsageProxy() {}
    int update_storage_io_usage(common::ObMySQLTransaction &trans,
                                const uint64_t tenant_id,
                                const int64_t storage_id,
                                const int64_t dest_id,
                                const ObString &storage_mod,
                                const ObString &type,
                                const int64_t total);
private:
    DISALLOW_COPY_AND_ASSIGN(ObStorageIOUsageProxy);
};

}
}
#endif
