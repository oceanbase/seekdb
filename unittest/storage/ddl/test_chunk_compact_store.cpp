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

#define ASSERT_OK(x) ASSERT_EQ(OB_SUCCESS, (x))

#include <chrono>

#define private public
#define protected public
#include "storage/blocksstable/ob_row_generate.h"
#include "storage/blocksstable/ob_data_file_prepare.h"
#include "sql/engine/basic/chunk_store/ob_compact_store.h"
#include "unittest/storage/blocksstable/ob_data_file_prepare.h"
#include "mtlenv/mock_server_runtime_env.h"
#include "observer/omt/ob_server_module_lifecycle.h"
#undef private

namespace oceanbase
{

using namespace common;
using namespace lib;
using namespace share;
using namespace sql;

//const int64_t COLUMN_CNT = 64;
const int64_t COLUMN_CNT = 64;
const int64_t BATCH_SIZE = 10000;
const int64_t ROUND[6] = {2,8,32,128,512, 1024};
int64_t RESULT_ADD[6] = {0,0,0,0,0,0};
int64_t RESULT_BUILD[6] = {0,0,0,0,0,0};

// Route the test's process-wide temporary-file manager through share::g_mp.
class FakeModuleProvider : public share::ObIModuleProvider
{
public:
  tmp_file::ObTmpFileManager *tmp_file_manager() override { return tmp_file_mgr_; }
  tmp_file::ObTmpFileManager *tmp_file_mgr_ = nullptr;
};

typedef ObChunkDatumStore::StoredRow StoredRow;
//typedef ObChunkDatumStore::Block Block;
typedef ObTempBlockStore::Block Block;

class ObStoredRowGenerate {
public:
  int get_stored_row(StoredRow **&sr);
  int get_stored_row_irregular(StoredRow **&sr);

  common::ObArenaAllocator allocator_;
};

int ObStoredRowGenerate::get_stored_row(StoredRow **&sr)
{
  int ret = OB_SUCCESS;
  int64_t data_size = ((sizeof(ObDatum) + 8) * COLUMN_CNT + 8) * BATCH_SIZE;
  int32_t row_size = (sizeof(ObDatum) + 8) * COLUMN_CNT + 8;
  allocator_.reuse();
  void *buf = allocator_.alloc(data_size);
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc buff", K(ret));
  } else {
    MEMSET(buf, 0, data_size);
    for (int64_t i = 0; i < BATCH_SIZE; i++)
    {
      StoredRow * cur_sr = (StoredRow*) ((char*)buf + i * row_size);
      if (i == BATCH_SIZE) {
        cur_sr->row_size_ = 8 + 1042*COLUMN_CNT;
      } else {
        cur_sr->row_size_ = row_size;
      }
      cur_sr->cnt_ = COLUMN_CNT;
      for (int64_t j = 0; j < COLUMN_CNT; j++) {
        if (i != BATCH_SIZE) {
          int64_t datum_offset = sizeof(ObDatum) * j;
          int64_t data_offset = COLUMN_CNT * sizeof(ObDatum) + 8 * j + sizeof(StoredRow);
          ObDatum *datum_ptr = (ObDatum *)(cur_sr->payload_ + datum_offset);
          int64_t *data_ptr = (int64_t *)((char*)cur_sr + data_offset);
          datum_ptr->len_ = 8;
          //MEMCPY((void*)&datum_ptr->ptr_, &data_offset, 8);
          MEMCPY((void*)&datum_ptr->ptr_, &data_ptr, 8);
          *data_ptr = 1;
        } else {
          // wont't go here 
          // generate var data
          int64_t datum_offset = sizeof(ObDatum) * j;
          int64_t data_offset = COLUMN_CNT * sizeof(ObDatum) + 8 * j + sizeof(StoredRow);
          ObDatum *datum_ptr = (ObDatum *)(cur_sr->payload_ + datum_offset);
          int64_t *data_ptr = (int64_t *)((char*)cur_sr + data_offset);
          datum_ptr->len_ = 1030;
          //MEMCPY((void*)&datum_ptr->ptr_, &data_offset, 8);
          MEMCPY((void*)&datum_ptr->ptr_, &data_ptr, 8);
          *data_ptr = 1;
        }
      }
    }
    sr = (StoredRow**)buf;
  }

