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

#ifndef OB_STORAGE_ESTIMATOR_H
#define OB_STORAGE_ESTIMATOR_H
#include "share/stat/ob_opt_stat_manager.h"
namespace oceanbase
{
using namespace common;
namespace common {
struct ObSimpleBatch;
struct ObEstRowCountRecord;
}
namespace storage {
class ObIPartitionGroupGuard;
class ObTableScanParam;
}
namespace obrpc {
struct ObEstPartArg;
struct ObEstPartRes;
struct ObEstPartArgElement;
struct ObEstPartResElement;
}
namespace sql
{

class ObStorageEstimator
{
public:
  ObStorageEstimator() {};

  static int estimate_row_count(const obrpc::ObEstPartArg &arg,
                                obrpc::ObEstPartRes &res);

  static int estimate_block_count_and_row_count(const obrpc::ObEstBlockArg &arg,
                                                obrpc::ObEstBlockRes &res);
  static int estimate_skip_rate(const obrpc::ObEstSkipRateArg &arg, obrpc::ObEstSkipRateRes &res);
private:

  // compute memtable whole range row counts
  static int estimate_memtable_row_count(const obrpc::ObEstPartArg &arg,
                                         int64_t &logical_row_count,
                                         int64_t &physical_row_count);

  /**
  * @brief storage_estimate_rowcount
  * estimate rowcount for an index access path using storage interface
  */
  static int storage_estimate_rowcount(const uint64_t tenant_id,
                                       storage::ObTableScanParam &param,
                                       const ObSimpleBatch &batch,
                                       obrpc::ObEstPartResElement &res);

  // do compute query range row counts
  // Through storage layer interface to get logical row and physical row information
  //@param[in] batch : query range collection
  //@param[in] table_scan_param: table scan parameter
  //@param[in] range_columns_count: index column count
  //@param[in] part_service: partition service
  static int storage_estimate_partition_batch_rowcount(
      const uint64_t tenant_id,
      const ObSimpleBatch &batch,
      storage::ObTableScanParam &table_scan_param,
      ObIArray<common::ObEstRowCountRecord> &est_records,
      double &logical_row_count,
      double &physical_row_count);

  /**
  * @brief storage_estimate_block_count_and_row_count
  * estimate the blockcount of tablet by using storage interface
  */
  static int storage_estimate_block_count_and_row_count(const obrpc::ObEstBlockArgElement &arg,
                                                        obrpc::ObEstBlockResElement &res);
  static int storage_estimate_skip_rate(const obrpc::ObEstSkipRateArgElement &arg,
                                        obrpc::ObEstSkipRateResElement &res);                                          
};

}
}

#endif // OB_STORAGE_ESTIMATOR_H
