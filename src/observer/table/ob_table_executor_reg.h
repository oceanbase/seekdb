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

#ifndef OCEANBASE_OBSERVER_OB_TABLE_EXECUTOR_REG_H
#define OCEANBASE_OBSERVER_OB_TABLE_EXECUTOR_REG_H
#include "ob_table_scan_executor.h"
#include "ob_table_insert_executor.h"
#include "ob_table_delete_executor.h"
#include "ob_table_update_executor.h"
#include "ob_table_insert_up_executor.h"
#include "ob_table_replace_executor.h"
#include "ob_table_lock_executor.h"
#include "ttl/ob_table_ttl_executor.h"

namespace oceanbase
{
namespace table
{

template <int>
struct ObTableApiExecutorTypeTraits
{
  constexpr static bool registered_ = false;
  typedef char Spec;
  typedef char Executor;
};

template <typename T>
struct ObTableApiExecutorTraits
{
  constexpr static int type_ = 0;
};

#define REGISTER_TABLE_API_EXECUTOR(type, spec, executor)        \
  template <> struct ObTableApiExecutorTypeTraits<type> {        \
    constexpr static bool registered_ = true;                    \
    typedef spec Spec;                                           \
    typedef executor Executor;                                   \
  };                                                             \
  template <> struct ObTableApiExecutorTraits<spec> {            \
    constexpr static int type_ = type;                           \
  };                                                             \
  template <> struct ObTableApiExecutorTraits<executor> {        \
    constexpr static int type_ = type;                           \
  };

// REGISTER_TABLE_API_EXECUTOR(executor_type, spec, executor)

REGISTER_TABLE_API_EXECUTOR(TABLE_API_EXEC_SCAN, ObTableApiScanSpec, ObTableApiScanExecutor);
REGISTER_TABLE_API_EXECUTOR(TABLE_API_EXEC_INSERT, ObTableApiInsertSpec, ObTableApiInsertExecutor);
REGISTER_TABLE_API_EXECUTOR(TABLE_API_EXEC_DELETE, ObTableApiDelSpec, ObTableApiDeleteExecutor);
REGISTER_TABLE_API_EXECUTOR(TABLE_API_EXEC_UPDATE, ObTableApiUpdateSpec, ObTableApiUpdateExecutor);
REGISTER_TABLE_API_EXECUTOR(TABLE_API_EXEC_INSERT_UP, ObTableApiInsertUpSpec, ObTableApiInsertUpExecutor);
REGISTER_TABLE_API_EXECUTOR(TABLE_API_EXEC_REPLACE, ObTableApiReplaceSpec, ObTableApiReplaceExecutor);
REGISTER_TABLE_API_EXECUTOR(TABLE_API_EXEC_LOCK, ObTableApiLockSpec, ObTableApiLockExecutor);
REGISTER_TABLE_API_EXECUTOR(TABLE_API_EXEC_TTL, ObTableApiTTLSpec, ObTableApiTTLExecutor);

#undef REGISTER_TABLE_API_EXECUTOR

} // end namespace table
} // end namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_TABLE_EXECUTOR_REG_H
