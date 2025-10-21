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

#include "ob_query_iterator_factory.h"
#include "storage/access/ob_table_scan_iterator.h"
#include "ob_value_row_iterator.h"

namespace oceanbase
{
using namespace oceanbase::common;
namespace storage
{

int64_t ObQueryIteratorFactory::multi_scan_merge_alloc_count_ = 0;
int64_t ObQueryIteratorFactory::multi_scan_merge_release_count_ = 0;
int64_t ObQueryIteratorFactory::multi_get_merge_alloc_count_ = 0;
int64_t ObQueryIteratorFactory::multi_get_merge_release_count_ = 0;
int64_t ObQueryIteratorFactory::table_scan_alloc_count_ = 0;
int64_t ObQueryIteratorFactory::table_scan_release_count_ = 0;
int64_t ObQueryIteratorFactory::insert_dup_alloc_count_ = 0;
int64_t ObQueryIteratorFactory::insert_dup_release_count_ = 0;
int64_t ObQueryIteratorFactory::col_map_alloc_count_ = 0;
int64_t ObQueryIteratorFactory::col_map_release_count_ = 0;

void ObQueryIteratorFactory::print_count()
{
  static int64_t last_stat_time = 0;
  int64_t stat_time = ObTimeUtility::current_time();
  const int64_t stat_interval = 10000000;
  if (stat_time - ATOMIC_LOAD(&last_stat_time) > stat_interval) {
    STORAGE_LOG(INFO, "ObQueryIteratorFactory statistics",
        K_(multi_scan_merge_alloc_count), K_(multi_scan_merge_release_count),
        K_(multi_get_merge_alloc_count), K_(multi_get_merge_release_count),
        K_(table_scan_alloc_count), K_(table_scan_release_count),
        K_(insert_dup_alloc_count), K_(insert_dup_release_count),
        K_(col_map_alloc_count), K_(col_map_release_count));
    ATOMIC_STORE(&last_stat_time, stat_time);
  }
}




ObValueRowIterator *ObQueryIteratorFactory::get_insert_dup_iter()
{
  print_count();
  ObValueRowIterator *iter = rp_alloc(ObValueRowIterator, ObModIds::OB_VALUE_ROW_ITER);
  if (NULL != iter) {
    (void)ATOMIC_FAA(&insert_dup_alloc_count_, 1);
  }
  return iter;
}


// no need to invoked reset() in the following free_XXX,
// since rp_free() will invoke reset() for the corresponding object

void ObQueryIteratorFactory::free_insert_dup_iter(blocksstable::ObDatumRowIterator *iter)
{
  if (OB_LIKELY(NULL != iter)) {
    (void)ATOMIC_FAA(&insert_dup_release_count_, 1);
    rp_free(static_cast<ObValueRowIterator *>(iter), ObModIds::OB_VALUE_ROW_ITER);
    iter = NULL;
  }
}



} // namespace storage
} // namespace oceanbase
