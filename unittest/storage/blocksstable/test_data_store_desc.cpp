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
#include <chrono>

#define protected public
#define private public

#include "storage/blocksstable/ob_macro_block.h"
#include "storage/test_schema_prepare.h"
#include "storage/blocksstable/ob_data_file_prepare.h"

namespace oceanbase
{
using namespace common;
using namespace storage;
using namespace blocksstable;
using namespace compaction;

namespace unittest
{
static ObSimpleMemLimitGetter getter;
class TestObDataStoreDesc : public blocksstable::TestDataFilePrepare
{
public:
  TestObDataStoreDesc()
    : blocksstable::TestDataFilePrepare(&getter, "test_data_store_desc"),
      mock_tablet_id_(1)
  {}
  ~TestObDataStoreDesc() = default;
  virtual void SetUp();
  virtual void TearDown();
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

  ObTabletID mock_tablet_id_;
};

void TestObDataStoreDesc::SetUp()
{
  TestDataFilePrepare::SetUp();
}

void TestObDataStoreDesc::TearDown()
{
  TestDataFilePrepare::TearDown();
}

TEST_F(TestObDataStoreDesc, test_static_desc)
{
  ObStaticDataStoreDesc static_desc;
  ObTableSchema table_schema;
  TestSchemaPrepare::prepare_schema(table_schema, 5);
  table_schema.compressor_type_ = ObCompressorType::ZSTD_1_3_8_COMPRESSOR;
  
  const int64_t snapshot = 10000;
  share::SCN scn;
  scn.convert_for_tx(100);
  ASSERT_EQ(OB_INVALID_ARGUMENT,
            static_desc.init(false/*is_ddl*/, table_schema, mock_tablet_id_,
                             MINI_MERGE, snapshot, share::SCN::invalid_scn(),
                             1 /*data_format_version*/, false /* micro_index_clustered */, 0/*concurrent_cnt*/));
  ASSERT_EQ(OB_SUCCESS,
            static_desc.init(false/*is_ddl*/, table_schema, mock_tablet_id_,
                             MINI_MERGE, snapshot, scn, 1 /*data_format_version*/,
                             false /* micro_index_clustered */, 0/*concurrent_cnt*/));
  ASSERT_TRUE(static_desc.is_valid());

  ASSERT_EQ(static_desc.is_ddl_, false);
  ASSERT_EQ(static_desc.merge_type_, MINI_MERGE);
  ASSERT_EQ(static_desc.tablet_id_, mock_tablet_id_);
  ASSERT_EQ(static_desc.compressor_type_, ObStaticDataStoreDesc::DEFAULT_MINOR_COMPRESSOR_TYPE);
  ASSERT_EQ(static_desc.schema_version_, table_schema.schema_version_);
  ASSERT_EQ(static_desc.snapshot_version_, snapshot);
  ASSERT_EQ(static_desc.end_scn_, scn);

  static_desc.reset();
  ASSERT_FALSE(static_desc.is_valid());

  ObStaticDataStoreDesc static_desc2;
  ASSERT_EQ(OB_SUCCESS,
            static_desc2.init(true/*is_ddl*/, table_schema, mock_tablet_id_,
                             MAJOR_MERGE, snapshot, scn, cal_version(1, 0, 0, 0), false /* micro_index_clustered */, 0/*concurrent_cnt*/));
  ASSERT_TRUE(static_desc2.is_valid());

  ASSERT_EQ(static_desc2.is_ddl_, true);
  ASSERT_EQ(static_desc2.merge_type_, MAJOR_MERGE);
  ASSERT_EQ(static_desc2.tablet_id_, mock_tablet_id_);
  ASSERT_EQ(static_desc2.compressor_type_, ObCompressorType::ZSTD_1_3_8_COMPRESSOR);
  ASSERT_EQ(static_desc2.schema_version_, table_schema.schema_version_);
  ASSERT_EQ(static_desc2.snapshot_version_, snapshot);
  ASSERT_EQ(static_desc2.end_scn_.val_, snapshot);
  static_desc2.progressive_merge_round_ = 1;
  static_desc2.macro_block_size_ = 100;
  static_desc2.macro_store_size_ = 100;
  static_desc2.micro_block_size_limit_ = 100;
  ObStaticDataStoreDesc static_desc3;
  ASSERT_EQ(OB_SUCCESS, static_desc3.assign(static_desc2));
  ASSERT_TRUE(static_desc3.is_valid());
  STORAGE_LOG(INFO, "cmp", K(static_desc2), K(static_desc3));
  ASSERT_TRUE(static_desc3 == static_desc2);
}

TEST_F(TestObDataStoreDesc, test_col_desc)
{
  const int64_t rowkey_cnt = 3;
  const int64_t col_cnt = 5;
  const int64_t mv_rowkey_cnt = ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  ObColDataStoreDesc col_desc;
  ObTableSchema table_schema;
  TestSchemaPrepare::prepare_schema(table_schema, rowkey_cnt, col_cnt);

  ASSERT_FALSE(col_desc.is_valid());
  ASSERT_EQ(OB_SUCCESS, col_desc.init(true/*is_major*/, table_schema, cal_version(1, 0, 0, 0)));
  ASSERT_TRUE(col_desc.is_valid());

  ASSERT_EQ(true, col_desc.default_col_checksum_array_valid_);
  ASSERT_EQ(rowkey_cnt, col_desc.schema_rowkey_col_cnt_);
  ASSERT_EQ(rowkey_cnt + mv_rowkey_cnt, col_desc.rowkey_column_count_);
  ASSERT_EQ(col_cnt + mv_rowkey_cnt, col_desc.row_column_count_);
  ASSERT_EQ(col_desc.full_stored_col_cnt_, col_desc.row_column_count_);
  ASSERT_EQ(col_desc.row_column_count_, col_desc.col_default_checksum_array_.count());
  ASSERT_EQ(col_desc.row_column_count_, col_desc.col_desc_array_.count());
}

TEST_F(TestObDataStoreDesc, test_whole_data_desc)
{
  const int64_t snapshot = 1;
  ObWholeDataStoreDesc whole_desc;
  ObTableSchema table_schema;
  TestSchemaPrepare::prepare_schema(table_schema, 5);
  ASSERT_EQ(OB_SUCCESS,
            whole_desc.init(false/*is_ddl*/, table_schema, mock_tablet_id_,
                            MAJOR_MERGE, snapshot, cal_version(1, 0, 0, 0),
                            table_schema.get_micro_index_clustered(),
                            0/*concurrent_cnt*/,
                            share::SCN::invalid_scn()));
  ASSERT_TRUE(whole_desc.is_valid());

  // point to other static desc member
  ObStaticDataStoreDesc static_desc;
  ASSERT_EQ(OB_INVALID_ARGUMENT,
            static_desc.init(false/*is_ddl*/, table_schema, mock_tablet_id_,
                             MINI_MERGE, snapshot,
                             share::SCN::invalid_scn(), 0/*data_format_version*/, false /* micro_index_clustered */, 0 /*concurrent_cnt*/));
  ASSERT_EQ(OB_SUCCESS,
            static_desc.init(false/*is_ddl*/, table_schema, mock_tablet_id_,
                             MAJOR_MERGE, snapshot,
                             share::SCN::invalid_scn(), cal_version(1, 0, 0, 0), false /* micro_index_clustered */, 0 /*concurrent_cnt*/));
  whole_desc.desc_.static_desc_ = &static_desc;
  ASSERT_FALSE(whole_desc.is_valid());
}

TEST_F(TestObDataStoreDesc, gen_index_desc)
{
  ObWholeDataStoreDesc data_desc;
  ObWholeDataStoreDesc index_desc;
  ObTableSchema table_schema;
  TestSchemaPrepare::prepare_schema(table_schema, 5);

  const int64_t snapshot = 10000;
  share::SCN scn;
  scn.convert_for_tx(100);
  ASSERT_EQ(OB_SUCCESS,
            data_desc.init(false/*is_ddl*/, table_schema, mock_tablet_id_,
                             MAJOR_MERGE, snapshot, DATA_CURRENT_VERSION,
                             table_schema.get_micro_index_clustered(), 0/*concurrent_cnt*/));
  ASSERT_TRUE(data_desc.is_valid());
  const ObDataStoreDesc &data_store_desc = data_desc.get_desc();

  ASSERT_EQ(OB_SUCCESS, index_desc.gen_index_store_desc(data_store_desc));
  
  const ObDataStoreDesc &index_data_desc = index_desc.get_desc();
  ASSERT_EQ(index_data_desc.get_row_column_count(), data_store_desc.get_rowkey_column_count() + 1);
  ASSERT_EQ(index_data_desc.get_col_desc_array().count(), data_store_desc.get_rowkey_column_count() + 1);
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -rf test_data_store_desc.log*");
  OB_LOGGER.set_file_name("test_data_store_desc.log");
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
