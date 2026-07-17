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
#define USING_LOG_PREFIX STORAGE

#include "storage/direct_load/ob_direct_load_multiple_heap_table_sorter.h"
#include "observer/table_load/ob_table_load_service.h"
#include "observer/table_load/ob_table_load_table_ctx.h"
#include "storage/direct_load/ob_direct_load_datum_row.h"
#include "storage/direct_load/ob_direct_load_external_table.h"
#include "storage/direct_load/ob_direct_load_multiple_heap_table_builder.h"
#include "storage/direct_load/ob_direct_load_multiple_heap_table_map.h"
#include "storage/ob_parallel_external_sort.h"

namespace oceanbase
{
namespace storage
{
using namespace common;
using namespace blocksstable;
using namespace observer;

namespace
{
class ObDirectLoadSortMemoryPolicy
{
public:
  static int get_chunk_sort_memory(ObDirectLoadMemContext *mem_ctx, int64_t &sort_memory)
  {
    int ret = OB_SUCCESS;
    sort_memory = 0;
    if (OB_ISNULL(mem_ctx)) {
      ret = OB_INVALID_ARGUMENT;
      STORAGE_LOG(WARN, "invalid direct load memory context", KR(ret), KP(mem_ctx));
    } else if (mem_ctx->exe_mode_ == ObTableLoadExeMode::MAX_TYPE) {
      sort_memory = mem_ctx->heap_table_mem_chunk_size_;
    } else if (OB_FAIL(ObTableLoadService::get_sort_memory(sort_memory))) {
      STORAGE_LOG(WARN, "fail to get sort memory", KR(ret));
    }
    const int64_t min_memory_limit = ObExternalSortConstant::MIN_MEMORY_LIMIT;
    if (OB_SUCC(ret) && sort_memory < min_memory_limit) {
      STORAGE_LOG(INFO, "adjust direct load sort memory to minimum",
                  K(sort_memory), K(min_memory_limit));
      sort_memory = min_memory_limit;
    }
    return ret;
  }
};
} // namespace

ObDirectLoadMultipleHeapTableSorter::ObDirectLoadMultipleHeapTableSorter(
  ObDirectLoadMemContext *mem_ctx)
  : ctx_(nullptr),
    mem_ctx_(mem_ctx),
    allocator_("TLD_Sorter"),
    index_dir_id_(-1),
    data_dir_id_(-1),
    heap_table_array_(nullptr),
    chunk_pool_()
{
  
}

ObDirectLoadMultipleHeapTableSorter::~ObDirectLoadMultipleHeapTableSorter()
{
  drain_chunk_pool();
}

int ObDirectLoadMultipleHeapTableSorter::add_table(const ObDirectLoadTableHandle &table_handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!table_handle.is_valid() || !table_handle.get_table()->is_external_table())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(table_handle));
  } else {
    ObDirectLoadExternalTable *external_table =
      static_cast<ObDirectLoadExternalTable *>(table_handle.get_table());
    if (OB_FAIL(fragments_.push_back(external_table->get_fragments()))) {
      LOG_WARN("fail to push back", KR(ret));
    }
  }
  return ret;
}

int ObDirectLoadMultipleHeapTableSorter::acquire_chunk(ObDirectLoadMultipleHeapTableMap *&chunk)
{
  int ret = OB_SUCCESS;
  chunk = nullptr;
  ObMemAttr mem_attr("TLD_MemChunk");
  int64_t sort_memory = 0;
  if (OB_FAIL(ObDirectLoadSortMemoryPolicy::get_chunk_sort_memory(mem_ctx_, sort_memory))) {
    LOG_WARN("fail to get direct load chunk sort memory", KR(ret));
  } else if (chunk_pool_.count() > 0) {
    chunk = chunk_pool_.at(chunk_pool_.count() - 1);
    chunk_pool_.pop_back();
    chunk->set_mem_limit(sort_memory);
    chunk->reuse();
    LOG_TRACE("reuse direct load heap table map chunk", K(sort_memory), K(chunk_pool_.count()));
  } else {
    if (OB_ISNULL(chunk = OB_NEW(ObDirectLoadMultipleHeapTableMap, mem_attr, sort_memory))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to new ObDirectLoadMultipleHeapTableMap", KR(ret));
    } else if (OB_FAIL(chunk->init())) {
      LOG_WARN("fail to init external sort", KR(ret));
    }
  }
  if (OB_FAIL(ret)) {
    if (nullptr != chunk) {
      destroy_chunk(chunk);
    }
  }
  return ret;
}

