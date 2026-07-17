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

#ifndef OCEANBASE_SQL_OPTIMIZER_OB_OPT_EST_PARAMETER_
#define OCEANBASE_SQL_OPTIMIZER_OB_OPT_EST_PARAMETER_
#include "common/object/ob_obj_type.h"
#define DEFAULT_CPU_SPEED 2500
#define DEFAULT_DISK_SEQ_READ_SPEED 1024
#define DEFAULT_DISK_RND_READ_SPEED 512
#define DEFAULT_NETWORK_SPEED 156
#define DEFAULT_MACR_BLOCK_SIZE  (16 * 1024)

namespace oceanbase
{
namespace sql
{
class OptSystemStat;

class ObOptCostModelParameter {
public:
  explicit ObOptCostModelParameter(
    const double DEFAULT_CPU_TUPLE_COST,
    const double DEFAULT_TABLE_SCAN_CPU_TUPLE_COST,
    const double DEFAULT_MICRO_BLOCK_SEQ_COST,
    const double DEFAULT_MICRO_BLOCK_RND_COST,
    const double DEFAULT_FETCH_ROW_RND_COST,
    const double DEFAULT_CMP_GEO_COST,
    const double DEFAULT_MATERIALIZE_PER_BYTE_WRITE_COST,
    const double DEFAULT_READ_MATERIALIZED_PER_ROW_COST,
    const double DEFAULT_PER_AGGR_FUNC_COST,
    const double DEFAULT_PER_WIN_FUNC_COST,
    const double DEFAULT_CPU_OPERATOR_COST,
    const double DEFAULT_JOIN_PER_ROW_COST,
    const double DEFAULT_BUILD_HASH_PER_ROW_COST,
    const double DEFAULT_PROBE_HASH_PER_ROW_COST,
    const double DEFAULT_RESCAN_COST,
    const double DEFAULT_NETWORK_SER_PER_BYTE_COST,
    const double DEFAULT_NETWORK_DESER_PER_BYTE_COST,
    const double DEFAULT_NETWORK_TRANS_PER_BYTE_COST,
    const double DEFAULT_PX_RESCAN_PER_ROW_COST,
    const double DEFAULT_PX_BATCH_RESCAN_PER_ROW_COST,
    const double DEFAULT_DAS_RESCAN_PER_ROW_RPC_COST,
    const double DEFAULT_DAS_BATCH_RESCAN_PER_ROW_RPC_COST,
    const double DEFAULT_NL_SCAN_COST,
    const double DEFAULT_BATCH_NL_SCAN_COST,
    const double DEFAULT_NL_GET_COST,
    const double DEFAULT_BATCH_NL_GET_COST,
    const double DEFAULT_TABLE_LOOPUP_PER_ROW_RPC_COST,
    const double DEFAULT_INSERT_PER_ROW_COST,
    const double DEFAULT_INSERT_INDEX_PER_ROW_COST,
    const double DEFAULT_INSERT_CHECK_PER_ROW_COST,
    const double DEFAULT_UPDATE_PER_ROW_COST,
    const double DEFAULT_UPDATE_INDEX_PER_ROW_COST,
    const double DEFAULT_UPDATE_CHECK_PER_ROW_COST,
    const double DEFAULT_DELETE_PER_ROW_COST,
    const double DEFAULT_DELETE_INDEX_PER_ROW_COST,
    const double DEFAULT_DELETE_CHECK_PER_ROW_COST,
    const double DEFAULT_SPATIAL_PER_ROW_COST,
    const double DEFAULT_RANGE_COST,
    const double DEFAULT_CMP_UDF_COST,
    const double DEFAULT_CMP_LOB_COST,
    const double DEFAULT_CMP_ERR_HANDLE_EXPR_COST,
    const double DEFAULT_FUNCTIONAL_LOOKUP_PER_ROW_COST,
    const double (&comparison_params)[common::ObMaxTC + 1],
		const double (&hash_params)[common::ObMaxTC + 1],
		const double (&project_params)[2][2][common::ObMaxTC + 1]
    )
    : CPU_TUPLE_COST(DEFAULT_CPU_TUPLE_COST),
      TABLE_SCAN_CPU_TUPLE_COST(DEFAULT_TABLE_SCAN_CPU_TUPLE_COST),
      MICRO_BLOCK_SEQ_COST(DEFAULT_MICRO_BLOCK_SEQ_COST),
      MICRO_BLOCK_RND_COST(DEFAULT_MICRO_BLOCK_RND_COST),
      FETCH_ROW_RND_COST(DEFAULT_FETCH_ROW_RND_COST),
      CMP_SPATIAL_COST(DEFAULT_CMP_GEO_COST),
      MATERIALIZE_PER_BYTE_WRITE_COST(DEFAULT_MATERIALIZE_PER_BYTE_WRITE_COST),
      READ_MATERIALIZED_PER_ROW_COST(DEFAULT_READ_MATERIALIZED_PER_ROW_COST),
      PER_AGGR_FUNC_COST(DEFAULT_PER_AGGR_FUNC_COST),
      PER_WIN_FUNC_COST(DEFAULT_PER_WIN_FUNC_COST),
      CPU_OPERATOR_COST(DEFAULT_CPU_OPERATOR_COST),
      JOIN_PER_ROW_COST(DEFAULT_JOIN_PER_ROW_COST),
      BUILD_HASH_PER_ROW_COST(DEFAULT_BUILD_HASH_PER_ROW_COST),
      PROBE_HASH_PER_ROW_COST(DEFAULT_PROBE_HASH_PER_ROW_COST),
      RESCAN_COST(DEFAULT_RESCAN_COST),
      NETWORK_SER_PER_BYTE_COST(DEFAULT_NETWORK_SER_PER_BYTE_COST),
      NETWORK_DESER_PER_BYTE_COST(DEFAULT_NETWORK_DESER_PER_BYTE_COST),
      NETWORK_TRANS_PER_BYTE_COST(DEFAULT_NETWORK_TRANS_PER_BYTE_COST),
      PX_RESCAN_PER_ROW_COST(DEFAULT_PX_RESCAN_PER_ROW_COST),
      PX_BATCH_RESCAN_PER_ROW_COST(DEFAULT_PX_BATCH_RESCAN_PER_ROW_COST),
      DAS_RESCAN_PER_ROW_RPC_COST(DEFAULT_DAS_RESCAN_PER_ROW_RPC_COST),
      DAS_BATCH_RESCAN_PER_ROW_RPC_COST(DEFAULT_DAS_BATCH_RESCAN_PER_ROW_RPC_COST),
      NL_SCAN_COST(DEFAULT_NL_SCAN_COST),
      BATCH_NL_SCAN_COST(DEFAULT_BATCH_NL_SCAN_COST),
      NL_GET_COST(DEFAULT_NL_GET_COST),
      BATCH_NL_GET_COST(DEFAULT_BATCH_NL_GET_COST),
      TABLE_LOOPUP_PER_ROW_RPC_COST(DEFAULT_TABLE_LOOPUP_PER_ROW_RPC_COST),
      INSERT_PER_ROW_COST(DEFAULT_INSERT_PER_ROW_COST),
      INSERT_INDEX_PER_ROW_COST(DEFAULT_INSERT_INDEX_PER_ROW_COST),
      INSERT_CHECK_PER_ROW_COST(DEFAULT_INSERT_CHECK_PER_ROW_COST),
      UPDATE_PER_ROW_COST(DEFAULT_UPDATE_PER_ROW_COST),
      UPDATE_INDEX_PER_ROW_COST(DEFAULT_UPDATE_INDEX_PER_ROW_COST),
      UPDATE_CHECK_PER_ROW_COST(DEFAULT_UPDATE_CHECK_PER_ROW_COST),
      DELETE_PER_ROW_COST(DEFAULT_DELETE_PER_ROW_COST),
      DELETE_INDEX_PER_ROW_COST(DEFAULT_DELETE_INDEX_PER_ROW_COST),
      DELETE_CHECK_PER_ROW_COST(DEFAULT_DELETE_CHECK_PER_ROW_COST),
      SPATIAL_PER_ROW_COST(DEFAULT_SPATIAL_PER_ROW_COST),
      RANGE_COST(DEFAULT_RANGE_COST),
      CMP_UDF_COST(DEFAULT_CMP_UDF_COST),
      CMP_LOB_COST(DEFAULT_CMP_LOB_COST),
      CMP_ERR_HANDLE_EXPR_COST(DEFAULT_CMP_ERR_HANDLE_EXPR_COST),
      FUNCTIONAL_LOOKUP_PER_ROW_COST(DEFAULT_FUNCTIONAL_LOOKUP_PER_ROW_COST),
      comparison_params_(comparison_params),
		  hash_params_(hash_params),
			project_params_(project_params)
    {
    }

