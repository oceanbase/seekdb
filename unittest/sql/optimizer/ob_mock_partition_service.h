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

#ifndef _OB_MOCK_PARTITION_SERVICE_H_
#define _OB_MOCK_PARTITION_SERVICE_H_
#undef private
#undef protected
#include <gmock/gmock.h>
#define private public
#define protected public
#include "../../storage/mockcontainer/ob_partition_service.h"
#include "storage/access/ob_dml_param.h"
#include "share/ob_server_locality_cache.h"
#include "common/row/ob_row_iterator.h"

using namespace oceanbase;
namespace test
{
class MockPartitionService : public oceanbase::storage::ObPartitionService
{
public:
  MockPartitionService() {}
  virtual ~MockPartitionService() {}

  //int get_scan_cost(const oceanbase::storage::ObTableScanParam &param,
                    //oceanbase::storage::ObPartitionEst &cost_estimate)
  //{
    //UNUSED(param);
    //cost_estimate.logical_row_count_ = 100.0;
    //cost_estimate.physical_row_count_ = 100.0;
    //return OB_SUCCESS;
  //}

  int table_scan(
      common::ObVTableScanParam &param,
      common::ObNewRowIterator *&result)
  {
    UNUSED(param);
    int ret = OB_SUCCESS;
    ObObj *value = OB_NEW(ObObj, ObModIds::TEST);
    value->set_type(ObIntType);
    value->set_int(1);
    value->set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

    ObNewRow *row = OB_NEW(ObNewRow, ObModIds::TEST);
    row->cells_ = value;
    row->count_ = 1;

    ObSingleRowIteratorWrapper *single = OB_NEW(ObSingleRowIteratorWrapper, ObModIds::TEST);
    single->set_row(row);
    result = single;
    return ret;
  }

  int revert_scan_iter(common::ObNewRowIterator *iter)
  {
    UNUSED(iter);
    return OB_SUCCESS;
  }

  int insert_rows(const transaction::ObTransDesc &trans_desc,
                  const storage::ObDMLBaseParam &dml_param,
                  const common::ObIArray<uint64_t> &column_ids,
                  common::ObNewRowIterator *row_iter,
                  int64_t &affected_rows)
  {
    UNUSED(trans_desc);
    UNUSED(dml_param);
    UNUSED(column_ids);
    int ret = OB_SUCCESS;
    common::ObNewRow *row = NULL;
    while (OB_SUCCESS == (ret = row_iter->get_next_row(row)));
    affected_rows = 1;
    return OB_SUCCESS;
  }

  int update_rows(const transaction::ObTransDesc &trans_desc,
                  const storage::ObDMLBaseParam &dml_param,
                  const common::ObIArray<uint64_t> &column_ids,
                  const common::ObIArray< uint64_t> &updated_column_ids,
                  common::ObNewRowIterator *row_iter,
                  int64_t &affected_rows)
  {
    UNUSED(trans_desc);
    UNUSED(dml_param);
    UNUSED(column_ids);
    UNUSED(updated_column_ids);
    UNUSED(row_iter);
    affected_rows = 1;
    return OB_SUCCESS;
  }

  int delete_rows(const transaction::ObTransDesc &trans_desc,
                  const storage::ObDMLBaseParam &dml_param,
                  const common::ObIArray<uint64_t> &column_ids,
                  common::ObNewRowIterator *row_iter,
                  int64_t &affected_rows)
  {
    UNUSED(trans_desc);
    UNUSED(dml_param);
    UNUSED(column_ids);
    int ret = OB_SUCCESS;
    common::ObNewRow *row = NULL;
    while (OB_SUCCESS == (ret = row_iter->get_next_row(row)));
    affected_rows = 1;
    return OB_SUCCESS;
  }
};

} // end namespace test



#endif /* _OB_MOCK_PARTITION_SERVICE_H_ */
