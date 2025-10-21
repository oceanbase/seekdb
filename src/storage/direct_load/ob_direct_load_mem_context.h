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

#include "share/ob_order_perserving_encoder.h"
#include "share/schema/ob_table_param.h"
#include "storage/direct_load/ob_direct_load_easy_queue.h"
#include "storage/direct_load/ob_direct_load_i_table.h"
#include "storage/direct_load/ob_direct_load_mem_define.h"
#include "storage/direct_load/ob_direct_load_table_data_desc.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadDMLRowHandler;
class ObDirectLoadMemWorker;

class ObMemDumpQueue
{
  struct Item
  {
    void *ptr_;
    Item() : ptr_(nullptr) {}
    Item(void *ptr) : ptr_(ptr) {}
  };
public:
  ~ObMemDumpQueue();
  int push(void *p);
  int pop(void *&p);
  int init(int64_t capacity) {
    return queue_.init(capacity);
  }
  int64_t size() const {
    return queue_.size();
  }
private:
  common::LightyQueue queue_;
};



class ObDirectLoadMemContext
{
public:
  static int init_enc_param(const ObColDesc &col_desc, share::ObEncParam &param);
  static int init_enc_params(const common::ObIArray<share::schema::ObColDesc> &column_descs,
                             const int64_t rowkey_column_num,
                             const sql::ObLoadDupActionType dup_action,
                             ObIArray<share::ObEncParam> &enc_params);

  typedef ObDirectLoadExternalMultiPartitionRowChunk ChunkType;
  ObDirectLoadMemContext() : datum_utils_(nullptr),
                             dml_row_handler_(nullptr),
                             file_mgr_(nullptr),
                             table_mgr_(nullptr),
                             dup_action_(sql::ObLoadDupActionType::LOAD_INVALID_MODE),
                             exe_mode_(observer::ObTableLoadExeMode::MAX_TYPE),
                             merge_count_per_round_(0),
                             max_mem_chunk_count_(0),
                             mem_chunk_size_(0),
                             heap_table_mem_chunk_size_(0),
                             total_thread_cnt_(0),
                             dump_thread_cnt_(0),
                             load_thread_cnt_(0),
                             finish_load_thread_cnt_(0),
                             running_dump_task_cnt_(0),
                             fly_mem_chunk_count_(0),
                             has_error_(false)
  {
  }

  ~ObDirectLoadMemContext();

public:
  int init();
  int init_enc_params(const sql::ObLoadDupActionType dup_acton,
                      const common::ObIArray<share::schema::ObColDesc> &column_descs);
  void reset();
  int add_tables_from_table_compactor(ObIDirectLoadTabletTableCompactor &compactor);
  int add_tables_from_table_array(ObDirectLoadTableHandleArray &table_array);
  int acquire_chunk(ChunkType *&chunk);
  void release_chunk(ChunkType *chunk);

public:
  static const int64_t MIN_MEM_LIMIT = 8LL * 1024 * 1024; // 8MB

public:
  ObDirectLoadTableDataDesc table_data_desc_;
  ObArray<share::ObEncParam> enc_params_; // for ObAdaptiveQS
  const blocksstable::ObStorageDatumUtils *datum_utils_;
  ObDirectLoadDMLRowHandler *dml_row_handler_;
  ObDirectLoadTmpFileManager *file_mgr_;
  ObDirectLoadTableManager *table_mgr_;
  sql::ObLoadDupActionType dup_action_;

  observer::ObTableLoadExeMode exe_mode_;
  int64_t merge_count_per_round_;
  int64_t max_mem_chunk_count_;
  int64_t mem_chunk_size_;
  int64_t heap_table_mem_chunk_size_;

  int64_t total_thread_cnt_; // total number of threads
  int64_t dump_thread_cnt_; // number of dump threads
  int64_t load_thread_cnt_; // number of load threads, has no actual meaning in pre_sort, only used as a sample thread exit flag

  int64_t finish_load_thread_cnt_; // the number of load threads that have finished
  int64_t running_dump_task_cnt_; // Number of dump tasks still running
  int64_t fly_mem_chunk_count_; // the current number of chunks, including those still being written and already closed chunks

  ObDirectLoadEasyQueue<int64_t> pre_sort_chunk_queue_; // presort task queue
  ObDirectLoadEasyQueue<ObDirectLoadMemWorker *> mem_loader_queue_; // loader task queue
  ObMemDumpQueue mem_dump_queue_; // dump task queue
  ObDirectLoadEasyQueue<storage::ObDirectLoadExternalMultiPartitionRowChunk *> mem_chunk_queue_; // closed chunk queue

  // save result
  lib::ObMutex mutex_;
  ObDirectLoadTableHandleArray tables_handle_;

  volatile bool has_error_;
};

} // namespace storage
} // namespace oceanbase