  double get_cpu_tuple_cost(const OptSystemStat& stat) const;
  double get_table_scan_cpu_tuple_cost(const OptSystemStat& stat) const;
  double get_micro_block_seq_cost(const OptSystemStat& stat) const;
  double get_micro_block_rnd_cost(const OptSystemStat& stat) const;
  double get_project_column_cost(const OptSystemStat& stat,
                                 int64_t type,
                                 bool is_rnd,        
                                 bool use_column_store) const;
  double get_fetch_row_rnd_cost(const OptSystemStat& stat) const;
  double get_cmp_spatial_cost(const OptSystemStat& stat) const;
  double get_materialize_per_byte_write_cost(const OptSystemStat& stat) const;
  double get_read_materialized_per_row_cost(const OptSystemStat& stat) const;
  double get_per_aggr_func_cost(const OptSystemStat& stat) const;
  double get_per_win_func_cost(const OptSystemStat& stat) const;
  double get_cpu_operator_cost(const OptSystemStat& stat) const;
  double get_join_per_row_cost(const OptSystemStat& stat) const;
  double get_build_hash_per_row_cost(const OptSystemStat& stat) const;
  double get_probe_hash_per_row_cost(const OptSystemStat& stat) const;
  double get_rescan_cost(const OptSystemStat& stat) const;
  double get_network_ser_per_byte_cost(const OptSystemStat& stat) const;
  double get_network_deser_per_byte_cost(const OptSystemStat& stat) const;
  double get_network_trans_per_byte_cost(const OptSystemStat& stat) const;
  double get_px_rescan_per_row_cost(const OptSystemStat& stat) const;
  double get_px_batch_rescan_per_row_cost(const OptSystemStat& stat) const;
  double get_das_rescan_per_row_rpc_cost(const OptSystemStat& stat) const;
  double get_das_batch_rescan_per_row_rpc_cost(const OptSystemStat& stat) const;
  double get_nl_scan_cost(const OptSystemStat& stat) const;
  double get_batch_nl_scan_cost(const OptSystemStat& stat) const;
  double get_nl_get_cost(const OptSystemStat& stat) const;
  double get_batch_nl_get_cost(const OptSystemStat& stat) const;
  double get_table_loopup_per_row_rpc_cost(const OptSystemStat& stat) const;
  double get_insert_per_row_cost(const OptSystemStat& stat) const;
  double get_insert_index_per_row_cost(const OptSystemStat& stat) const;
  double get_insert_check_per_row_cost(const OptSystemStat& stat) const;
  double get_update_per_row_cost(const OptSystemStat& stat) const;
  double get_update_index_per_row_cost(const OptSystemStat& stat) const;
  double get_update_check_per_row_cost(const OptSystemStat& stat) const;
  double get_delete_per_row_cost(const OptSystemStat& stat) const;
  double get_delete_index_per_row_cost(const OptSystemStat& stat) const;
  double get_delete_check_per_row_cost(const OptSystemStat& stat) const;
  double get_spatial_per_row_cost(const OptSystemStat& stat) const;
  double get_range_cost(const OptSystemStat& stat) const;
  double get_comparison_cost(const OptSystemStat& stat, int64_t type) const;
  double get_hash_cost(const OptSystemStat& stat, int64_t type) const;
  double get_cmp_err_handle_expr_cost(const OptSystemStat& stat) const;
  double get_functional_lookup_per_row_cost(const OptSystemStat& stat) const;

protected:
  /** Read the CPU overhead of one line, which basically includes only the get_next_row() operation */
  double CPU_TUPLE_COST;
  /** The cost of storage layer emitting one row **/
  double TABLE_SCAN_CPU_TUPLE_COST;
  /** The overhead of sequentially reading a microblock and deserializing it */
  double MICRO_BLOCK_SEQ_COST;
  /** The overhead of randomly reading a microblock and deserializing it */
  double MICRO_BLOCK_RND_COST;
  /** The overhead of locating a specific row position during random reads */
  double FETCH_ROW_RND_COST;
  /** Compare the cost of one space data comparison */
  double CMP_SPATIAL_COST;
  /** The cost of materializing a byte */
  double MATERIALIZE_PER_BYTE_WRITE_COST;
  /** Read the cost of reading a materialized row, i.e., get_next_row() on the materialized data structure */
  double READ_MATERIALIZED_PER_ROW_COST;
  /** The cost of one aggregate function calculation */
  double PER_AGGR_FUNC_COST;
  /** The cost of one window function calculation */
  double PER_WIN_FUNC_COST;
  /** The basic cost of one operation */
  double CPU_OPERATOR_COST;
  /** The basic cost of connecting one row between two tables */
  double JOIN_PER_ROW_COST;
  /** Amortized cost per row when building hash table */
  double BUILD_HASH_PER_ROW_COST;
  /** Amortized cost per row when querying hash table */
  double PROBE_HASH_PER_ROW_COST;
  double RESCAN_COST;
  /*network serialization cost for one byte*/
  double NETWORK_SER_PER_BYTE_COST;
  /*network de-serialization cost for one byte*/
  double NETWORK_DESER_PER_BYTE_COST;
  /** The cost of transmitting 1 byte over the network */
  double NETWORK_TRANS_PER_BYTE_COST;
  /*additional px-rescan cost*/
  double PX_RESCAN_PER_ROW_COST;
  double PX_BATCH_RESCAN_PER_ROW_COST;
  /*additional das-rescan cost*/
  double DAS_RESCAN_PER_ROW_RPC_COST;
  double DAS_BATCH_RESCAN_PER_ROW_RPC_COST;
  // Cost of scanning the right table once in nested loop join under condition
  double NL_SCAN_COST;
  // Cost of scanning the right table once for batch nestloop join under condition
  double BATCH_NL_SCAN_COST;
  // Cost of getting the right table once in nested loop join under condition
  double NL_GET_COST;
  // Cost of getting the right table once in batch nested loop join under condition
  double BATCH_NL_GET_COST;
  // table look up one line of rpc cost
  double TABLE_LOOPUP_PER_ROW_RPC_COST;
  // insert one row into the main table cost
  double INSERT_PER_ROW_COST;
  // insert one row into the index table cost
  double INSERT_INDEX_PER_ROW_COST;
  // insert single constraint check cost
  double INSERT_CHECK_PER_ROW_COST;
  // update one row of the main table cost
  double UPDATE_PER_ROW_COST;
  // update the cost of updating one row in the index table
  double UPDATE_INDEX_PER_ROW_COST;
  // update single constraint check cost
  double UPDATE_CHECK_PER_ROW_COST;
  //delete one row from the main table cost
  double DELETE_PER_ROW_COST;
  //delete one row from the index table cost
  double DELETE_INDEX_PER_ROW_COST;
  //delete single constraint check cost
  double DELETE_CHECK_PER_ROW_COST;
  // Linear parameters for spatial index scan
  double SPATIAL_PER_ROW_COST;
  // Storage layer switch cost of one range
  double RANGE_COST;
  // Calculate the cost of a UDF
  double CMP_UDF_COST;
  // Calculate the cost of an expression with a return value of LOB
  double CMP_LOB_COST;
  // Calculate the cost of an expression that needs to be processed for exceptions
  double CMP_ERR_HANDLE_EXPR_COST;
  // Calculate the cost of a full-text index functional lookup expression
  double FUNCTIONAL_LOOKUP_PER_ROW_COST;

  const double (&comparison_params_)[common::ObMaxTC + 1];
  const double (&hash_params_)[common::ObMaxTC + 1];  /*
   *                             +-sequence access project
   *              +-row store----+
   *              |              +-random access project
   * project cost-+
   *              |              +-sequence access project
   *              +-column store-+
   *                             +-random access project
   */
  const double (&project_params_)[2][2][common::ObMaxTC + 1];
};

}
}
#endif /*OCEANBASE_SQL_OPTIMIZER_OB_OPT_EST_PARAMETER_*/
