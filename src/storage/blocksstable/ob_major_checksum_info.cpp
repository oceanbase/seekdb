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
#include "ob_major_checksum_info.h"
#include "storage/compaction/ob_basic_tablet_merge_ctx.h"
namespace oceanbase
{
using namespace compaction;
namespace blocksstable
{
ObMajorChecksumInfo::ObMajorChecksumInfo()
  : version_(MAJOR_CHECKSUM_INFO_VERSION_V1),
    exec_mode_(EXEC_MODE_MAX),
    reserved_(0),
    compaction_scn_(0),
    row_count_(0),
    data_checksum_(0),
    column_ckm_struct_()
{
}

void ObMajorChecksumInfo::reset()
{
  exec_mode_ = EXEC_MODE_MAX;
  reserved_ = 0;
  compaction_scn_ = 0;
  row_count_ = 0;
  data_checksum_ = 0;
  column_ckm_struct_.reset();
}

bool ObMajorChecksumInfo::is_empty() const
{
  return compaction_scn_ == 0;
}

bool ObMajorChecksumInfo::is_valid() const
{
  return row_count_ >= 0
    && ((0 == compaction_scn_ && column_ckm_struct_.is_empty())
    || (compaction_scn_ > 0 && !column_ckm_struct_.is_empty() && compaction::is_valid_exec_mode(get_exec_mode())));
}

int ObMajorChecksumInfo::assign(
  const ObMajorChecksumInfo &other,
  ObArenaAllocator *allocator)
{
  int ret = OB_SUCCESS;
  reset();
  info_ = other.info_;
  compaction_scn_ = other.compaction_scn_;
  if (other.is_empty()) {
    // do nothing
  } else if (OB_ISNULL(allocator)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("input allocator is null", KR(ret), K(allocator));
  } else {
    row_count_ = other.row_count_;
    data_checksum_ = other.data_checksum_;
    if (OB_FAIL(column_ckm_struct_.assign(*allocator, other.column_ckm_struct_))) {
      LOG_WARN("failed to assgin column checksums", KR(ret), K(other));
    }
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObMajorChecksumInfo is invalid after assign", KR(ret), KPC(this), K(other));
  }
  return ret;
}

int ObMajorChecksumInfo::deep_copy(
      char *buf,
      const int64_t buf_len,
      int64_t &pos,
      ObMajorChecksumInfo &dest) const
{
  int ret = OB_SUCCESS;
  dest.reset();
  dest.info_ = info_;
  dest.compaction_scn_ = compaction_scn_;
  if (!is_empty()) {
    dest.row_count_ = row_count_;
    dest.data_checksum_ = data_checksum_;
    if (OB_FAIL(column_ckm_struct_.deep_copy(buf, buf_len, pos, dest.column_ckm_struct_))) {
      LOG_WARN("failed to deep copy column checksum", KR(ret), K_(column_ckm_struct));
    }
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(!dest.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObMajorChecksumInfo is invalid after deep copy", KR(ret), KPC(this), K(dest));
  }
  return ret;
}

int ObMajorChecksumInfo::init_from_merge_result(
    ObArenaAllocator &allocator,
    const compaction::ObBasicTabletMergeCtx &ctx,
    const blocksstable::ObSSTableMergeRes &res)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!res.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(res));
  } else {
    compaction_scn_ = ctx.get_merge_version();
    row_count_ = res.row_count_;
    data_checksum_ = res.data_checksum_;
    exec_mode_ = ctx.get_exec_mode();
    if (OB_FAIL(column_ckm_struct_.assign(allocator, res.data_column_checksums_))) {
      LOG_WARN("fail to assign column checksum array", K(ret), KPC(this), K(res));
    } else if (OB_UNLIKELY(!is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("after init from merge result, major checksum info is not valid", KR(ret), KPC(this));
    } else {
      LOG_INFO("success to init ckm info from merge result", KR(ret), KPC(this));
    }
  }
  return ret;
}

int ObMajorChecksumInfo::init_from_sstable(
    ObArenaAllocator &allocator,
    const compaction::ObExecMode exec_mode,
    const storage::ObStorageSchema &storage_schema,
    const blocksstable::ObSSTable &sstable)
{
  int ret = OB_SUCCESS;
  UNUSED(storage_schema);
  if (OB_UNLIKELY(!sstable.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(sstable));
  } else {
    compaction_scn_ = sstable.get_snapshot_version();
    row_count_ = sstable.get_row_count();
    data_checksum_ = sstable.get_data_checksum();
    exec_mode_ = exec_mode;
    ObSEArray<int64_t, OB_ROW_DEFAULT_COLUMNS_COUNT> tmp_col_ckm_array;
    tmp_col_ckm_array.set_attr(ObMemAttr("MajorCkmInfo"));
    if (OB_FAIL(sstable.fill_column_ckm_array(tmp_col_ckm_array))) {
      LOG_WARN("fail to fill column checksum array", K(ret), KPC(this), K(sstable));
    }
    if (FAILEDx(column_ckm_struct_.assign(allocator, tmp_col_ckm_array))) {
      LOG_WARN("fail to assign column checksum array", K(ret), KPC(this), K(sstable));
    } else if (OB_UNLIKELY(!is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("after init from sstable, major checksum info is not valid", KR(ret), KPC(this));
    } else {
      LOG_INFO("success to init ckm info from sstable", KR(ret), KPC(this));
    }
  }
  return ret;
}

int64_t ObMajorChecksumInfo::get_serialize_size() const
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, info_, compaction_scn_);
  if (!is_empty()) {
    LST_DO_CODE(OB_UNIS_ADD_LEN, row_count_, data_checksum_);
    len += column_ckm_struct_.get_serialize_size();
  }
  return len;
}