  return ret;
}

int ObStoredRowGenerate::get_stored_row_irregular(StoredRow **&sr)
{
  int ret = OB_SUCCESS;
  int64_t data_size = ((sizeof(ObDatum) + 8) * COLUMN_CNT + 8) * BATCH_SIZE;
  int32_t row_size = (sizeof(ObDatum) + 8) * COLUMN_CNT + 8;
  allocator_.reuse();
  void *buf = allocator_.alloc(data_size);
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc buff", K(ret));
  } else {
    MEMSET(buf, 0, data_size);
    for (int64_t i = 0; i < BATCH_SIZE; i++)
    {
      StoredRow * cur_sr = (StoredRow*) ((char*)buf + i * row_size);
      if (i == BATCH_SIZE) {
        cur_sr->row_size_ = 8 + 1042*COLUMN_CNT;
      } else {
        cur_sr->row_size_ = row_size;
      }
      cur_sr->cnt_ = COLUMN_CNT;
      for (int64_t j = 0; j < COLUMN_CNT; j++) {
        if (i != BATCH_SIZE) {
          int64_t datum_offset = sizeof(ObDatum) * j;
          int64_t data_offset = COLUMN_CNT * sizeof(ObDatum) + 8 * j + sizeof(StoredRow);
          ObDatum *datum_ptr = (ObDatum *)(cur_sr->payload_ + datum_offset);
          int64_t *data_ptr = (int64_t *)((char*)cur_sr + data_offset);
          datum_ptr->len_ = 8;
          //MEMCPY((void*)&datum_ptr->ptr_, &data_offset, 8);
          MEMCPY((void*)&datum_ptr->ptr_, &data_ptr, 8);
          *data_ptr = i * 1024 + j;
        } else {
          // wont't go here
          // generate var data
          int64_t datum_offset = sizeof(ObDatum) * j;
          int64_t data_offset = COLUMN_CNT * sizeof(ObDatum) + 8 * j + sizeof(StoredRow);
          ObDatum *datum_ptr = (ObDatum *)(cur_sr->payload_ + datum_offset);
          int64_t *data_ptr = (int64_t *)((char*)cur_sr + data_offset);
          datum_ptr->len_ = 1030;
          //MEMCPY((void*)&datum_ptr->ptr_, &data_offset, 8);
          MEMCPY((void*)&datum_ptr->ptr_, &data_ptr, 8);
          *data_ptr = 1;
        }
      }
    }
    sr = (StoredRow**)buf;
  }
  return ret;
}
class TestCompactChunk : public TestDataFilePrepare
{
public:
  TestCompactChunk();
  void SetUp();
  void TearDown();
  static void SetUpTestCase()
  {
    ASSERT_EQ(OB_SUCCESS, ObTimerService::get_instance().start());
  } 
  static void TearDownTestCase()
  {
    ObTimerService::get_instance().stop();
    ObTimerService::get_instance().wait();
    ObTimerService::get_instance().destroy();
  }

