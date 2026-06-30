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

#include "ob_tx_table_define.h"
#include "storage/tx_table/ob_tx_data_table.h"

namespace oceanbase
{
namespace storage
{

void *TxDataDefaultAllocator::alloc(const int64_t size)
{
  common::ObMemAttr attr;
  
  attr.label_ = "TX_DATA_ITER";
  if (size <= 0) {
    abort();
  }
  return ob_malloc(size, attr);
}

int ObTxCtxTableCommonHeader::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, MAGIC_VERSION_))) {
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, DATA_LEN_))) {
  }
  return ret;
}

int ObTxCtxTableCommonHeader::deserialize(const char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t des_version = 0;
  int64_t des_data_len = 0;

  if (OB_UNLIKELY(nullptr == buf || buf_len <= 0 || pos > buf_len)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid arguments.", KP(buf), K(buf_len), K(ret));
  } else if (OB_FAIL(serialization::decode_vi64(buf, buf_len, pos, &des_version))) {
  } else if (OB_FAIL(serialization::decode_vi64(buf, buf_len, pos, &des_data_len))) {
  } else if (des_version != MAGIC_VERSION_) {
    ret = OB_NOT_SUPPORTED;
    TRANS_LOG(WARN, "object des_version mismatch", K(ret), K(des_version));
  } else if (des_data_len < 0) {
    ret = OB_ERR_UNEXPECTED;
  } else if (buf_len < des_data_len + pos) {
    ret = OB_DESERIALIZE_ERROR;
  }

  return ret;
}

int64_t ObTxCtxTableCommonHeader::get_serialize_size() const
{
  int64_t len = 0;
  len += serialization::encoded_length_vi64(MAGIC_VERSION_);
  len += serialization::encoded_length_vi64(DATA_LEN_);
  return len;
}

int ObTxCtxTableInfo::serialize(char *buf,
                                const int64_t buf_len,
                                int64_t &pos) const
{
  int ret = OB_SUCCESS;
  const int64_t data_len = get_serialize_size_();
  ObTxCtxTableCommonHeader header(MAGIC_VERSION, data_len);
  if (OB_ISNULL(tx_data_guard_.tx_data())) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "invalid tx data guard", KR(ret), KPC(this));
  } else if (OB_FAIL(header.serialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(serialize_(buf, buf_len, pos))) {
  }
  return ret;
}

int ObTxCtxTableInfo::serialize_(char *buf,
                                 const int64_t buf_len,
                                 int64_t &pos) const
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(tx_id_.serialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(ls_id_.serialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, cluster_id_))) {
  } else if (OB_FAIL(tx_data_guard_.tx_data()->serialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(exec_info_.serialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(table_lock_info_.serialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, (int64_t)cluster_version_))) {
  }

  return ret;
}

int ObTxCtxTableInfo::deserialize(const char *buf,
                                  const int64_t buf_len,
                                  int64_t &pos,
                                  ObTxDataTable &tx_data_table)
{
  int ret = OB_SUCCESS;
  ObTxCtxTableCommonHeader header(MAGIC_VERSION, 0);

  if (OB_FAIL(tx_data_table.alloc_tx_data(tx_data_guard_, false/* enable_throttle */))) {
  } else if (OB_FAIL(header.deserialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(deserialize_(buf, buf_len, pos, tx_data_table))) {
  }
  return ret;
}

int ObTxCtxTableInfo::deserialize_(const char *buf,
                                   const int64_t buf_len,
                                   int64_t &pos,
                                   ObTxDataTable &tx_data_table)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(tx_id_.deserialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(ls_id_.deserialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(serialization::decode_vi64(buf, buf_len, pos, &cluster_id_))) {
  } else if (OB_FAIL(tx_data_guard_.tx_data()->deserialize(buf, buf_len, pos, *tx_data_table.get_tx_data_allocator()))) {
  } else if (OB_FAIL(exec_info_.deserialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(table_lock_info_.deserialize(buf, buf_len, pos))) {
  }
  // _NOTE_
  // before 4.2.1.1, the serialize size of table_lock_info_
  // is not accurate(which larger than real serialize size),
  // this caused the size of ObTxCtxTableInfo is also inaccurate
  //
  // when deserialize use `compatible_version_` to decide whether
  // guess extra members by examine remain buf size
  if (OB_SUCC(ret) && compatible_version_ >= ObTxCtxTableMeta::VERSION_1 && buf_len > pos) {
    // has remains, continue to deserialize new members
    if (OB_FAIL(serialization::decode_vi64(buf, buf_len, pos, (int64_t*)&cluster_version_))) {
    }
  }

  return ret;
}

int64_t ObTxCtxTableInfo::get_serialize_size(void) const
{
  int64_t data_len = get_serialize_size_();
  ObTxCtxTableCommonHeader header(MAGIC_VERSION, data_len);

  int64_t len = 0;
  len += header.get_serialize_size();
  len += data_len;

  return len;
}

int64_t ObTxCtxTableInfo::get_serialize_size_(void) const
{
  int64_t len = 0;
  len += tx_id_.get_serialize_size();
  len += ls_id_.get_serialize_size();
  len += serialization::encoded_length_vi64(cluster_id_);
  len += serialization::encoded_length_vi64((int64_t)cluster_version_);
  len += (OB_NOT_NULL(tx_data_guard_.tx_data()) ? tx_data_guard_.tx_data()->get_serialize_size() : 0);
  len += exec_info_.get_serialize_size();
  len += table_lock_info_.get_serialize_size();
  return len;
}

bool ObTxCtxTableInfo::is_valid() const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tx_id_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "tx id not valid", K(ret), K(tx_id_));
  } else if (OB_UNLIKELY(!ls_id_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "ls id not valid", K(ret), K(ls_id_));
    // TODO: gengli  this is invalid if it a uncommited tx
    // } else if (OB_UNLIKELY(!state_info_.is_valid())) {
    //   ret = OB_ERR_UNEXPECTED;
    //   TRANS_LOG(ERROR, "state info not valid", K(ret), K(state_info_));
  } else {
    // do nothing
  }
  return OB_SUCC(ret);
}

