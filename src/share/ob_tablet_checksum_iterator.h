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

#ifndef OCEANBASE_SHARE_OB_TABLET_CHECKSUM_ITERATOR_H_
#define OCEANBASE_SHARE_OB_TABLET_CHECKSUM_ITERATOR_H_

#include "lib/list/ob_dlist.h"
#include "lib/string/ob_sql_string.h"
#include "share/schema/ob_schema_struct.h"
#include "share/ob_tablet_checksum_operator.h"

namespace oceanbase
{
namespace common
{
class ObISQLClient;
}
namespace share
{
class ObTabletLSPair;

class ObTabletChecksumIterator
{

public:
  ObTabletChecksumIterator() 
    : is_inited_(false), tenant_id_(OB_INVALID_TENANT_ID),
      compaction_scn_(), checksum_items_(), cur_idx_(0), 
      sql_proxy_(NULL)
  {}
  virtual ~ObTabletChecksumIterator() { reset(); }

  void reset();
  void reuse();


  void set_compaction_scn(const SCN &compaction_scn) { compaction_scn_ = compaction_scn; }

private:
  int fetch_next_batch();

private:
  // Keep BATCH_FETCH_COUNT consistent with MAX_BATCH_COUNT in ob_tablet_checksum_operator.h for efficiency.
  // E.g., if BATCH_FETCH_COUNT = 100 and MAX_BATCH_COUNT = 99, then it will launch two query in fetch_next_batch.
  // The second query only get one row, which is inefficient.
  static const int64_t BATCH_FETCH_COUNT = 99;

  bool is_inited_;
  uint64_t tenant_id_;
  SCN compaction_scn_;
  common::ObSEArray<ObTabletChecksumItem, BATCH_FETCH_COUNT> checksum_items_;
  int64_t cur_idx_;
  common::ObISQLClient *sql_proxy_;

  DISALLOW_COPY_AND_ASSIGN(ObTabletChecksumIterator);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_TABLET_CHECKSUM_ITERATOR_H_
