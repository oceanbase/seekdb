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

#ifndef OCEANBASE_STORAGE_TRUNCATE_INFO_OB_TRUNCATE_FILTER_EVALUATOR_H_
#define OCEANBASE_STORAGE_TRUNCATE_INFO_OB_TRUNCATE_FILTER_EVALUATOR_H_

#include "lib/allocator/page_arena.h"
#include "query/engine/basic/ob_external_pushdown_filter.h"

namespace oceanbase
{
namespace common
{
template <typename T> class ObIArray;
}
namespace share
{
namespace schema
{
struct ObColDesc;
}
}
namespace blocksstable
{
struct ObDatumRow;
struct ObStorageDatum;
}
namespace storage
{
struct ObTruncateInfoArray;

// Storage-owned compiled truncate semantics.  Its public surface is a row
// decision plus the generic query attachment operation; predicate structure,
// storage datum layout, and reader-specific batch work stay private.
class ObTruncateFilterEvaluator final : public sql::ObIExternalPushdownFilter
{
public:
  ObTruncateFilterEvaluator();
  ~ObTruncateFilterEvaluator();

  int init(
      const int64_t schema_rowkey_count,
      const common::ObIArray<share::schema::ObColDesc> &columns,
      const ObTruncateInfoArray &truncate_infos);
  int switch_info(
      const int64_t schema_rowkey_count,
      const common::ObIArray<share::schema::ObColDesc> &columns,
      const ObTruncateInfoArray &truncate_infos);
  void reuse();

  int filter(const blocksstable::ObDatumRow &row, bool &filtered) const;
  int filter_projected(
      const blocksstable::ObStorageDatum *datums,
      const int64_t datum_count,
      bool &filtered) const;

  int64_t referenced_column_count() const;
  int32_t referenced_column(const int64_t index) const;
  bool is_valid() const { return nullptr != impl_; }

  int execute(sql::ObExternalFilterExecutionContext &context) override;

private:
  struct Impl;
  int rebuild(
      const int64_t schema_rowkey_count,
      const common::ObIArray<share::schema::ObColDesc> &columns,
      const ObTruncateInfoArray &truncate_infos);
  void reset_impl();

private:
  common::ObArenaAllocator allocator_;
  Impl *impl_;
  DISALLOW_COPY_AND_ASSIGN(ObTruncateFilterEvaluator);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_TRUNCATE_INFO_OB_TRUNCATE_FILTER_EVALUATOR_H_