int ObTxCtxTableMeta::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  const int64_t data_len = get_serialize_size_();
  ObTxCtxTableCommonHeader header(MAGIC_VERSION, data_len);
  if (OB_FAIL(header.serialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(serialize_(buf, buf_len, pos))) {
  }
  return ret;
}

int ObTxCtxTableMeta::serialize_(char* buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(tx_id_.serialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(ls_id_.serialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, tx_ctx_serialize_size_))) {
  } else if (OB_FAIL(serialization::encode_vi32(buf, buf_len, pos, row_num_))) {
  } else if (OB_FAIL(serialization::encode_vi32(buf, buf_len, pos, row_idx_))) {
  } else if (OB_FAIL(serialization::encode_vi32(buf, buf_len, pos, version_))) {
  } else {
  }
  return ret;
}

int ObTxCtxTableMeta::deserialize(const char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  ObTxCtxTableCommonHeader header(MAGIC_VERSION, 0);

  if (OB_FAIL(header.deserialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(deserialize_(buf, buf_len, pos))) {
  }
  return ret;
}

int ObTxCtxTableMeta::deserialize_(const char* buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(tx_id_.deserialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(ls_id_.deserialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(serialization::decode_vi64(buf, buf_len, pos, &tx_ctx_serialize_size_))) {
  } else if (OB_FAIL(serialization::decode_vi32(buf, buf_len, pos, &row_num_))) {
  } else if (OB_FAIL(serialization::decode_vi32(buf, buf_len, pos, &row_idx_))) {
  } else if (pos < buf_len) { // decode the version field
    if (OB_FAIL(serialization::decode_vi32(buf, buf_len, pos, &version_))) {
    }
  } else {
    version_ = VERSION_0; // VERSION_0 without version field serialized
  }
  return ret;
}

int64_t ObTxCtxTableMeta::get_serialize_size() const
{
  int64_t data_len = get_serialize_size_();
  ObTxCtxTableCommonHeader header(MAGIC_VERSION, data_len);

  int64_t len = 0;
  len += header.get_serialize_size();
  len += data_len;

  return len;
}

int64_t ObTxCtxTableMeta::get_serialize_size_() const
{
  int64_t len = 0;
  len += tx_id_.get_serialize_size();
  len += ls_id_.get_serialize_size();
  len += serialization::encoded_length_vi64(tx_ctx_serialize_size_);
  len += serialization::encoded_length_vi32(row_num_);
  len += serialization::encoded_length_vi32(row_idx_);
  len += serialization::encoded_length_vi32(version_);
  return len;
}

DEF_TO_STRING(ObCommitVersionsArray::Node)
{
  int64_t pos = 0;
  J_KV(K_(start_scn),
       K_(commit_version));
  return pos;
}

DEF_TO_STRING(ObCommitVersionsArray)
{
  int64_t pos = 0;
  J_KV(KP(this), K(array_.count()));
  int64_t cnt = array_.count();
  for (int64_t i = 0; i < cnt && i < 5; i++) {
    J_KV("idx", i, "node", array_.at(i));
  }
  for (int64_t i = cnt - 5; i >= 5 && i < cnt; i++) {
    J_KV("idx", i, "node", array_.at(i));
  }
  return pos;
}

int ObCommitVersionsArray::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;

  const int64_t len = get_serialize_size_();

  if (OB_UNLIKELY(OB_ISNULL(buf) || buf_len <= 0 || pos > buf_len)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "serialize ObCommitVersionsArray failed.", KR(ret), KP(buf), K(buf_len),
                K(pos));
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, UNIS_VERSION))) {
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, len))) {
  } else if (OB_FAIL(serialize_(buf, buf_len, pos))) {
  }

  return ret;
}