  int init_memory_budget()
  {
    lib::set_memory_budget(128LL << 32);
    return OB_SUCCESS;
  }

protected:
  ObStoredRowGenerate row_generate_;
  ObArenaAllocator allocator_;
  FakeModuleProvider provider_;
  tmp_file::ObTmpFileManager *tmp_file_mgr_;
  share::ObIModuleProvider *old_module_provider_;
};
TestCompactChunk::TestCompactChunk()
  : TestDataFilePrepare("TestTmpFile", 2 * 1024 * 1024, 2048),
    provider_(),
    tmp_file_mgr_(nullptr),
    old_module_provider_(nullptr)
{
}
void TestCompactChunk::SetUp()
{
  int ret = OB_SUCCESS;
  const int64_t bucket_num = 1024;
  const int64_t max_cache_size = 1024 * 1024 * 1024;
  const int64_t block_size = common::OB_MALLOC_BIG_BLOCK_SIZE;
  TestDataFilePrepare::SetUp();
  ret = ObKVGlobalCache::get_instance().init(bucket_num, max_cache_size, block_size);
  if (OB_INIT_TWICE == ret) {
    ret = OB_SUCCESS;
  } else {
    ASSERT_EQ(OB_SUCCESS, ret);
  }
  // set observer memory limit
  CHUNK_MGR.set_limit(8LL * 1024 * 1024 * 1024);

  EXPECT_EQ(OB_SUCCESS, init_memory_budget());
  ASSERT_EQ(OB_SUCCESS, common::ObClockGenerator::init());
  ASSERT_EQ(OB_SUCCESS, tmp_file::ObTmpBlockCache::get_instance().init("tmp_block_cache"));
  ASSERT_EQ(OB_SUCCESS, tmp_file::ObTmpPageCache::get_instance().init("sn_tmp_page_cache"));
  ASSERT_EQ(OB_SUCCESS, ObTimerService::get_instance().start());

  old_module_provider_ = share::g_mp;
  ASSERT_EQ(OB_SUCCESS, server_module_new_default(tmp_file_mgr_));
  ASSERT_EQ(OB_SUCCESS, tmp_file::ObTmpFileManager::server_module_init(tmp_file_mgr_));
  tmp_file_mgr_->get_sn_file_manager().page_cache_controller_.write_buffer_pool_.default_wbp_memory_limit_ = 40*1024*1024;
  ASSERT_EQ(OB_SUCCESS, tmp_file_mgr_->start());
  provider_.tmp_file_mgr_ = tmp_file_mgr_;
  share::g_mp = &provider_;
  SERVER_STORAGE_META_SERVICE.is_started_ = true;
}

void TestCompactChunk::TearDown()
{
  ObKVGlobalCache::get_instance().destroy();
  allocator_.reuse();
  row_generate_.allocator_.reuse();
  // The temporary-file manager depends on the block manager owned by the base fixture.
  if (OB_NOT_NULL(tmp_file_mgr_)) {
    tmp_file_mgr_->stop();
    tmp_file_mgr_->wait();
    server_module_destroy_default(tmp_file_mgr_);
  }
  provider_.tmp_file_mgr_ = nullptr;
  share::g_mp = old_module_provider_;
  old_module_provider_ = nullptr;
  TestDataFilePrepare::TearDown();

  tmp_file::ObTmpBlockCache::get_instance().destroy();
  tmp_file::ObTmpPageCache::get_instance().destroy();
  common::ObClockGenerator::destroy();
  ObTimerService::get_instance().stop();
  ObTimerService::get_instance().wait();
  ObTimerService::get_instance().destroy();
}

