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

#ifndef __OB_SQL_PDML_BATCH_ROW_CACHE_H__
#define __OB_SQL_PDML_BATCH_ROW_CACHE_H__

#include "lib/container/ob_se_array.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"
#include "sql/engine/ob_tenant_sql_memory_manager.h"
#include "sql/engine/ob_sql_mem_mgr_processor.h"
#include "sql/engine/ob_io_event_observer.h"

namespace oceanbase
{
namespace common
{
class ObNewRow;
}

namespace sql
{
struct ObDASTabletLoc;
class ObExecContext;
// Single partition new engine data cache
class ObPDMLOpRowIterator
{
public:
    friend class ObPDMLOpBatchRowCache;
    ObPDMLOpRowIterator() : eval_ctx_(nullptr) {}
    virtual ~ObPDMLOpRowIterator() = default;
    // Get the next row of data from row_store_it_
    // The returned data corresponds to the exprs of the stored data
    int get_next_row(const ObExprPtrIArray &row);
    void close() { row_store_it_.reset(); }
private:
    int init_data_source(ObChunkDatumStore &row_datum_store,
                         ObEvalCtx *eval_ctx);
private:
  ObChunkDatumStore::Iterator row_store_it_;
  ObEvalCtx *eval_ctx_;
  DISALLOW_COPY_AND_ASSIGN(ObPDMLOpRowIterator);
};
// PDML cache
// Cache data for multiple partitions
class ObPDMLOpBatchRowCache final
{
public:
  explicit ObPDMLOpBatchRowCache(ObEvalCtx *eval_ctx, ObMonitorNode &op_monitor_info);
  ~ObPDMLOpBatchRowCache();
public:
  int init(uint64_t tenant_id, int64_t part_cnt, bool with_barrier, const ObTableModifySpec &spec);
  // ObBatchRowCache does not need to support disk persistence, once memory is insufficient, add_row will fail, the caller will
  // Responsible for inserting all cached data into the storage layer and releasing the memory of the cache.
  // @desc part_id indicates the offset of the partition where the row is located in the location structure
  // @return If the cache is full, return OB_SIZE_OVERFLOW; if the cache is successful, return OB_SUCCESS; otherwise, return other types of errors
  int add_row(const ObExprPtrIArray &row, common::ObTabletID tablet_id);
  // @desc get all part_id from current cache
  int get_part_id_array(ObTabletIDArray &arr);
  // @desc Obtain all data for the corresponding partition through part_id
  int get_row_iterator(common::ObTabletID tablet_id, ObPDMLOpRowIterator *&iterator);
  // @desc Release the data and memory of the partition corresponding to part_id
  // TODO: jiangting.lk In the entire cache framework, actually there is no need for an interface to clean up data of a single partition
  // Only need an interface to clean up the entire cache state, such as reuse. Each time before refilling the data, call the reuse method,
  // Clean up the entire cache state.
  // int free_rows(int64_t part_id);
  // @desc no row cached
  bool empty() const;
  // @desc Reset cache state, will not release memory space, convenient for reuse of cache later
  int reuse_after_rows_processed();
  // @desc Release all memory occupied by internal structures (such as hashmap) of ObBatchRowCache
  void destroy();
private:
  int init_row_store(ObChunkDatumStore *&chunk_row_store);
  int create_new_bucket(common::ObTabletID tablet_id, ObChunkDatumStore *&row_store);
  int process_dump();
  int dump_all_datum_store();
  bool need_dump() const;
  int free_datum_store_memory();
private:
  typedef common::hash::ObHashMap<common::ObTabletID, ObChunkDatumStore *,
                                  common::hash::NoPthreadDefendMode> PartitionStoreMap;
  // HashMap: part_id => chunk_datum_store_ ptr
  // dynamic add row store to map, when meet a new partition
  common::ObArenaAllocator row_allocator_; // used for allocating memory to store in cache
  ObEvalCtx *eval_ctx_;
  PartitionStoreMap pstore_map_;
  ObPDMLOpRowIterator iterator_; // used to construct the corresponding iter
  int64_t cached_rows_num_; // indicates the number of rows currently in the cache
  int64_t cached_rows_size_; // bytes cached. used to control max memory used by batchRowCache
  int64_t cached_in_mem_rows_num_; // indicates the number of in-memory rows currently cached, excluding dumped rows
  uint64_t tenant_id_;
  bool with_barrier_;

  lib::MemoryContext mem_context_;
  ObSqlWorkAreaProfile profile_;
  ObSqlMemMgrProcessor sql_mem_processor_;
  ObIOEventObserver io_event_observer_;
  DISALLOW_COPY_AND_ASSIGN(ObPDMLOpBatchRowCache);
};


class ObDMLOpDataReader
{
public:
  // Read a row of data from the DML operator, generally from child op
  // At the same time, responsible for calculating which partition this row of data belongs to.
  // Generally, the partition id is stored in a pseudo column in the row
  virtual int read_row(ObExecContext &ctx,
                       const ObExprPtrIArray *&row,
                       common::ObTabletID &tablet_id,
                       bool &is_skipped) = 0;
};

class ObDMLOpDataWriter
{
public:
  // Write data in bulk to the storage layer
  virtual int write_rows(ObExecContext &ctx,
                         const ObDASTabletLoc *tablet_loc,
                         ObPDMLOpRowIterator &iterator) = 0;
};

}
}
#endif /* __OB_SQL_PDML_BATCH_ROW_CACHE_H__ */
//// end of header file

