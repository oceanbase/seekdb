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
#include "storage/ob_bloom_filter_task.h"
#include "storage/blocksstable/ob_macro_block_bare_iterator.h"
#include "storage/blocksstable/ob_storage_cache_suite.h"

namespace oceanbase
{
namespace storage
{

using namespace oceanbase::common;
using namespace oceanbase::common::hash;
using namespace oceanbase::blocksstable;
using namespace share;


/*
 * ObBloomFilterBuildTask
 * */

ObBloomFilterBuildTask::ObBloomFilterBuildTask(
    const uint64_t table_id,
    const blocksstable::MacroBlockId &macro_id,
    const int64_t prefix_len)
    : IObDedupTask(T_BLOOMFILTER),
      table_id_(table_id),
      macro_id_(macro_id),
      macro_handle_(),
      prefix_len_(prefix_len),
      allocator_(ObModIds::OB_BLOOM_FILTER),
      io_buf_(nullptr)
{
  abort_unless(OB_SUCCESS == macro_handle_.set_macro_block_id(macro_id));
}

ObBloomFilterBuildTask::~ObBloomFilterBuildTask()
{
}

int64_t ObBloomFilterBuildTask::hash() const
{
  uint64_t hash_val = macro_id_.hash();
  hash_val = murmurhash(&table_id_, sizeof(uint64_t), hash_val);
  hash_val = murmurhash(&prefix_len_, sizeof(int64_t), hash_val);
  return hash_val;
}

bool ObBloomFilterBuildTask::operator ==(const IObDedupTask &other) const
{
  bool is_equal = false;
  if (this == &other) {
    is_equal = true;
  } else {
    if (get_type() == other.get_type()) {
      // it's safe to do this transformation, we have checked the task's type
      const ObBloomFilterBuildTask &o = static_cast<const ObBloomFilterBuildTask &>(other);
      is_equal = true && o.table_id_ == table_id_
                 && o.macro_id_ == macro_id_ && o.prefix_len_ == prefix_len_;
    }
  }
  return is_equal;
}

int64_t ObBloomFilterBuildTask::get_deep_copy_size() const
{
  return sizeof(*this);
}

IObDedupTask *ObBloomFilterBuildTask::deep_copy(char *buffer, const int64_t buf_size) const
{
  ObBloomFilterBuildTask *task = NULL;
  if (NULL != buffer && buf_size >= get_deep_copy_size()) {
    task = new (buffer) ObBloomFilterBuildTask(
        table_id_,
        macro_id_,
        prefix_len_);
  }
  return task;
}

int ObBloomFilterBuildTask::process()
{
  int ret = OB_SUCCESS;
  ObBloomFilterCacheValue bfcache_value;

  if (OB_UNLIKELY(false)
      || OB_UNLIKELY(!macro_id_.is_valid())
      || OB_UNLIKELY(prefix_len_ <= 0)) {
    ret = OB_INVALID_DATA;
    LOG_WARN("The bloom filter build task is not valid, ",
      K_(macro_id), K_(prefix_len), K(ret));
  } else if (OB_FAIL(build_bloom_filter())) {
  } else {
    LOG_INFO("Success to build bloom filter, ", K_(table_id), K_(macro_id), K_(prefix_len));
  }

  return ret;
}

int ObBloomFilterBuildTask::build_bloom_filter()
{
  int ret = OB_SUCCESS;

  MOD_SCOPE {
    void *buf = nullptr;
    ObStoreCtx store_ctx;
    bool need_build = false;

    ObBloomFilterCacheValue bfcache_value;
    ObStorageObjectHandle macro_handle;
    ObStorageObjectReadInfo read_info;
    ObMacroBlockRowBareIterator *macro_bare_iter = nullptr;
    ObSSTableMacroBlockHeader macro_header;
    const ObDatumRow *row = nullptr;
    lib::Worker::CompatMode compat_mode = lib::Worker::CompatMode::MYSQL;
    {
      THIS_WORKER.set_compatibility_mode(compat_mode);
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(OB_STORE_CACHE.get_bf_cache().check_need_build(ObBloomFilterCacheKey(
        macro_id_, prefix_len_), need_build))) {
    } else if (!need_build) {
      //already in cache,do nothing
    } else if (OB_ISNULL(buf = ob_malloc(sizeof(ObMacroBlockRowBareIterator), ObModIds::OB_BLOOM_FILTER))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("Fail to allocate memory, ", "size", sizeof(ObMacroBlockRowBareIterator), K(ret));
    } else {
      macro_bare_iter = new (buf) ObMacroBlockRowBareIterator(allocator_);
      // submit io
      read_info.macro_block_id_ = macro_id_;
      read_info.offset_ = 0;
      read_info.size_ = OB_DEFAULT_MACRO_BLOCK_SIZE;
      read_info.io_desc_.set_mode(ObIOMode::READ);
      read_info.io_desc_.set_wait_event(ObWaitEventIds::DB_FILE_DATA_READ);
      read_info.io_desc_.set_sys_module_id(ObIOModule::BLOOM_FILTER_IO);
      read_info.io_timeout_ms_ = std::max(GCONF._data_storage_io_timeout / 1000, DEFAULT_IO_WAIT_TIME_MS);
      


      if (OB_ISNULL(io_buf_) && OB_ISNULL(io_buf_ =
          reinterpret_cast<char*>(allocator_.alloc(OB_DEFAULT_MACRO_BLOCK_SIZE)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        STORAGE_LOG(WARN, "failed to alloc macro read info buffer", K(ret), K(OB_DEFAULT_MACRO_BLOCK_SIZE));
      } else {
        read_info.buf_ = io_buf_;
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ObObjectManager::read_object(read_info, macro_handle))) {
      } else if (OB_FAIL(macro_bare_iter->open(
          read_info.buf_, macro_handle.get_data_size(), true /*check*/))) {
      } else if (OB_FAIL(macro_bare_iter->get_macro_block_header(macro_header))) {
      } else if (OB_UNLIKELY(!macro_header.is_valid() || macro_header.is_normal_cg_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Invalid macro block header", K(ret), K(macro_header));
      } else if (OB_FAIL(bfcache_value.init(prefix_len_, macro_header.fixed_header_.row_count_))) {
      } else {
        ObStorageDatumUtils datum_utils;
        ObDatumRowkey rowkey;
        if (OB_FAIL(datum_utils.init(macro_bare_iter->get_rowkey_column_descs(),
                                     macro_header.fixed_header_.rowkey_column_count_,
                                     allocator_))) {
        }
        while (OB_SUCC(ret) && OB_SUCC(macro_bare_iter->get_next_row(row))) {
          uint64_t key_hash = 0;
          if (OB_FAIL(rowkey.assign(row->storage_datums_, prefix_len_))) {
          } else if (OB_FAIL(rowkey.murmurhash(0, datum_utils, key_hash))) {
          } else if (OB_FAIL(bfcache_value.insert(static_cast<uint32_t>(key_hash)))) {
          }
        }
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("Fail to iterate macro block", K(ret));
        } else if (OB_FAIL(ObStorageCacheSuite::get_instance().get_bf_cache().put_bloom_filter(macro_id_, bfcache_value, true/* adaptive */))) {
        }
      }

      if (OB_NOT_NULL(macro_bare_iter)) {
        macro_bare_iter->~ObMacroBlockRowBareIterator();
        ob_free(macro_bare_iter);
      }
    }
    macro_handle_.reset();
  }

  return ret;
}

} // namespace storage
} // namespace oceanbase