TEST_F(TestCompactChunk, test_read_writer_compact)
{
  int ret = OB_SUCCESS;
  ObCompactStore cs_chunk;

  cs_chunk.init(1,
        ObCtxIds::DEFAULT_CTX_ID, "SORT_CACHE_CTX", true, 0, true);
  ChunkRowMeta row_meta(allocator_);
  row_meta.col_cnt_ = COLUMN_CNT;
  row_meta.fixed_cnt_ = COLUMN_CNT;
  row_meta.var_data_off_ = 8 * row_meta.fixed_cnt_;
  row_meta.column_length_.prepare_allocate(COLUMN_CNT);
  row_meta.column_offset_.prepare_allocate(COLUMN_CNT);
  for (int64_t i = 0; i < COLUMN_CNT; i++) {
    if (i != COLUMN_CNT) {
      row_meta.column_length_[i] = 8;
      row_meta.column_offset_[i] = 8 * i;
    } else {
      row_meta.column_length_[i] = 0;
      row_meta.column_offset_[i] = 0;
    }
  }
  cs_chunk.set_meta(&row_meta);
  
  
  StoredRow **sr;
  ret = row_generate_.get_stored_row(sr); 
  ASSERT_EQ(ret, OB_SUCCESS);

  char *buf = reinterpret_cast<char*>(sr);
  int64_t pos = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < BATCH_SIZE; i++) {
    StoredRow *tmp_sr = (StoredRow *)(buf + pos);
    ret = cs_chunk.add_row(*tmp_sr);
    ASSERT_EQ(ret, OB_SUCCESS);
    pos += tmp_sr->row_size_;
  }
  ret = cs_chunk.finish_add_row();
  ASSERT_EQ(ret, OB_SUCCESS);
  for (int64_t i = 0; OB_SUCC(ret) && i < BATCH_SIZE; i++) {
    int64_t result = 0;
    const StoredRow *cur_sr = nullptr;
    ret = cs_chunk.get_next_row(cur_sr);
    if (ret == OB_ITER_END) {
      ret = OB_SUCCESS;
    }
    ASSERT_EQ(ret, OB_SUCCESS);
    for (int64_t k = 0; k < cur_sr->cnt_; k++) {
      ObDatum cur_cell = cur_sr->cells()[k];
      result += *(int64_t *)(cur_cell.ptr_);
    }
    OB_ASSERT(result == 64);
  }
}


TEST_F(TestCompactChunk, test_read_writer_compact_vardata)
{
  int ret = OB_SUCCESS;
  ObCompactStore cs_chunk;

  cs_chunk.init(1,
        ObCtxIds::DEFAULT_CTX_ID, "SORT_CACHE_CTX", true, 0, true);
  ChunkRowMeta row_meta(allocator_);
  row_meta.col_cnt_ = COLUMN_CNT;
  row_meta.fixed_cnt_ = 0;
  row_meta.var_data_off_ = 0;
  row_meta.column_length_.prepare_allocate(COLUMN_CNT);
  row_meta.column_offset_.prepare_allocate(COLUMN_CNT);
  for (int64_t i = 0; i < COLUMN_CNT; i++) {
    if (i != COLUMN_CNT) {
      row_meta.column_length_[i] = 0;
      row_meta.column_offset_[i] = 0;
    } else {
      row_meta.column_length_[i] = 0;
      row_meta.column_offset_[i] = 0;
    }
  }
  cs_chunk.set_meta(&row_meta);
  
  StoredRow **sr;
  ret = row_generate_.get_stored_row(sr);
  ASSERT_EQ(ret, OB_SUCCESS);

  char *buf = reinterpret_cast<char*>(sr);
  int64_t pos = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < BATCH_SIZE; i++) {
    StoredRow *tmp_sr = (StoredRow *)(buf + pos);
    ret = cs_chunk.add_row(*tmp_sr);
    ASSERT_EQ(ret, OB_SUCCESS);
    pos += tmp_sr->row_size_;
  }
  ret = cs_chunk.finish_add_row();
  ASSERT_EQ(ret, OB_SUCCESS);
  for (int64_t i = 0; OB_SUCC(ret) && i < BATCH_SIZE; i++) {
    int64_t result = 0;
    const StoredRow *cur_sr = nullptr;
    ret = cs_chunk.get_next_row(cur_sr);
    if (ret == OB_ITER_END) {
      ret = OB_SUCCESS;
    }
    ASSERT_EQ(ret, OB_SUCCESS);
    for (int64_t k = 0; k < cur_sr->cnt_; k++) {
      ObDatum cur_cell = cur_sr->cells()[k];
      result += *(int64_t *)(cur_cell.ptr_);
    }
    OB_ASSERT(result == 64);
  }
}

