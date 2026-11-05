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

#include "log_meta.h"
#include "share/storage/local_store.h"
#include "share/inner_table/local_table_schema.h"

namespace oceanbase
{
namespace palf
{
using namespace common;
using namespace share;
LogMeta::LogMeta() : version_(-1),
                     log_mode_meta_(),
                     log_snapshot_meta_()
{
}

LogMeta::~LogMeta() { reset(); }

LogMeta::LogMeta(const LogMeta &rmeta) { *this = rmeta; }

int LogMeta::generate_by_palf_base_info(const PalfBaseInfo &palf_base_info,
                                        const AccessMode &access_mode)
{
  int ret = OB_SUCCESS;
  if (false == is_valid_access_mode(access_mode) || false == palf_base_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(INFO, "invalid argument", KPC(this), K(access_mode), K(palf_base_info));
  } else if (OB_FAIL(log_snapshot_meta_.generate(palf_base_info.curr_lsn_, palf_base_info.prev_log_info_, palf_base_info.curr_lsn_))) {
  } else {
    const SCN &prev_scn = palf_base_info.prev_log_info_.scn_;
    const SCN init_ref_scn = (prev_scn.is_valid() ? prev_scn: SCN::min_scn());
    version_ = LOG_META_VERSION;
    log_mode_meta_.generate(access_mode, init_ref_scn);
    PALF_LOG(INFO, "generate_by_palf_base_info success", KPC(this));
  }
  return ret;
}

bool LogMeta::is_valid() const
{
  return LOG_META_VERSION == version_
         && true == log_mode_meta_.is_valid()
         && true == log_snapshot_meta_.is_valid();
}

void LogMeta::reset()
{
  version_ = -1;
  log_mode_meta_.reset();
  log_snapshot_meta_.reset();
  version_ = -1;
}

int LogMeta::update_log_snapshot_meta(const LogSnapshotMeta &log_snapshot_meta)
{
  int ret = OB_SUCCESS;
  if (false == log_snapshot_meta.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(log_snapshot_meta));
  } else {
    log_snapshot_meta_ = log_snapshot_meta;
  }
  return ret;
}

void LogMeta::operator=(const LogMeta &log_meta)
{
  version_ = log_meta.version_;
  log_mode_meta_ = log_meta.log_mode_meta_;
  log_snapshot_meta_ = log_meta.log_snapshot_meta_;
}

DEFINE_SERIALIZE(LogMeta)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (OB_UNLIKELY(NULL == buf || buf_len < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, new_pos, version_))
             || OB_FAIL(log_mode_meta_.serialize(buf, buf_len, new_pos))
             || OB_FAIL(log_snapshot_meta_.serialize(buf, buf_len, new_pos))) {
    PALF_LOG(ERROR, "LogMeta serialize failed", K(ret), K(buf), K(buf_len), K(pos), K(new_pos));
  } else {
    pos = new_pos;
    PALF_LOG(INFO, "LogMeta serialize", K(*this), K(buf), KP(buf), K(pos));
  }
  return ret;
}

DEFINE_DESERIALIZE(LogMeta)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (OB_UNLIKELY(NULL == buf || data_len < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(serialization::decode_i64(buf, data_len, new_pos, &version_))) {
  } else if (LOG_META_VERSION != version_) {
    ret = OB_NOT_SUPPORTED;
    PALF_LOG(ERROR, "unsupported LogMeta version", K(ret), K_(version));
  } else if (OB_FAIL(log_mode_meta_.deserialize(buf, data_len, new_pos))
             || OB_FAIL(log_snapshot_meta_.deserialize(buf, data_len, new_pos))) {
    PALF_LOG(ERROR, "LogMeta deserialize failed", K(ret), K(buf), K(data_len), K(pos), K(new_pos));
  } else {
    PALF_LOG(INFO, "LogMeta deserialize", K(buf), K(buf + pos), K(pos), K(new_pos), K(*this));
    pos = new_pos;
  }
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(LogMeta)
{
  int64_t size = 0;
  size += serialization::encoded_length_i64(version_);
  size += log_mode_meta_.get_serialize_size();
  size += log_snapshot_meta_.get_serialize_size();
  return size;
}

namespace meta_store {
using namespace share;
using namespace share::palf_meta;
namespace {
int check_commit(int ret)
{
  if (OB_IO_ERROR == ret) {
    PALF_LOG(ERROR, "local PALF metadata commit is uncertain; stop before changing log files", K(ret));
    ob_abort();
  }
  return ret;
}

int write_meta(LocalStore &store, const LogMeta &meta, bool create)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator("PalfMeta");
  const int64_t size = meta.get_serialize_size();
  char *buf = static_cast<char *>(allocator.alloc(size));
  int64_t pos = 0;
  int64_t affected = 0;
  if (!meta.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(meta.serialize(buf, size, pos))) {
  } else {
    // Log IO advances the manifest independently; update only the snapshot.
    auto request = create ? LocalRequest::upsert(schema()) : LocalRequest::update(schema());
    request.set(Payload, LocalValue::blob(buf, pos));
    if (create) {
      request.set(Id, int64_t(0)).set(ExpectedNextBlockId, int64_t(0));
    } else {
      request.eq(Id, int64_t(0));
    }
    if (OB_FAIL(check_commit(store.execute(request, &affected)))) {
    } else if (affected != 1) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}
}

int create(LocalStore &store, const LogMeta &meta)
{
  return write_meta(store, meta, true);
}

int save(LocalStore &store, const LogMeta &meta)
{
  return write_meta(store, meta, false);
}

int load(LocalStore &store, LogMeta &meta, block_id_t &expected_next_block_id)
{
  return store.query(LocalRequest::select(schema(), {Payload, ExpectedNextBlockId}).eq(Id, int64_t(0)),
      [&](LocalRowReader &row) {
        int ret = OB_SUCCESS;
        int size = 0;
        const char *buf = static_cast<const char *>(row.get_blob(0, &size));
        const int64_t block_id = row.get_int64(1);
        int64_t pos = 0;
        if (OB_ISNULL(buf) || size <= 0 || block_id < 0 || block_id >= LOG_INVALID_BLOCK_ID) {
          ret = OB_INVALID_DATA;
        } else if (OB_FAIL(meta.deserialize(buf, size, pos))) {
        } else if (pos != size || !meta.is_valid()) {
          ret = OB_INVALID_DATA;
        } else {
          expected_next_block_id = static_cast<block_id_t>(block_id);
        }
        return ret;
      });
}

int update_manifest(LocalStore &store, block_id_t expected_next_block_id)
{
  int ret = OB_SUCCESS;
  int64_t affected = 0;
  if (!is_valid_block_id(expected_next_block_id)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(check_commit(store.execute(LocalRequest::update(schema()).eq(Id, int64_t(0))
      .set(ExpectedNextBlockId, static_cast<int64_t>(expected_next_block_id)), &affected)))) {
  } else if (affected != 1) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int remove(LocalStore &store)
{
  return check_commit(store.execute(LocalRequest::erase(schema()).eq(Id, int64_t(0))));
}
} // namespace meta_store
} // end namespace palf
} // end namespace oceanbase