int ObCommitVersionsArray::deserialize(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t version = 0;
  int64_t len = 0;
  array_.reuse();

  if (OB_UNLIKELY(nullptr == buf || data_len <= 0 || pos > data_len)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid arguments.", KP(buf), K(data_len), K(ret));
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &version))) {
  } else if (version != UNIS_VERSION) {
    ret = OB_VERSION_NOT_MATCH;
    STORAGE_LOG(WARN, "object version mismatch", K(ret), K(version));
  }  else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &len))) {
  } else if (OB_UNLIKELY(len < 0)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "can't decode object with negative length", KR(ret), K(len));
  } else if (OB_UNLIKELY(data_len < len + pos)) {
    ret = OB_DESERIALIZE_ERROR;
    STORAGE_LOG(WARN, "buf length not correct", KR(ret), K(len), K(pos), K(data_len));
  } else {
    int64_t original_pos = pos;
    pos = 0;
    array_.reuse();
    if (OB_FAIL(deserialize_(buf + original_pos, len, pos))) {
    }
    pos += original_pos;
  }

  return ret;
}

int64_t ObCommitVersionsArray::get_serialize_size() const
{
  int64_t data_len = get_serialize_size_();
  int64_t len = 0;
  len += serialization::encoded_length_vi64(UNIS_VERSION);
  len += serialization::encoded_length_vi64(data_len);
  len += data_len;
  return len;
}

int ObCommitVersionsArray::serialize_(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  for (int i = 0; OB_SUCC(ret) && i < array_.count(); i++) {
    LST_DO_CODE(OB_UNIS_ENCODE, array_.at(i).start_scn_, array_.at(i).commit_version_);
  }
  return ret;
}

int ObCommitVersionsArray::deserialize_(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;

  ObCommitVersionsArray::Node node;
  while (OB_SUCC(ret) && pos < data_len) {
    LST_DO_CODE(OB_UNIS_DECODE, node.start_scn_, node.commit_version_);
    array_.push_back(node);
  }

  return ret;
}

int64_t ObCommitVersionsArray::get_serialize_size_() const
{
  int64_t len = 0;
  for (int i = 0; i < array_.count(); i++) {
    LST_DO_CODE(OB_UNIS_ADD_LEN, array_.at(i).start_scn_, array_.at(i).commit_version_);
  }
  return len;
}

bool ObCommitVersionsArray::is_valid()
{
  bool bool_ret = true;
  for (int i = 0; i < array_.count() - 1; i++) {
    if (!array_.at(i).start_scn_.is_valid() || 
        !array_.at(i).commit_version_.is_valid() ||
        array_.at(i).commit_version_.is_max() ||
        array_.at(i).start_scn_ > array_.at(i + 1).start_scn_ || 
        array_.at(i).start_scn_ > array_.at(i).commit_version_) {
      bool_ret = false;
      STORAGE_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "this commit version array is invalid", K(array_.at(i)),
                  K(array_.at(i + 1)));
    }
  }

  int64_t last_node_idx = array_.count() - 1;
  if (!array_.at(last_node_idx).start_scn_.is_max() && array_.at(last_node_idx).commit_version_.is_max()) {
    bool_ret = false;
    STORAGE_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "this commit version array is invalid", K(array_.at(last_node_idx)));
  }

  return bool_ret;
}

bool ObITxDataCheckFunctor::is_decided() const
{
  return tx_data_check_data_.is_rollback_ ||
    ObTxData::ABORT == tx_data_check_data_.state_;
}

void ObITxDataCheckFunctor::resolve_tx_data_check_data_(const int32_t state,
                                                        const share::SCN commit_version,
                                                        const share::SCN end_scn,
                                                        const bool is_rollback)
{
  tx_data_check_data_.state_ = state;
  tx_data_check_data_.commit_version_ = commit_version;
  tx_data_check_data_.end_scn_ = end_scn;
  tx_data_check_data_.is_rollback_ = is_rollback;
}

bool ObITxDataCheckFunctor::may_exist_undecided_state_in_tx_data_table() const
{
  return may_exist_undecided_state_in_tx_data_table_;
}

void ObITxDataCheckFunctor::set_may_exist_undecided_state_in_tx_data_table()
{
  may_exist_undecided_state_in_tx_data_table_ = true;
}

} // end namespace transaction
} // end namespace oceanbase

