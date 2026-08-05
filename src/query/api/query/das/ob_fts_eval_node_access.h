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

#ifndef OCEANBASE_QUERY_API_DAS_OB_FTS_EVAL_NODE_ACCESS_H_
#define OCEANBASE_QUERY_API_DAS_OB_FTS_EVAL_NODE_ACCESS_H_

namespace oceanbase
{
namespace common
{
template <typename T> class ObIArray;
}
namespace sql
{
struct ObFtsEvalNode;
}
namespace query
{

void release_fts_eval_node(sql::ObFtsEvalNode *node);
int evaluate_fts_boolean(
    sql::ObFtsEvalNode *node,
    const common::ObIArray<double> &relevances,
    double &result);

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_DAS_OB_FTS_EVAL_NODE_ACCESS_H_
