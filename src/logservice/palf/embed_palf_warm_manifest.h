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

#ifndef OCEANBASE_LOGSERVICE_EMBED_PALF_WARM_MANIFEST_H_
#define OCEANBASE_LOGSERVICE_EMBED_PALF_WARM_MANIFEST_H_

#include "share/log/palf/log_define.h"

namespace oceanbase
{
namespace palf
{

static const int64_t EMBED_PALF_WARM_MANIFEST_MAGIC = 0x50414C465741524D; // "PALFWARM"
static const int64_t EMBED_PALF_WARM_MANIFEST_VERSION = 1;
static const char EMBED_PALF_WARM_MANIFEST_FILE[] = ".embed_palf_warm_manifest";
static const char EMBED_PALF_WARM_MANIFEST_TMP_FILE[] = ".embed_palf_warm_manifest.tmp";

struct EmbedPalfWarmStorageSnapshot
{
  block_id_t min_block_id_;
  block_id_t max_block_id_;
  int64_t log_tail_lsn_val_;
  int64_t last_entry_start_lsn_val_;
  int32_t entry_header_len_;
  char entry_header_buf_[MAX_LOG_HEADER_SIZE];

  void reset()
  {
    min_block_id_ = LOG_INVALID_BLOCK_ID;
    max_block_id_ = LOG_INVALID_BLOCK_ID;
    log_tail_lsn_val_ = 0;
    last_entry_start_lsn_val_ = 0;
    entry_header_len_ = 0;
    memset(entry_header_buf_, 0, sizeof(entry_header_buf_));
  }

  bool is_valid() const
  {
    return LOG_INVALID_BLOCK_ID != min_block_id_
           && LOG_INVALID_BLOCK_ID != max_block_id_
           && entry_header_len_ > 0
           && entry_header_len_ <= MAX_LOG_HEADER_SIZE
           && last_entry_start_lsn_val_ >= 0
           && log_tail_lsn_val_ > 0;
  }
};

struct EmbedPalfWarmManifest
{
  int64_t magic_;
  int64_t version_;
  int64_t payload_checksum_;
  int64_t write_ts_us_;
  EmbedPalfWarmStorageSnapshot meta_;
  EmbedPalfWarmStorageSnapshot redo_;

  void reset()
  {
    magic_ = 0;
    version_ = 0;
    payload_checksum_ = 0;
    write_ts_us_ = 0;
    meta_.reset();
    redo_.reset();
  }

  bool is_valid() const
  {
    return EMBED_PALF_WARM_MANIFEST_MAGIC == magic_
           && EMBED_PALF_WARM_MANIFEST_VERSION == version_
           && meta_.is_valid()
           && redo_.is_valid();
  }
};

#ifdef OB_BUILD_EMBED_MODE
bool is_embed_palf_warm_manifest_filename(const char *file_name);
int load_embed_palf_warm_manifest(const char *log_stream_dir, EmbedPalfWarmManifest &manifest);
int save_embed_palf_warm_manifest(const char *log_stream_dir, const EmbedPalfWarmManifest &manifest);
int delete_embed_palf_warm_manifest(const char *log_stream_dir);
#endif

} // namespace palf
} // namespace oceanbase

#endif // OCEANBASE_LOGSERVICE_EMBED_PALF_WARM_MANIFEST_H_
