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

#pragma once

#include "common/object/ob_obj_type.h"
#include "common/ob_tablet_id.h"
#include "share/vector/ob_i_vector.h"
#include "sql/engine/ob_batch_rows.h"

namespace oceanbase
{
namespace common
{
class ObDatum;
} // namespace common
namespace share
{
namespace schema
{
class ObColDesc;
} // namespace schema
class ObTabletCacheInterval;
} // namespace share
namespace storage
{
using common::ObTabletID;
class ObDirectLoadBatchRows;

class ObDirectLoadVectorUtils
{
public:
  static int new_vector(VectorFormat format, VecValueTypeClass value_tc, ObIAllocator &allocator,
                        common::ObIVector *&vector);
  static int prepare_vector(ObIVector *vector, const int64_t max_batch_size,
                            ObIAllocator &allocator);

  static int to_datum(common::ObIVector *vector, const int64_t idx, common::ObDatum &datum);

  static int check_rowkey_length(const ObDirectLoadBatchRows &batch_rows,
                                 const int64_t rowkey_column_count);
  static int check_rowkey_length(const ObDirectLoadBatchRows &batch_rows,
                                 const int64_t rowkey_column_count,
                                 const common::ObIArray<share::schema::ObColDesc> &col_descs);

  // tablet id vector, ginore null value
  static const VecValueTypeClass tablet_id_value_tc = VEC_TC_INTEGER;
  static int make_const_tablet_id_vector(const ObTabletID &tablet_id, ObIAllocator &allocator,
                                         common::ObIVector *&vector);
  static ObTabletID get_tablet_id(common::ObIVector *vector, const int64_t batch_idx);

  static bool check_all_tablet_id_is_same(const uint64_t *tablet_ids, const int64_t size);
  static bool check_is_same_tablet_id(const ObTabletID &tablet_id, common::ObIVector *vector,
                                      const int64_t size);
  static bool check_is_same_tablet_id(const ObTabletID &tablet_id, common::ObIVector *vector,
                                      const uint16_t *selector, const int64_t size);
  static bool check_is_same_tablet_id(const ObTabletID &tablet_id,
                                      const common::ObDatumVector &datum_vec, const int64_t size);
  static bool check_is_same_tablet_id(const ObTabletID &tablet_id,
                                      const common::ObDatumVector &datum_vec,
                                      const uint16_t *selector, const int64_t size);

  // hidden pk vector
  static int batch_fill_hidden_pk(common::ObIVector *vector, const int64_t start,
                                  const int64_t size, share::ObTabletCacheInterval &pk_interval);
  static int batch_fill_value(common::ObIVector *vector, const int64_t start,
                              const int64_t size, const int64_t value);

  // multi version vector
  static const VecValueTypeClass multi_version_value_tc = VEC_TC_INTEGER;
  static int make_const_multi_version_vector(const int64_t value, ObIAllocator &allocator,
                                             common::ObIVector *&vector);
private:
  static int new_vector_fixed(VecValueTypeClass value_tc, ObIAllocator &allocator,
                        common::ObIVector *&vector);
  static int new_vector_continuous(VecValueTypeClass value_tc, ObIAllocator &allocator,
                        common::ObIVector *&vector);
  static int new_vector_discrete(VecValueTypeClass value_tc, ObIAllocator &allocator,
                        common::ObIVector *&vector);
  static int new_vector_uniform(VecValueTypeClass value_tc, ObIAllocator &allocator,
                        common::ObIVector *&vector);
  static int new_vector_uniform_const(VecValueTypeClass value_tc, ObIAllocator &allocator,
                        common::ObIVector *&vector);
};

} // namespace storage
} // namespace oceanbase
