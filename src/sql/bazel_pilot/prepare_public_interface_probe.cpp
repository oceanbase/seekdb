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

// PROTOTYPE: compile-only consumer of engine/prepare's public interface.

#include "sql/engine/prepare/ob_deallocate_executor.h"
#include "sql/engine/prepare/ob_execute_executor.h"
#include "sql/engine/prepare/ob_prepare_executor.h"

namespace oceanbase
{
namespace sql
{

void bazel_pilot_accept_prepare_interface(
    ObDeallocateExecutor *,
    ObExecuteExecutor *,
    ObPrepareExecutor *,
    ObDeallocateStmt *,
    ObExecuteStmt *,
    ObPrepareStmt *)
{
}

} // namespace sql
} // namespace oceanbase
