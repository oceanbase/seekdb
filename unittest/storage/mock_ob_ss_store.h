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

#ifndef MOCK_OB_SS_STORE_H_
#define MOCK_OB_SS_STORE_H_

#include <gmock/gmock.h>
#include "storage/ob_ss_store.h"

namespace oceanbase
{
namespace storage
{

class MockObSSStore : public ObSSStore
{
public:
  MockObSSStore() {}
  virtual ~MockObSSStore() {}

  MOCK_METHOD4(
      get,
      int(
        const storage::ObTableAccessParam &param,
        storage::ObTableAccessContext &context,
        const common::ObStoreRowkey &rowkey,
        ObStoreRowIterator *&row_iter));
  MOCK_METHOD4(
      scan,
      int(
        const ObTableAccessParam &param,
        ObTableAccessContext &context,
        const common::ObStoreRange &key_range,
        ObStoreRowIterator *&row_iter));
  MOCK_METHOD4(
      multi_get,
      int(
        const ObTableAccessParam &param,
        ObTableAccessContext &context,
        const common::ObIArray<common::ObStoreRowkey> &rowkeys,
        ObStoreRowIterator *&row_iter));
  MOCK_METHOD4(
      multi_scan,
      int(
        const ObTableAccessParam &param,
        ObTableAccessContext &context,
        const common::ObIArray<common::ObStoreRange> &ranges,
        ObStoreRowIterator *&row_iter));

  MOCK_METHOD5(
      estimate_get_cost,
      int (const common::ObQueryFlag query_flag,
        const uint64_t table_id,
        const common::ObIArray<common::ObStoreRowkey> &rowkeys,
        const common::ObIArray<share::schema::ObColDesc> &columns,
        ObPartitionEst &cost_metrics));

  MOCK_METHOD5(
      estimate_scan_cost,
      int (const common::ObQueryFlag query_flag,
        const uint64_t table_id,
        const common::ObStoreRange &key_range,
        const common::ObIArray<share::schema::ObColDesc> &columns,
        ObPartitionEst &cost_metrics));

   MOCK_METHOD5(
       estimate_multi_scan_cost,
       int (
         const common::ObQueryFlag query_flag,
         const uint64_t table_id,
         const common::ObIArray<common::ObStoreRange> &ranges,
         const common::ObIArray<share::schema::ObColDesc> &columns,
         ObPartitionEst &cost_metrics));


  //virtual enum storage::ObStoreType get_store_type() const { return MAJOR_SSTORE; }

  MOCK_CONST_METHOD0(
      get_store_type,
      enum storage::ObStoreType());

};

}
}



#endif /* MOCK_OB_SS_STORE_H_ */
