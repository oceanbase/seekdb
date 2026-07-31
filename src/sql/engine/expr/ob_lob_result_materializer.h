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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_LOB_RESULT_MATERIALIZER_H_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_LOB_RESULT_MATERIALIZER_H_

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObObj;
}
namespace sql
{
class ObExecContext;
class ObSQLSessionInfo;

// Materializes a protocol-facing LOB value without exposing Observer's query
// driver to PL. The implementation remains inside SQL's expression runtime.
int materialize_lob_result(common::ObObj &value,
                           common::ObIAllocator *allocator,
                           const ObSQLSessionInfo &session_info);

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_EXPR_OB_LOB_RESULT_MATERIALIZER_H_
