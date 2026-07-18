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

#ifndef OCEANBASE_UNITTEST_STORAGE_BLOCKSSTABLE_TEST_SSTABLE_GENERATOR_H_
#define OCEANBASE_UNITTEST_STORAGE_BLOCKSSTABLE_TEST_SSTABLE_GENERATOR_H_
#include "storage/blocksstable/ob_macro_block_writer.h"
#include "storage/blocksstable/slog/ob_base_storage_logger.h"
#include "storage/blocksstable/ob_macro_block_meta.h"
#include "storage/blocksstable/ob_macro_block_marker.h"
namespace oceanbase
{
using namespace common;
namespace blocksstable
{
class ObDataFile;
}
namespace unittest
{
class TestSSTableGenerator
{
public:
  TestSSTableGenerator();
  int open(
      blocksstable::ObDataStoreDesc &desc,
      const char *path,
      const int64_t row_count);
  int generate();
  int close();
  blocksstable::ObDataFile *get_data_file() { return &data_file_; }
private:
  int generate_row(const int64_t index);
private:
  char path_[OB_MAX_FILE_NAME_LENGTH];
  blocksstable::ObDataStoreDesc desc_;
  blocksstable::ObDataFile data_file_;
  blocksstable::ObBaseStorageLogger logger_;
  blocksstable::ObMacroBlockMetaImage image_;
  blocksstable::ObMacroBlockWriter writer_;
  blocksstable::ObMacroBlockMarker marker_;
  int64_t row_count_;
  common::ObObj cells_[common::OB_ROW_MAX_COLUMNS_COUNT];
  common::ModulePageAllocator mod_;
  common::ModuleArena arena_;
};
}//end namespace unittest
}//end namespace oceanbase
#endif