TEST_F(TestCompactChunk, test_rescan_get_last_row_compact)
{
  int ret = OB_SUCCESS;
  ObCompactStore cs_chunk;
  cs_chunk.init(1,
        ObCtxIds::DEFAULT_CTX_ID, "SORT_CACHE_CTX", true, 0, false/*disable trunc*/);
  ChunkRowMeta row_meta(allocator_);
  row_meta.col_cnt_ = COLUMN_CNT;
  row_meta.fixed_cnt_ = 0;
  row_meta.var_data_off_ = 0;
  row_meta.column_length_.prepare_allocate(COLUMN_CNT);
  row_meta.column_offset_.prepare_allocate(COLUMN_CNT);
  for (int64_t i = 0; i < COLUMN_CNT; i++) {
    if (i != COLUMN_CNT) {
      row_meta.column_length_[i] = 0;
      row_meta.column_offset_[i] = 0;
    } else {
      row_meta.column_length_[i] = 0;
      row_meta.column_offset_[i] = 0;
    }
  }
  cs_chunk.set_meta(&row_meta);
  StoredRow **sr;
  ret = row_generate_.get_stored_row_irregular(sr);
  ASSERT_EQ(ret, OB_SUCCESS);

  char *buf = reinterpret_cast<char*>(sr);
  int64_t pos = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < BATCH_SIZE; i++) {
    StoredRow *tmp_sr = (StoredRow *)(buf + pos);
    ret = cs_chunk.add_row(*tmp_sr);
    ASSERT_EQ(ret, OB_SUCCESS);
    pos += tmp_sr->row_size_;
    // get last row
    const StoredRow *cur_sr = nullptr;
    ret = cs_chunk.get_last_stored_row(cur_sr);
    ASSERT_EQ(ret, OB_SUCCESS);
    int64_t res = 0;
    for (int64_t k = 0; k < cur_sr->cnt_; k++) {
      ObDatum cur_cell = cur_sr->cells()[k];
      res += *(int64_t *)(cur_cell.ptr_);
    }
    OB_ASSERT(res == ((1024 * i * COLUMN_CNT) + ((COLUMN_CNT - 1) * COLUMN_CNT / 2)));
  }

  ret = cs_chunk.finish_add_row();
  ASSERT_EQ(ret, OB_SUCCESS);
  for (int j = 0; OB_SUCC(ret) && j < 2; j++ ) {
    int64_t total_res = 0;
    cs_chunk.rescan();
    for (int64_t i = 0; OB_SUCC(ret) && i < BATCH_SIZE; i++) {
      int64_t result = 0;
      const StoredRow *cur_sr = nullptr;
      ret = cs_chunk.get_next_row(cur_sr);
      if (ret == OB_ITER_END) {
        ret = OB_SUCCESS;
      }
      ASSERT_EQ(ret, OB_SUCCESS);
      for (int64_t k = 0; k < cur_sr->cnt_; k++) {
        ObDatum cur_cell = cur_sr->cells()[k];
        result += *(int64_t *)(cur_cell.ptr_);
        total_res += *(int64_t *)(cur_cell.ptr_);
      }
      OB_ASSERT(result == ((1024 * i * COLUMN_CNT) + ((COLUMN_CNT - 1) * COLUMN_CNT / 2)));
    }
    OB_ASSERT(total_res == ((1024 * (BATCH_SIZE-1) * BATCH_SIZE * COLUMN_CNT / 2) + BATCH_SIZE * ((COLUMN_CNT - 1) * COLUMN_CNT / 2)));
  }
}