int ObMajorChecksumInfo::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, info_, compaction_scn_);
  if (!is_empty()) {
    LST_DO_CODE(OB_UNIS_ENCODE, row_count_, data_checksum_);
    if (OB_FAIL(column_ckm_struct_.serialize(buf, buf_len, pos))) {
      LOG_WARN("Fail to serialize column checksum", K(ret), K_(column_ckm_struct));
    }
  }
  return ret;
}

int ObMajorChecksumInfo::deserialize(common::ObArenaAllocator &allocator, const char *buf,
                                     const int64_t data_len, int64_t &pos) {
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, info_, compaction_scn_);
  if (!is_empty()) {
    LST_DO_CODE(OB_UNIS_DECODE, row_count_, data_checksum_);
    if (OB_FAIL(column_ckm_struct_.deserialize(allocator, buf, data_len, pos))) {
      LOG_WARN("Fail to deserialize column count", K(ret));
    } else if (OB_UNLIKELY(!is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("deserialize ckm info is not valid", K(ret), KPC(this));
    }
  }
  return ret;
}

void ObMajorChecksumInfo::gene_info(char* buf, const int64_t buf_len, int64_t &pos) const
{
  if (OB_ISNULL(buf) || buf_len <= 0) {
    // do nothing
  } else {
    J_OBJ_START();
    BUF_PRINTF("ObMajorChecksumInfo");
    J_COLON();
    J_KV("compaction_scn", get_compaction_scn(), "exec_mode", exec_mode_to_str(get_exec_mode()));
    J_OBJ_END();
  }
}

int64_t ObMajorChecksumInfo::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len <= 0)) {
    // do nothing
  } else {
    J_OBJ_START();
    J_KV("is_empty", is_empty());
    if (!is_empty() || !is_valid()) {
      J_COMMA();
      J_KV(K_(compaction_scn), 
        "exec_mode", compaction::exec_mode_to_str(get_exec_mode()),
        K_(row_count), K_(data_checksum), K_(column_ckm_struct));
    }
    J_OBJ_END();
  }
  return pos;
}

} // namespace compaction
} // namespace oceanbase
