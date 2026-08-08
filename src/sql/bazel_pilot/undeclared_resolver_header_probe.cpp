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

// PROTOTYPE: this include must not compile without a direct module dependency.

#include "sql/resolver/prepare/ob_prepare_stmt.h"

namespace oceanbase
{
namespace sql
{

ObPrepareStmt *bazel_pilot_undeclared_prepare_stmt()
{
  return nullptr;
}

} // namespace sql
} // namespace oceanbase
