// owner: zs475329
// owner group: storage

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
#include <gtest/gtest.h>
#include <stdexcept>
#include <chrono>
#define OK(ass) ASSERT_EQ(OB_SUCCESS, (ass))
#define NOT_NULL(ass) ASSERT_NE(nullptr, (ass))
#define private public
#define protected public

#include "storage/blocksstable/ob_row_generate.h"
#include "storage/blocksstable/ob_dag_macro_block_writer.h"
#include "mtlenv/mock_tenant_module_env.h"
#include "storage/ddl/ob_cg_block_tmp_file.h"
#include "storage/ddl/ob_cg_block_tmp_files_iterator.h"
#include "storage/blocksstable/ob_data_file_prepare.h"
#include "unittest/storage/blocksstable/cs_encoding/ob_row_vector_converter.h"

namespace oceanbase
{
using namespace common;
using namespace compaction;
using namespace blocksstable;
using namespace tmp_file;
using namespace storage;
using namespace share::schema;
using namespace std;


int ObDDLRedoLogWriterCallback::wait()
{
  int ret = OB_SUCCESS;
  return ret;
}
int ObMacroBlockWriter::exec_callback(const ObStorageObjectHandle &macro_handle, ObMacroBlock *macro_block)
{
  int ret = OB_SUCCESS;
  return ret;
}

namespace unittest
{


class TestMacroBlockRowBareIterator : public ::testing::Test
{
public:
  TestMacroBlockRowBareIterator() = default;
  virtual ~TestMacroBlockRowBareIterator() = default;
  static void SetUpTestCase();
  static void TearDownTestCase();
  virtual void SetUp();
  virtual void TearDown();
protected:
  void set_master_key(ObWholeDataStoreDesc &data_desc);
  void generate_cg_block(ObCGBlockFile &cg_block_file,
                         int &micro_block_cnt,
                         const bool is_complete_macro_block,
                         const ObMergeType merge_type = ObMergeType::MAJOR_MERGE,
                         const bool is_encrypt = false);
  void generate_cg_block_without_batch(ObCGBlockFile &cg_block_file,
                                       int &micro_block_cnt,
                                       const bool is_complete_macro_block,
                                       const ObMergeType merge_type = ObMergeType::MAJOR_MERGE,
                                       const bool is_encrypt = false);
  void large_batch_generate_cg_block(ObCGBlockFile &cg_block_file, int &micro_block_cnt);
  void random_change_cgblock_micro_block_id(ObCGBlock *cg_block, int64_t &actual_micro_block_count);
  void aggregate_cg_blocks_into_macro_block(ObCGBlockFilesIterator &cg_block_files_iter,
                                            const int64_t expected_all_micro_block_cnt,
                                            const ObMergeType merge_type = ObMergeType::MAJOR_MERGE,
                                            const bool is_encrypt = false,
                                            const uint64_t count = INT64_MAX);
  void test_buf_not_enough(ObCGBlockFilesIterator &cg_block_files_iter, const bool check_if_complete_macro_block, const uint64_t count = INT64_MAX);
  void test_reuse_macro_block(ObCGBlockFilesIterator &cg_block_files_iter, const uint64_t count = INT64_MAX);
  void prepare_index_builder(ObWholeDataStoreDesc &data_desc,
                             ObSSTableIndexBuilder &sstable_builder,
                             const bool need_submit_io,
                             const ObMergeType merge_type = ObMergeType::MAJOR_MERGE,
                             const bool is_encrypt = false);
  void prepare_data_desc(ObWholeDataStoreDesc &data_desc,
                         ObSSTableIndexBuilder *sstable_builder,
                         const bool need_submit_io,
                         const ObMergeType merge_type = ObMergeType::MAJOR_MERGE,
                         const bool is_encrypt = false);
  void prepare_schema();
  void prepare_index_desc(const ObWholeDataStoreDesc &data_desc, ObWholeDataStoreDesc &index_desc);

