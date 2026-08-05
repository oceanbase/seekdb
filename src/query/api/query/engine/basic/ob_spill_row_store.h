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

#ifndef OCEANBASE_QUERY_API_ENGINE_BASIC_OB_SPILL_ROW_STORE_H_
#define OCEANBASE_QUERY_API_ENGINE_BASIC_OB_SPILL_ROW_STORE_H_

#include <stdint.h>
#include "share/vector/ob_bit_vector.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
template <typename T> class ObIArray;
}
namespace sql
{
class ObEvalCtx;
class ObExpr;
}
namespace query
{

struct ObSpillRowStore;
struct ObSpillRowStoreIterator;

int create_spill_row_store(
    common::ObIAllocator &allocator, ObSpillRowStore *&store);
void reset_spill_row_store(ObSpillRowStore *store);
int spill_row_store_add_batch(
    ObSpillRowStore *store,
    const common::ObIArray<sql::ObExpr *> &expressions,
    sql::ObEvalCtx &eval_ctx,
    const sql::ObBitVector &skip,
    int64_t batch_size,
    int64_t &stored_count);
int spill_row_store_add_row(
    ObSpillRowStore *store,
    const common::ObIArray<sql::ObExpr *> &expressions,
    sql::ObEvalCtx &eval_ctx);
int64_t spill_row_store_row_count(const ObSpillRowStore *store);

int create_spill_row_store_iterator(
    common::ObIAllocator &allocator, ObSpillRowStoreIterator *&iterator);
void reset_spill_row_store_iterator(ObSpillRowStoreIterator *iterator);
int init_spill_row_store_iterator(
    ObSpillRowStoreIterator *iterator, ObSpillRowStore *store);
int spill_row_store_next_row(
    ObSpillRowStoreIterator *iterator,
    sql::ObEvalCtx &eval_ctx,
    const common::ObIArray<sql::ObExpr *> &expressions);

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_ENGINE_BASIC_OB_SPILL_ROW_STORE_H_