int ObDirectLoadMultipleHeapTableSorter::close_chunk(ObDirectLoadMultipleHeapTableMap *&chunk)
{
  int ret = OB_SUCCESS;
  ObArray<ObTabletID> keys;
  ObDirectLoadDatumRow datum_row;
  ObDirectLoadMultipleHeapTableBuilder table_builder;
  ObDirectLoadMultipleHeapTableBuildParam table_builder_param;

  
  table_builder_param.table_data_desc_ = mem_ctx_->table_data_desc_;
  table_builder_param.file_mgr_ = mem_ctx_->file_mgr_;
  table_builder_param.extra_buf_ = reinterpret_cast<char *>(1); // unuse, delete in future
  table_builder_param.extra_buf_size_ = 4096;
  table_builder_param.index_dir_id_ = index_dir_id_;
  table_builder_param.data_dir_id_ = data_dir_id_;
  if (OB_ISNULL(chunk)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("chunk is null", KR(ret));
  } else if (OB_FAIL(chunk->get_all_key_sorted(keys))) {
    LOG_WARN("fail to get keys", KR(ret));
  } else if (OB_FAIL(datum_row.init(mem_ctx_->table_data_desc_.column_count_))) {
    LOG_WARN("fail to init datum row", KR(ret));
  } else if (OB_FAIL(table_builder.init(table_builder_param))) {
    LOG_WARN("fail to init table builder", KR(ret));
  }
  // Write to table_builder in order of tablet_id
  for (int64_t i = 0; OB_SUCC(ret) && i < keys.count(); i++) {
    const ObTabletID &tablet_id = keys.at(i);
    ObArray<const ObDirectLoadConstExternalMultiPartitionRow *> bag;
    
    if (OB_FAIL(chunk->get(tablet_id, bag))) {
      LOG_WARN("fail to get bag", KR(ret));
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < bag.count(); j++) {
      const ObDirectLoadConstExternalMultiPartitionRow *row = bag.at(j);
      if (OB_FAIL(row->to_datum_row(datum_row))) {
        LOG_WARN("fail to transfer dataum row", KR(ret));
      } else if (OB_FAIL(table_builder.append_row(tablet_id, datum_row))) {
        LOG_WARN("fail to append row", KR(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_builder.close())) {
      LOG_WARN("fail to close table builder", KR(ret));
    } else if (OB_FAIL(get_tables(table_builder))) {
      LOG_WARN("fail to get tables", KR(ret));
    }
  }

  if (chunk != nullptr) {
    if (OB_SUCC(ret)) {
      recycle_chunk(chunk);
    } else {
      destroy_chunk(chunk);
    }
  }
  return ret;
}

void ObDirectLoadMultipleHeapTableSorter::recycle_chunk(ObDirectLoadMultipleHeapTableMap *&chunk)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(chunk)) {
    // do nothing
  } else if (chunk_pool_.count() >= MAX_CACHED_CHUNK_COUNT) {
    destroy_chunk(chunk);
  } else {
    chunk->reuse();
    if (OB_FAIL(chunk_pool_.push_back(chunk))) {
      LOG_WARN("fail to cache direct load heap table map chunk", KR(ret), K(chunk_pool_.count()));
      destroy_chunk(chunk);
    } else {
      chunk = nullptr;
    }
  }
}

void ObDirectLoadMultipleHeapTableSorter::destroy_chunk(ObDirectLoadMultipleHeapTableMap *&chunk)
{
  if (OB_NOT_NULL(chunk)) {
    ObMemAttr mem_attr("TLD_MemChunk");
    OB_DELETE(ObDirectLoadMultipleHeapTableMap, mem_attr, chunk);
    chunk = nullptr;
  }
}

