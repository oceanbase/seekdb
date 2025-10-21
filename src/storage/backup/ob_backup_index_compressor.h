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

#ifndef STORAGE_LOG_STREAM_BACKUP_INDEX_COMPRESSOR_H_
#define STORAGE_LOG_STREAM_BACKUP_INDEX_COMPRESSOR_H_

#include "lib/compress/ob_compressor.h"
#include "lib/compress/ob_compress_util.h"
#include "storage/blocksstable/ob_data_buffer.h"

namespace oceanbase
{
namespace backup
{

// This class represents a compressor for backup index blocks.
class ObBackupIndexBlockCompressor final
{
public:
  ObBackupIndexBlockCompressor();
  ~ObBackupIndexBlockCompressor();
  void reuse();
  void reset();
  int init(const int64_t block_size, const common::ObCompressorType type);
  int compress(const char *in, const int64_t in_size, const char *&out, int64_t &out_size);
  int decompress(const char *in, const int64_t in_size, const int64_t uncomp_size,
      const char *&out, int64_t &out_size);

private: 
  bool is_inited_;
  bool is_none_;
  int64_t block_size_;
  common::ObCompressor *compressor_;
  blocksstable::ObSelfBufferWriter comp_buf_;
  blocksstable::ObSelfBufferWriter decomp_buf_;
  DISALLOW_COPY_AND_ASSIGN(ObBackupIndexBlockCompressor);
};

}
}

#endif
