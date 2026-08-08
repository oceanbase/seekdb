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

#ifndef OCEANBASE_STORAGE_OB_DML_PARAM_
#define OCEANBASE_STORAGE_OB_DML_PARAM_

#include "data_plane/access/ob_table_scan_param.h"
#include "query/engine/basic/ob_row_to_expr_projector.h"
#include "data_plane/access/ob_tablet_scan.h"
#include "lib/container/ob_iarray.h"
#include "common/ob_common_types.h"
#include "storage/meta_mem/ob_tablet_handle.h"
#include "common/datum/ob_datum.h"
#include "query/engine/basic/ob_pushdown_filter.h"
#include "storage/tx/ob_trans_define_v4.h"

namespace oceanbase
{
namespace sql
{
class ObOperator;
class ObTableLocation;
class ObEvalCtx;
class ObEvalInfo;
using common::ObDatum;
}  // namespace sql
namespace share
{
namespace schema
{
class ObTableParam;
class ObTableDMLParam;
}  // namespace schema
}  // namespace share
namespace blocksstable
{
struct ObStorageDatum;
}
namespace storage
{
class ObStoreCtxGuard;

struct ObDMLBaseParam
{
  ObDMLBaseParam()
      : timeout_(-1),
        schema_version_(-1),
        sql_mode_(DEFAULT_MYSQL_MODE),
        tz_info_(NULL),
        table_param_(NULL),
        runtime_schema_version_(OB_INVALID_VERSION),
        is_total_quantity_log_(false),
        is_ignore_(false),
        prelock_(false),
        is_batch_stmt_(false),
        dml_allocator_(nullptr),
        store_ctx_guard_(nullptr),
        spec_seq_no_(),
        snapshot_(),
        branch_id_(0),
        write_flag_(),
        check_schema_version_(true),
        ddl_task_id_(0),
        lob_allocator_(ObModIds::OB_LOB_ACCESS_BUFFER, OB_MALLOC_NORMAL_BLOCK_SIZE),
        is_main_table_in_fts_ddl_(false),
        has_async_index_(false)
  {
  }

  ~ObDMLBaseParam()
  {
  }

  int64_t timeout_;
  int64_t schema_version_;
  ObSQLMode sql_mode_;
  const common::ObTimeZoneInfo *tz_info_;
  const share::schema::ObTableDMLParam *table_param_;
  int64_t runtime_schema_version_;
  bool is_total_quantity_log_;
  bool is_ignore_;
  bool prelock_;
  bool is_batch_stmt_;
  mutable common::ObIAllocator *dml_allocator_;
  mutable ObStoreCtxGuard *store_ctx_guard_;

  // specified seq_no
  transaction::ObTxSEQ spec_seq_no_;
  // transaction snapshot
  transaction::ObTxReadSnapshot snapshot_;
  // parallel dml write branch id
  int16_t branch_id_;
  // write flag for inner write processing
  concurrent_control::ObWriteFlag write_flag_;
  bool check_schema_version_;
  int64_t ddl_task_id_;
  mutable ObArenaAllocator lob_allocator_;
  bool is_main_table_in_fts_ddl_; // whether the main table is in fts ddl when dml is executed
  // Set by DAS layer when the table has async-mode indexes (e.g. sync_mode=async HNSW).
  // Propagated to ObTxCtx::has_async_index_redo_ -> ObTxLogBlockHeader::HAS_ASYNC_INDEX
  // for Change Stream fast filtering in the Fetcher.
  bool has_async_index_;
  bool is_valid() const { return (timeout_ > 0 && schema_version_ >= 0) && nullptr != store_ctx_guard_; }
  DECLARE_TO_STRING;
};

} // end namespace storage
} // end namespace oceanbase

#endif //OCEANBASE_STORAGE_OB_DML_PARAM_