void ObDirectLoadMultipleHeapTableSorter::drain_chunk_pool()
{
  while (chunk_pool_.count() > 0) {
    ObDirectLoadMultipleHeapTableMap *chunk = chunk_pool_.at(chunk_pool_.count() - 1);
    chunk_pool_.pop_back();
    destroy_chunk(chunk);
  }
}

int ObDirectLoadMultipleHeapTableSorter::get_tables(
  ObIDirectLoadPartitionTableBuilder &table_builder)
{
  int ret = OB_SUCCESS;
  ObDirectLoadTableHandleArray table_array;
  if (OB_FAIL(table_builder.get_tables(table_array, mem_ctx_->table_mgr_))) {
    LOG_WARN("fail to get tables", KR(ret));
  } else if (OB_FAIL(heap_table_array_->add(table_array))) {
    LOG_WARN("fail to add table array", KR(ret));
  } else {
    ATOMIC_AAF(&ctx_->job_stat_->store_.compact_stage_product_tmp_files_, table_array.count());
  }
  return ret;
}

int ObDirectLoadMultipleHeapTableSorter::work()
{
  typedef ObDirectLoadExternalMultiPartitionRow RowType;
  typedef ObDirectLoadMultipleHeapTableMap ChunkType;
  typedef ObDirectLoadExternalBlockReader<RowType> ExternalReader;

  int ret = OB_SUCCESS;
  const RowType *row = nullptr;
  ChunkType *chunk = nullptr;
  ObDirectLoadConstExternalMultiPartitionRow const_row;

  if (OB_UNLIKELY(index_dir_id_ <= 0 || data_dir_id_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K_(index_dir_id), K_(data_dir_id));
  }

  for (int64_t i = 0; OB_SUCC(ret) && OB_LIKELY(!mem_ctx_->has_error_) && i < fragments_.count(); i++) {
    const ObDirectLoadExternalFragment &fragment = fragments_.at(i);
    ExternalReader external_reader;
    if (OB_FAIL(external_reader.init(mem_ctx_->table_data_desc_.external_data_block_size_,
                                     mem_ctx_->table_data_desc_.compressor_type_))) {
      LOG_WARN("fail to init external reader", KR(ret));
    } else if (OB_FAIL(external_reader.open(fragment.file_handle_, 0, fragment.file_size_))) {
      LOG_WARN("fail to open file", KR(ret));
    }
    while (OB_SUCC(ret) && OB_LIKELY(!mem_ctx_->has_error_)) {
      if (row == nullptr && OB_FAIL(external_reader.get_next_item(row))) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("fail to get next item", KR(ret));
        } else {
          ret = OB_SUCCESS;
          break;
        }
      } else if (chunk == nullptr && OB_FAIL(acquire_chunk(chunk))) {
        LOG_WARN("fail to acquire chunk", KR(ret));
      } else {
        const_row = *row;
        if (OB_FAIL(chunk->add_row(row->tablet_id_, const_row))) {
          if (OB_UNLIKELY(OB_BUF_NOT_ENOUGH != ret)) {
            LOG_WARN("fail to add row", KR(ret));
          } else {
            ret = OB_SUCCESS;
            if (OB_FAIL(close_chunk(chunk))) {
              LOG_WARN("fail to close chunk", KR(ret));
            }
          }
        } else {
          row = nullptr;
          ATOMIC_AAF(&ctx_->job_stat_->store_.compact_stage_load_rows_, 1);
        }
      }
    }
  }

  if (OB_SUCC(ret) && OB_LIKELY(!mem_ctx_->has_error_)) {
    if (chunk != nullptr && OB_FAIL(close_chunk(chunk))) {
      LOG_WARN("fail to close chunk", KR(ret));
    }
  }

  if (chunk != nullptr) {
    destroy_chunk(chunk);
  }

  return ret;
}

} // namespace storage
} // namespace oceanbase
