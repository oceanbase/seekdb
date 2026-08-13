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

#include "storage/slog_ckpt/ob_server_checkpoint_writer.h"
#include "storage/api/storage/runtime/ob_i_server_runtime.h"
#include "storage/meta_store/ob_server_runtime_meta.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
namespace storage
{

using namespace oceanbase::common;
using namespace oceanbase::blocksstable;

int ObServerCheckpointWriter::init(
    ObStorageLogger *server_slogger, ObIServerRuntime &server_runtime)
{
  int ret = OB_SUCCESS;
  const int64_t MEM_LIMIT = 128 << 20;  // 128M
  const char *MEM_LABEL = "ObServerCheckpointWriter";
  ObMemAttr mem_attr(MEM_LABEL);

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObServerCheckpointWriter init twice", K(ret));
  } else if (OB_FAIL(allocator_.init(
               common::OB_MALLOC_NORMAL_BLOCK_SIZE, MEM_LABEL, MEM_LIMIT))) {
  } else if (OB_FAIL(runtime_meta_item_writer_.init(false /*whether need addr*/, mem_attr))) {
  } else {
    server_slogger_ = server_slogger;
    server_runtime_ = &server_runtime;
    is_inited_ = true;
  }
  return ret;
}

int ObServerCheckpointWriter::write_checkpoint(const ObLogCursor &log_cursor)
{
  int ret = OB_SUCCESS;
  LOG_INFO("start to write server checkpoint", K(log_cursor));

  MacroBlockId runtime_meta_entry;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObServerCheckpointWriter not init", K(ret));
  } else if (OB_UNLIKELY(!log_cursor.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (OB_FAIL(write_runtime_meta_checkpoint(runtime_meta_entry))) {
  } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.update_super_block(log_cursor, runtime_meta_entry))) {
  } else if (OB_FAIL(server_slogger_->remove_useless_log_file(log_cursor.file_id_))) {
  } else {
    LOG_INFO("succeed to write server checkpoint", K(log_cursor), K(runtime_meta_entry));
  }

  return ret;
}

int ObServerCheckpointWriter::write_runtime_meta_checkpoint(MacroBlockId &block_entry)
{
  int ret = OB_SUCCESS;

  char *buf = nullptr;
  int64_t buf_len = 0;
  int64_t pos = 0;

  omt::ObServerRuntimeMeta meta;
  bool exist = false;
  if (OB_FAIL(server_runtime_->get_runtime_meta_for_ckpt(meta, exist))) {
  } else if (exist) {
    // The project keeps one local OMT runtime. Persist the current mainline
    // runtime metadata directly; the removed database-runtime bootstrap format
    // must not be mixed into this checkpoint.
    buf_len = meta.get_serialize_size();
    pos = 0;
    if (OB_ISNULL(buf = static_cast<char *>(allocator_.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to allocate memory", K(ret));
    } else if (OB_FAIL(meta.serialize(buf, buf_len, pos))) {
    } else if (OB_FAIL(runtime_meta_item_writer_.write_item(buf, buf_len, nullptr))) {
    }
    if (OB_LIKELY(nullptr != buf)) {
      allocator_.free(buf);
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(runtime_meta_item_writer_.close())) {
    } else if (OB_FAIL(runtime_meta_item_writer_.get_entry_block(block_entry))) {
    }
  }

  return ret;
}

ObIArray<MacroBlockId> &ObServerCheckpointWriter::get_meta_block_list()
{
  return runtime_meta_item_writer_.get_meta_block_list();
}



}  // end namespace storage
}  // end namespace oceanbase
