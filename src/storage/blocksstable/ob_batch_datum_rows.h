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

#ifndef OB_BATCH_DATUM_ROWS_H_
#define OB_BATCH_DATUM_ROWS_H_

#include "lib/container/ob_array.h"
#include "share/vector/ob_i_vector.h"
#include "storage/blocksstable/ob_datum_row.h"

namespace oceanbase
{
namespace blocksstable
{

class ObBatchDatumRows
{
public:
  ObBatchDatumRows() 
    : row_count_(0) 
  {
    vectors_.set_tenant_id(MTL_ID());
  }
  ~ObBatchDatumRows() {}
  void reset();

  OB_INLINE int64_t get_column_count() const { return vectors_.count(); }

  TO_STRING_KV(K_(row_flag), K_(mvcc_row_flag), K_(trans_id), K(vectors_.count()), K_(row_count));

public:
  // convert vectors_ to datum_row at row idx = idx
  // performance is low, use it in performance non sensitive position
  int to_datum_row(int64_t idx, ObDatumRow &datum_row) const;

public:
  ObDmlRowFlag row_flag_;
  ObMultiVersionRowFlag mvcc_row_flag_;
  transaction::ObTransID trans_id_;
  common::ObArray<common::ObIVector *> vectors_;
  int64_t row_count_;
};

} // namespace blocksstable
} // namespace oceanbase

#endif /* OB_BATCH_DATUM_ROWS_H_ */
