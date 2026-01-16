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

#include "lib/hash/ob_hashmap.h"
#include "lib/lock/ob_mutex.h"
#include "observer/table_load/ob_table_load_bucket.h"
#include "observer/table_load/ob_table_load_struct.h"
#include "share/table/ob_table_load_array.h"
#include "share/table/ob_table_load_define.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadParam;
class ObTableLoadStoreCtx;
class ObTableLoadTransCtx;
class ObTableLoadCoordinatorCtx;

class ObTableLoadTransBucketWriter
{
public:
  ObTableLoadTransBucketWriter(ObTableLoadTransCtx *trans_ctx);
  ~ObTableLoadTransBucketWriter();
  int init();
  // Only called in the corresponding worker thread, executed serially
  int write(int32_t session_id, table::ObTableLoadObjRowArray &obj_rows);
  int flush(int32_t session_id, bool &is_finished);
private:
  class SessionContext;
  int init_session_ctx_array();
  int handle_partition_with_autoinc_identity(SessionContext &session_ctx,
                                             table::ObTableLoadObjRowArray &obj_rows,
                                             const uint64_t &sql_mode, int32_t session_id);
  int handle_autoinc_column(const share::schema::ObColumnSchemaV2 *column_schema,
                            const common::ObObj &obj,
                            common::ObObj &out_obj,
                            int32_t session_id,
                            const uint64_t &sql_mode);
  int handle_identity_column(const share::schema::ObColumnSchemaV2 *column_schema,
                             const common::ObObj &obj,
                             common::ObObj &out_obj,
                             common::ObArenaAllocator &cast_allocator);
  // Non-partitioned table
  int write_for_non_partitioned(SessionContext &session_ctx,
                                const table::ObTableLoadObjRowArray &obj_rows);
  // partition table
  int write_for_partitioned(SessionContext &session_ctx,
                            const table::ObTableLoadObjRowArray &obj_rows);
  int get_load_bucket(SessionContext &session_ctx, const table::ObTableLoadPartitionId &partition_id,
                      ObTableLoadBucket *&load_bucket);
  int write_load_bucket(SessionContext &session_ctx, ObTableLoadBucket *load_bucket);
private:
  static const int64_t WRITE_ROW_SIZE = 2LL * 1024 * 1024;
  ObTableLoadTransCtx *const trans_ctx_;
  ObTableLoadCoordinatorCtx *const coordinator_ctx_;
  const ObTableLoadParam &param_;
  common::ObArenaAllocator allocator_;
  bool is_partitioned_;
  int64_t column_count_;
  common::ObCastMode cast_mode_;
  struct SessionContext
  {
    SessionContext();
    ~SessionContext();
    void reset();
    int32_t session_id_;
    // The following parameters are only accessed in the corresponding worker thread
    common::ObArenaAllocator allocator_;
    // for non-partitioned table
    table::ObTableLoadPartitionId partition_id_;
    ObTableLoadBucket load_bucket_;
    // for partitioned table
    common::hash::ObHashMap<common::ObAddr, ObTableLoadBucket *> load_bucket_map_;
    common::ObArray<ObTableLoadBucket *> load_bucket_array_;
    // The following parameters are accessed with a lock
    lib::ObMutex mutex_;
    uint64_t last_receive_sequence_no_;
  };
  SessionContext *session_ctx_array_;
  int64_t session_count_;
  int64_t flush_count_ CACHE_ALIGNED;
  bool is_inited_;
};

}  // namespace observer
}  // namespace oceanbase
