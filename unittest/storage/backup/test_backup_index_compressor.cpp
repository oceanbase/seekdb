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
#define private public
#define protected public

#include "storage/backup/ob_backup_index_compressor.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::backup;
using namespace oceanbase::blocksstable;

namespace oceanbase
{
namespace backup
{

TEST(ObBackupIndexBlockCompressorTest, Compress)
{
  int ret = OB_SUCCESS;

  ObBackupIndexBlockCompressor compressor;
  const int64_t block_size = 16 * 1024;
  const ObCompressorType compressor_type = ObCompressorType::ZSTD_COMPRESSOR;

  ret = compressor.init(block_size, compressor_type);
  EXPECT_EQ(OB_SUCCESS, ret);

  int buffer_size = 16 * 1024;
  char buffer[buffer_size];

  for (int i = 0; i < buffer_size; ++i) {
      buffer[i] = std::rand() % 8;
  }

  const char* in = buffer;
  int64_t in_size = buffer_size;

  const char* out = NULL;
  int64_t out_size = 0;

  ret = compressor.compress(in, in_size, out, out_size);
  EXPECT_EQ(OB_SUCCESS, ret);

  ObBackupIndexBlockCompressor decompressor;

  ret = decompressor.init(block_size, compressor_type);
  EXPECT_EQ(OB_SUCCESS, ret);

  const char *decomp_out = NULL;
  int64_t decomp_size = 0;

  ret = decompressor.decompress(out, out_size, block_size, decomp_out, decomp_size);
  EXPECT_EQ(OB_SUCCESS, ret);

  EXPECT_EQ(in_size, decomp_size);
  EXPECT_LE(out_size, in_size);

  LOG_INFO("compress info", K(in_size), K(out_size), K(decomp_size));
}

}
}

int main(int argc, char **argv)
{
  system("rm -f test_backup_index_compressor.log*");
  ObLogger &logger = ObLogger::get_logger();
  logger.set_file_name("test_backup_index_compressor.log", true);
  logger.set_log_level("info");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