// TEST_F(TestCompactChunk, test_rescan_add_storagedatum)
// {
//   int ret = OB_SUCCESS;
//   ObCompactStore cs_chunk;
//   cs_chunk.init(1, 1,
//         ObCtxIds::DEFAULT_CTX_ID, "SORT_CACHE_CTX", true, 0, false/*disable trunc*/, share::SORT_COMPACT_LEVEL);
//   ChunkRowMeta row_meta(allocator_);
//   row_meta.col_cnt_ = COLUMN_CNT;
//   row_meta.fixed_cnt_ = 0;
//   row_meta.var_data_off_ = 0;
//   row_meta.column_length_.prepare_allocate(COLUMN_CNT);
//   row_meta.column_offset_.prepare_allocate(COLUMN_CNT);
//   for (int64_t i = 0; i < COLUMN_CNT; i++) {
//     if (i != COLUMN_CNT) {
//       row_meta.column_length_[i] = 0;
//       row_meta.column_offset_[i] = 0;
//     } else {
//       row_meta.column_length_[i] = 0;
//       row_meta.column_offset_[i] = 0;
//     }
//   }
//   cs_chunk.set_meta(&row_meta);
//   StoredRow **sr;
//   ret = row_generate_.get_stored_row_irregular(sr);
//   ASSERT_EQ(ret, OB_SUCCESS);

//   char *buf = reinterpret_cast<char*>(sr);
//   int64_t pos = 0;
//   for (int64_t i = 0; OB_SUCC(ret) && i < BATCH_SIZE; i++) {
//     StoredRow *tmp_sr = (StoredRow *)(buf + pos);
//     ObStorageDatum ssr[COLUMN_CNT];
//     for (int64_t k = 0; OB_SUCC(ret) && k < COLUMN_CNT; k++) {
//       ssr[k].shallow_copy_from_datum(tmp_sr->cells()[k]);
//     }
//     ret = cs_chunk.add_row(ssr, COLUMN_CNT, 0);
//     ASSERT_EQ(ret, OB_SUCCESS);
//     pos += tmp_sr->row_size_;
//     // get last row
//     const StoredRow *cur_sr = nullptr;
//     ret = cs_chunk.get_last_stored_row(cur_sr);
//     ASSERT_EQ(ret, OB_SUCCESS);
//     int64_t res = 0;
//     for (int64_t k = 0; k < cur_sr->cnt_; k++) {
//       ObDatum cur_cell = cur_sr->cells()[k];
//       res += *(int64_t *)(cur_cell.ptr_);
//     }
//     OB_ASSERT(res == ((1024 * i * COLUMN_CNT) + ((COLUMN_CNT - 1) * COLUMN_CNT / 2)));
//   }

//   ret = cs_chunk.finish_add_row();
//   ASSERT_EQ(ret, OB_SUCCESS);
//   for (int j = 0; OB_SUCC(ret) && j < 2; j++ ) {
//     int64_t total_res = 0;
//     cs_chunk.rescan();
//     for (int64_t i = 0; OB_SUCC(ret) && i < BATCH_SIZE; i++) {
//       int64_t result = 0;
//       const StoredRow *cur_sr = nullptr;
//       ret = cs_chunk.get_next_row(cur_sr);
//       if (ret == OB_ITER_END) {
//         ret = OB_SUCCESS;
//       }
//       ASSERT_EQ(ret, OB_SUCCESS);
//       for (int64_t k = 0; k < cur_sr->cnt_; k++) {
//         ObDatum cur_cell = cur_sr->cells()[k];
//         result += *(int64_t *)(cur_cell.ptr_);
//         total_res += *(int64_t *)(cur_cell.ptr_);
//       }
//       OB_ASSERT(result == ((1024 * i * COLUMN_CNT) + ((COLUMN_CNT - 1) * COLUMN_CNT / 2)));
//     }
//     OB_ASSERT(total_res == ((1024 * (BATCH_SIZE-1) * BATCH_SIZE * COLUMN_CNT / 2) + BATCH_SIZE * ((COLUMN_CNT - 1) * COLUMN_CNT / 2)));
//   }
// }

}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  system("rm -rf test_ddl_compact_store.log*");
  OB_LOGGER.set_log_level("INFO");
  OB_LOGGER.set_file_name("test_ddl_compact_store.log", true);
  //testing::FLAGS_gtest_filter = "TestCompactChunk.test_dump_one_block";
  return RUN_ALL_TESTS();
}