  static const int64_t TEST_COLUMN_CNT = 20;
  static const int64_t TEST_ROWKEY_COLUMN_CNT = 20;
  static const int64_t TEST_ROW_CNT = 1000;
  static const int64_t TEST_MICRO_BLOCK_CNT = 30;
  static const int64_t MAX_TEST_COLUMN_CNT = TEST_COLUMN_CNT + 3;
  static const int64_t SNAPSHOT_VERSION = 2;
  static const int64_t TEST_BATCH_SIZE = 100;
  ObTableSchema table_schema_;
  ObTableSchema index_schema_;
  common::ObArray<share::schema::ObColDesc> col_descs_;
  common::ObArray<share::schema::ObColDesc> multi_col_descs_;
  ObRowGenerate row_generate_;
  ObRowGenerate index_row_generate_;
  ObITable::TableKey table_key_;
  ObDatumRow row_;
  ObDatumRow multi_row_;
  ObArenaAllocator allocator_;
private:
  int64_t tenant_id_ = 1;
  int64_t master_key_id_ = 12332;
  ObCipherOpMode mode_ = ObCipherOpMode::ob_invalid_mode;
  char master_key_[OB_MAX_MASTER_KEY_LENGTH] = "12345";
  char raw_key_[OB_MAX_ENCRYPTION_KEY_NAME_LENGTH] = "54321";
  char encrypt_key_[OB_MAX_ENCRYPTION_KEY_NAME_LENGTH];
  int64_t encrypt_key_len_;
};


void TestMacroBlockRowBareIterator::SetUpTestCase()
{
  int ret = OB_SUCCESS;
  STORAGE_LOG(INFO, "SetUpTestCase");
  EXPECT_EQ(OB_SUCCESS, MockTenantModuleEnv::get_instance().init());
}

void TestMacroBlockRowBareIterator::TearDownTestCase()
{
  MockTenantModuleEnv::get_instance().destroy();
}

static ObSimpleMemLimitGetter getter;

static void convert_to_multi_version_col_descs(const common::ObArray<share::schema::ObColDesc> &col_descs,
    const int64_t schema_rowkey_cnt, const int64_t column_cnt, common::ObArray<share::schema::ObColDesc> &multi_col_descs)
{
  for (int64_t i = 0; i < schema_rowkey_cnt; ++i) {
    multi_col_descs.push_back(col_descs[i]);
  }
  ObColDesc col_desc_0;
  col_desc_0.col_id_ = static_cast<int32_t>(schema_rowkey_cnt);
  col_desc_0.col_type_ = col_descs[ObInt32Type-1].col_type_;
  multi_col_descs.push_back(col_desc_0);
  ObColDesc col_desc_1;
  col_desc_1.col_id_ = static_cast<int32_t>(schema_rowkey_cnt+1);
  col_desc_1.col_type_ = col_descs[ObInt32Type-1].col_type_;
  multi_col_descs.push_back(col_desc_1);
  for(int64_t i = schema_rowkey_cnt; i < column_cnt; ++i) {
    multi_col_descs.push_back(col_descs[i]);
    multi_col_descs[i+2].col_id_ = static_cast<int32_t>(i+2);
  }
}

void TestMacroBlockRowBareIterator::SetUp()
{
  ASSERT_EQ(true, MockTenantModuleEnv::get_instance().is_inited());
  int ret = OB_SUCCESS;

  prepare_schema();
  row_generate_.reset();
  index_row_generate_.reset();
  ret = row_generate_.init(table_schema_, &allocator_);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(OB_SUCCESS, (row_generate_.get_schema().get_column_ids(col_descs_)));
  const ObColumnSchemaV2 *col_schema = nullptr;
  for (int64_t i = 0; OB_SUCC(ret) && i < TEST_COLUMN_CNT; ++i) {
    if (OB_ISNULL(col_schema = row_generate_.get_schema().get_column_schema(i + OB_APP_MIN_COLUMN_ID))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("The column schema is NULL", K(ret), K(i));
    } else if (col_descs_.at(i).col_type_.is_decimal_int()) {
      col_descs_.at(i).col_type_.set_stored_precision(col_schema->get_accuracy().get_precision());
      col_descs_.at(i).col_type_.set_scale(col_schema->get_accuracy().get_scale());
    }
  }
  convert_to_multi_version_col_descs(col_descs_, TestMacroBlockRowBareIterator::TEST_ROWKEY_COLUMN_CNT, TestMacroBlockRowBareIterator::TEST_COLUMN_CNT, multi_col_descs_);

  ret = index_row_generate_.init(index_schema_, &allocator_);
  ASSERT_EQ(OB_SUCCESS, ret);
  table_key_.tablet_id_ = table_schema_.get_table_id();
  table_key_.table_type_ = ObITable::TableType::MINOR_SSTABLE;
  table_key_.version_range_.snapshot_version_ = share::OB_MAX_SCN_TS_NS - 1;
  ASSERT_TRUE(table_key_.is_valid());
  GCONF.micro_block_merge_verify_level = MICRO_BLOCK_MERGE_VERIFY_LEVEL::ENCODING_AND_COMPRESSION_AND_WRITE_COMPLETE;
}

void TestMacroBlockRowBareIterator::TearDown()
{
  row_generate_.reset();
  index_row_generate_.reset();
  table_schema_.reset();
}

static void convert_to_multi_version_row(const ObDatumRow &org_row,
    const int64_t schema_rowkey_cnt, const int64_t column_cnt, const int64_t snapshot_version, const ObDmlFlag dml_flag,
    ObDatumRow &multi_row)
{
  for (int64_t i = 0; i < schema_rowkey_cnt; ++i) {
    multi_row.storage_datums_[i] = org_row.storage_datums_[i];
  }
  multi_row.storage_datums_[schema_rowkey_cnt].set_int(-snapshot_version);
  multi_row.storage_datums_[schema_rowkey_cnt + 1].set_int(0);
  for(int64_t i = schema_rowkey_cnt; i < column_cnt; ++i) {
    multi_row.storage_datums_[i + 2] = org_row.storage_datums_[i];
  }

  multi_row.count_ = column_cnt + 2;
  if (ObDmlFlag::DF_DELETE == dml_flag) {
    multi_row.row_flag_= ObDmlFlag::DF_DELETE;
  } else {
    multi_row.row_flag_ = ObDmlFlag::DF_INSERT;
  }
  multi_row.mvcc_row_flag_.set_last_multi_version_row(true);
}

void TestMacroBlockRowBareIterator::generate_cg_block_without_batch(ObCGBlockFile &cg_block_file,
                                                        int &micro_block_cnt,
                                                        const bool is_complete_macro_block,
                                                        const ObMergeType merge_type,
                                                        const bool is_encrypt)
{
  int ret = OB_SUCCESS;
  ObWholeDataStoreDesc data_desc;
  ObSSTableIndexBuilder sstable_builder(false /* not need writer buffer*/);
  prepare_index_builder(data_desc, sstable_builder, true/*need_submit_io*/, merge_type, is_encrypt);

  ObMacroDataSeq data_seq(0);
  ObDagTempMacroBlockWriter *dag_temp_macro_writer = new ObDagTempMacroBlockWriter();

  data_seq.set_parallel_degree(0);
  ObMacroSeqParam seq_param;
  seq_param.seq_type_ = ObMacroSeqParam::SEQ_TYPE_INC;
  seq_param.start_ = data_seq.macro_data_seq_;
  ObSSTablePrivateObjectCleaner cleaner;

  ObCGBlockFileWriter cg_block_writer;
  ASSERT_EQ(OB_SUCCESS, cg_block_writer.init(&cg_block_file));
  ASSERT_EQ(OB_SUCCESS, dag_temp_macro_writer->open(data_desc.get_desc(), data_seq.get_parallel_idx(), seq_param, cleaner, &cg_block_writer));
  static const int64_t MAX_TEST_COLUMN_CNT = TestMacroBlockRowBareIterator::TEST_COLUMN_CNT + 3;
  ObDatumRow multi_row;
  ASSERT_EQ(OB_SUCCESS, multi_row.init(allocator_, MAX_TEST_COLUMN_CNT));
  ObDmlFlag dml = DF_INSERT;
  int64_t test_row_num = TestMacroBlockRowBareIterator::TEST_ROW_CNT;
  int64_t test_micro_block_count = TestMacroBlockRowBareIterator::TEST_MICRO_BLOCK_CNT;
  ObDatumRow row;
  ObArenaAllocator allocator;
  ASSERT_EQ(OB_SUCCESS, row.init(allocator, TestMacroBlockRowBareIterator::TEST_COLUMN_CNT));
  ASSERT_EQ(false, dag_temp_macro_writer->is_alloc_block_needed());

  int current_index = dag_temp_macro_writer->current_index_;
  ObMacroBlock &current_block = dag_temp_macro_writer->macro_blocks_[current_index];
  while (current_block.get_micro_block_count() < test_micro_block_count - 1) {
    ASSERT_EQ(OB_SUCCESS, row_generate_.get_next_row(row));
    convert_to_multi_version_row(row, TestMacroBlockRowBareIterator::TEST_ROWKEY_COLUMN_CNT, TestMacroBlockRowBareIterator::TEST_COLUMN_CNT, 2, dml, multi_row);
    ASSERT_EQ(OB_SUCCESS, dag_temp_macro_writer->append_row(multi_row));
  }
  //build the last micro block
  ASSERT_EQ(OB_SUCCESS, row_generate_.get_next_row(row));
  convert_to_multi_version_row(row, TestMacroBlockRowBareIterator::TEST_ROWKEY_COLUMN_CNT, TestMacroBlockRowBareIterator::TEST_COLUMN_CNT, 2, dml, multi_row);
  ASSERT_EQ(OB_SUCCESS, dag_temp_macro_writer->append_row(multi_row));
  ASSERT_GT(dag_temp_macro_writer->micro_writer_->get_row_count(), 0);
  ASSERT_EQ(OB_SUCCESS, dag_temp_macro_writer->build_micro_block());

  //check the micro_block_count
  ASSERT_EQ(test_micro_block_count, current_block.get_micro_block_count());
  ASSERT_EQ(true, current_block.is_dirty());
  int32_t row_count = current_block.get_row_count();
  micro_block_cnt += current_block.get_micro_block_count();

  //generate cg_block
  ASSERT_EQ(OB_SUCCESS, dag_temp_macro_writer->flush_macro_block(current_block, !is_complete_macro_block, nullptr));
  if (dag_temp_macro_writer->is_need_macro_buffer_) {
    ASSERT_EQ(OB_SUCCESS, dag_temp_macro_writer->wait_io_finish(dag_temp_macro_writer->macro_handles_[current_index], &current_block));
  }
  ASSERT_EQ(false, current_block.is_dirty());

  ASSERT_EQ(OB_SUCCESS, dag_temp_macro_writer->close());
  delete dag_temp_macro_writer;
}

void TestMacroBlockRowBareIterator::set_master_key(ObWholeDataStoreDesc &data_desc)
{
  ASSERT_NE(ObCipherOpMode::ob_invalid_mode, mode_);
  data_desc.static_desc_.master_key_id_ = master_key_id_;
  data_desc.static_desc_.encrypt_id_ = static_cast<int64_t>(mode_);
  const int64_t copy_len = encrypt_key_len_ < share::OB_MAX_TABLESPACE_ENCRYPT_KEY_LENGTH ? encrypt_key_len_ : share::OB_MAX_TABLESPACE_ENCRYPT_KEY_LENGTH;
  memcpy(data_desc.static_desc_.encrypt_key_, encrypt_key_, copy_len);
  ObMicroBlockEncryption block_encrypt;
  int ret = block_encrypt.init(static_cast<int64_t>(mode_), tenant_id_, master_key_id_, encrypt_key_, encrypt_key_len_);
  ASSERT_EQ(OB_SUCCESS, ret);
}

void TestMacroBlockRowBareIterator::prepare_index_builder(ObWholeDataStoreDesc &data_desc,
                                          ObSSTableIndexBuilder &sstable_builder,
                                          const bool need_submit_io,
                                          const ObMergeType merge_type,
                                          const bool is_encrypt)
{
  int ret = OB_SUCCESS;
  prepare_data_desc(data_desc, &sstable_builder, need_submit_io, merge_type, is_encrypt);
  ret = sstable_builder.init(data_desc.get_desc());
  ASSERT_EQ(OB_SUCCESS, ret);
}

void TestMacroBlockRowBareIterator::prepare_data_desc(ObWholeDataStoreDesc &data_desc,
                                      ObSSTableIndexBuilder *sstable_builder,
                                      const bool need_submit_io,
                                      const ObMergeType merge_type,
                                      const bool is_encrypt)
{
  int ret = OB_SUCCESS;
  const int64_t snapshot_version = ObTimeUtility::fast_current_time();
  SCN end_scn;
  end_scn.convert_for_tx(snapshot_version);
  table_schema_.enable_macro_block_bloom_filter_ = true;
  ret = data_desc.init(false/*is_ddl*/, table_schema_, ObLSID(1), ObTabletID(1), merge_type,
                       ObTimeUtility::fast_current_time()/*snapshot_version*/, DATA_CURRENT_VERSION,
                       table_schema_.get_micro_index_clustered(), 0/*transfer_seq*/, 0/*concurrent_cnt*/,
                       end_scn, end_scn, nullptr, 0/*table_cg_idx*/,
                       compaction::ObExecMode::EXEC_MODE_LOCAL/*exec_mode*/, need_submit_io);
  if (is_encrypt) {
    set_master_key(data_desc);
  }

  data_desc.get_desc().sstable_index_builder_ = sstable_builder;
  ASSERT_EQ(OB_SUCCESS, ret);
}


void TestMacroBlockRowBareIterator::prepare_schema()
{
  ObColumnSchemaV2 column;
  uint64_t table_id = 3001;
  int64_t micro_block_size = 16 * 1024;
  //init table schema
  table_schema_.reset();
  index_schema_.reset();
  ASSERT_EQ(OB_SUCCESS, table_schema_.set_table_name("test_concurrency_defenses"));
  ASSERT_EQ(OB_SUCCESS, index_schema_.set_table_name("test_concurrency_defenses"));
  table_schema_.set_tenant_id(1);
  table_schema_.set_tablegroup_id(1);
  table_schema_.set_database_id(1);
  table_schema_.set_table_id(table_id);
  table_schema_.set_rowkey_column_num(TEST_ROWKEY_COLUMN_CNT);
  table_schema_.set_max_used_column_id(TEST_COLUMN_CNT);
  table_schema_.set_block_size(micro_block_size);
  table_schema_.set_compress_func_name("none");
  table_schema_.set_row_store_type(CS_ENCODING_ROW_STORE);

  index_schema_.set_tenant_id(1);
  index_schema_.set_tablegroup_id(1);
  index_schema_.set_database_id(1);
  index_schema_.set_table_id(table_id);
  index_schema_.set_rowkey_column_num(TEST_ROWKEY_COLUMN_CNT);
  index_schema_.set_max_used_column_id(TEST_ROWKEY_COLUMN_CNT + 1);
  index_schema_.set_block_size(micro_block_size);
  index_schema_.set_compress_func_name("none");
  index_schema_.set_row_store_type(CS_ENCODING_ROW_STORE);
  //init column
  ObArray<share::schema::ObColDesc> out_cols;
  const int64_t schema_version = 1;
  const int64_t rowkey_count = TEST_ROWKEY_COLUMN_CNT;
  const int64_t column_count = TEST_COLUMN_CNT;
  char name[OB_MAX_FILE_NAME_LENGTH];
  memset(name, 0, sizeof(name));
  ObObjType obj_types[2] = {ObInt32Type, ObFloatType};
  int64_t pos = 1;
  for(int64_t i = 0; i < TEST_COLUMN_CNT; ++i) {
    ObObjType obj_type = obj_types[i % 2];
    column.reset();
    column.set_table_id(table_id);
    column.set_column_id(i + OB_APP_MIN_COLUMN_ID);
    sprintf(name, "test%020ld", i);
    ASSERT_EQ(OB_SUCCESS, column.set_column_name(name));
    column.set_data_type(obj_type);
    column.set_collation_type(CS_TYPE_BINARY);
    column.set_data_length(1);
    if(obj_type == common::ObInt32Type) {
      column.set_rowkey_position(pos++);
      column.set_order_in_rowkey(ObOrderType::ASC);
    } else if(obj_type == common::ObFloatType) {
      column.set_rowkey_position(pos++);
      column.set_order_in_rowkey(ObOrderType::DESC);
    } else {
      // should not reach here
      ASSERT_EQ(OB_SUCCESS, OB_NOT_SUPPORTED);
    }
    share::schema::ObColDesc col;
    col.col_id_ = static_cast<uint64_t>(i + OB_APP_MIN_COLUMN_ID);
    col.col_type_.set_type(obj_type);
    ASSERT_EQ(OB_SUCCESS, out_cols.push_back(col));
    ASSERT_EQ(OB_SUCCESS, table_schema_.add_column(column));
    if (column.is_rowkey_column()) {
      ASSERT_EQ(OB_SUCCESS, index_schema_.add_column(column));
    }
  }
  column.reset();
  column.set_table_id(table_id);
  column.set_column_id(TEST_COLUMN_CNT +OB_APP_MIN_COLUMN_ID);
  column.set_data_type(ObVarcharType);
  column.set_collation_type(CS_TYPE_BINARY);
  column.set_data_length(1);
  column.set_rowkey_position(0);
  ASSERT_EQ(OB_SUCCESS, index_schema_.add_column(column));
}

TEST_F(TestMacroBlockRowBareIterator, test_macro_block_row_bare_iterator)
{
  LOG_INFO("BEGIN TestMacroBlockRowBareIterator.test_macro_block_row_bare_iterator");
  ObWholeDataStoreDesc data_desc;
  ObSSTableIndexBuilder sstable_builder(false /* not need writer buffer*/);
  prepare_index_builder(data_desc, sstable_builder, true/*need_submit_io*/, ObMergeType::MAJOR_MERGE, false/* is_encrypt */);

  ObMacroDataSeq data_seq(0);
  ObMacroBlockWriter *macro_writer = new ObMacroBlockWriter(true/*is_need_macro_buffer*/);

  data_seq.set_parallel_degree(0);
  ObMacroSeqParam seq_param;
  seq_param.seq_type_ = ObMacroSeqParam::SEQ_TYPE_INC;
  seq_param.start_ = data_seq.macro_data_seq_;
  ObSSTablePrivateObjectCleaner cleaner;
  const ObPreWarmerParam pre_warm_param(MEM_PRE_WARM);
  ASSERT_EQ(OB_SUCCESS, macro_writer->open(data_desc.get_desc(), data_seq.get_parallel_idx(), seq_param, pre_warm_param, cleaner, nullptr, nullptr, nullptr));
  static const int64_t MAX_TEST_COLUMN_CNT = TestMacroBlockRowBareIterator::TEST_COLUMN_CNT + 2;
  ObDatumRow multi_row;
  ASSERT_EQ(OB_SUCCESS, multi_row.init(allocator_, MAX_TEST_COLUMN_CNT));
  ObDmlFlag dml = DF_INSERT;
  int64_t test_row_num = TestMacroBlockRowBareIterator::TEST_ROW_CNT;
  int64_t test_micro_block_count = TestMacroBlockRowBareIterator::TEST_MICRO_BLOCK_CNT;
  ObDatumRow row;
  ObArenaAllocator allocator;
  ASSERT_EQ(OB_SUCCESS, row.init(allocator, TestMacroBlockRowBareIterator::TEST_COLUMN_CNT));
  for (int64_t i = 0; i < test_row_num; ++i) {
    OK(row_generate_.get_next_row(i, row));
    convert_to_multi_version_row(row, table_schema_.get_rowkey_column_num(), table_schema_.get_column_count(), SNAPSHOT_VERSION, dml, multi_row);
    ASSERT_EQ(OB_SUCCESS, macro_writer->append_row(multi_row));
  }
  ASSERT_EQ(OB_SUCCESS, macro_writer->close());
  macro_writer->reset();
  ASSERT_EQ(OB_SUCCESS, macro_writer->open(data_desc.get_desc(), data_seq.get_parallel_idx(), seq_param, pre_warm_param, cleaner, nullptr, nullptr, nullptr));
  for (int64_t i = test_row_num; i < test_row_num * 2; ++i) {
    OK(row_generate_.get_next_row(i, row));
    convert_to_multi_version_row(row, table_schema_.get_rowkey_column_num(), table_schema_.get_column_count(), SNAPSHOT_VERSION, dml, multi_row);
    ASSERT_EQ(OB_SUCCESS, macro_writer->append_row(multi_row));
  }
  ASSERT_EQ(OB_SUCCESS, macro_writer->close());
  macro_writer = nullptr;
  LOG_INFO("FINISH TestMacroBlockRowBareIterator.test_macro_block_row_bare_iterator");
}

}//end namespace unittest
}//end namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -f test_macro_block_row_bare_iterator.log*");
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  OB_LOGGER.set_file_name("test_macro_block_row_bare_iterator.log", true);
  srand(time(NULL));
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
