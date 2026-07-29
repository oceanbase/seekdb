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

#include "ob_storage_log.h"
#include "storage/meta_mem/ob_storage_meta_mem_mgr.h"

namespace oceanbase
{
using namespace share;
namespace storage
{
ObCreateRuntimePrepareLog::ObCreateRuntimePrepareLog(omt::ObServerRuntimeMeta &meta)
  : meta_(meta)
{
}

bool ObCreateRuntimePrepareLog::is_valid() const
{
  return meta_.is_valid();
}

OB_SERIALIZE_MEMBER(ObCreateRuntimePrepareLog, meta_);

ObCreateRuntimeCommitLog::ObCreateRuntimeCommitLog()
{
}

bool ObCreateRuntimeCommitLog::is_valid() const
{
  return true;
}

OB_SERIALIZE_MEMBER(ObCreateRuntimeCommitLog);

ObCreateRuntimeAbortLog::ObCreateRuntimeAbortLog()
{
}

bool ObCreateRuntimeAbortLog::is_valid() const
{
  return true;
}

OB_SERIALIZE_MEMBER(ObCreateRuntimeAbortLog);

ObUpdateServerResourcesLog::ObUpdateServerResourcesLog(share::ObServerRuntimeConfig &runtime_config)
  : runtime_config_(runtime_config)
{
}

bool ObUpdateServerResourcesLog::is_valid() const
{
  return runtime_config_.is_valid();
}

OB_SERIALIZE_MEMBER(ObUpdateServerResourcesLog, runtime_config_);

ObUpdateRuntimeSuperBlockLog::ObUpdateRuntimeSuperBlockLog(ObServerRuntimeSuperBlock &super_block)
  : super_block_(super_block)
{
}

bool ObUpdateRuntimeSuperBlockLog::is_valid() const
{
  return super_block_.is_valid();
}

OB_SERIALIZE_MEMBER(ObUpdateRuntimeSuperBlockLog, super_block_);

ObLSMetaLog::ObLSMetaLog(const ObLSMeta &ls_meta)
  : ls_meta_(ls_meta)
{
}

bool ObLSMetaLog::is_valid() const
{
  return ls_meta_.is_valid();
}


DEF_TO_STRING(ObLSMetaLog)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(ls_meta));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER(ObLSMetaLog, ls_meta_);

DEF_TO_STRING(ObLSMarkerLog)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER(ObLSMarkerLog);

ObDeleteTabletLog::ObDeleteTabletLog()
  : tablet_id_()
{
}

ObDeleteTabletLog::ObDeleteTabletLog(const ObTabletID &tablet_id)
  : tablet_id_(tablet_id)
{
}

bool ObDeleteTabletLog::is_valid() const
{
  return tablet_id_.is_valid();
}

OB_SERIALIZE_MEMBER(ObDeleteTabletLog, tablet_id_);

DEF_TO_STRING(ObDeleteTabletLog)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(tablet_id));
  J_OBJ_END();
  return pos;
}

ObUpdateTabletLog::ObUpdateTabletLog(
    const ObTabletID &tablet_id,
    const ObMetaDiskAddr &disk_addr)
  : tablet_id_(tablet_id),
    disk_addr_(disk_addr)
{
}

bool ObUpdateTabletLog::is_valid() const
{
  return tablet_id_.is_valid() && disk_addr_.is_valid();
}

OB_SERIALIZE_MEMBER(ObUpdateTabletLog, tablet_id_, disk_addr_);

DEF_TO_STRING(ObUpdateTabletLog)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(tablet_id), K_(disk_addr));
  J_OBJ_END();
  return pos;
}

ObEmptyShellTabletLog::ObEmptyShellTabletLog(
    const ObTabletID &tablet_id,
    ObTablet *tablet)
  : version_(EMPTY_SHELL_SLOG_VERSION),
    tablet_id_(tablet_id),
    tablet_(tablet)
{
}

int ObEmptyShellTabletLog::serialize(
    char *buf,
    const int64_t data_len,
    int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(serialization::encode(buf, data_len, pos, version_))) {
    STORAGE_LOG(WARN, "deserialize version_ failed", K(ret), KP(data_len), K(pos));
  } else if (OB_FAIL(tablet_id_.serialize(buf, data_len, pos))) {
    STORAGE_LOG(WARN, "deserialize tablet_id_ failed", K(ret), KP(data_len), K(pos));
  } else if (OB_FAIL(tablet_->serialize(buf, data_len, pos))) {
    STORAGE_LOG(WARN, "deserialize tablet failed", K(ret), KP(data_len), K(pos));
  }

  return ret;
}

int ObEmptyShellTabletLog::deserialize_id(
    const char *buf,
    const int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(serialization::decode(buf, data_len, pos, version_))) {
    STORAGE_LOG(WARN, "deserialize version_ failed", K(ret), KP(data_len), K(pos));
  } else if (OB_FAIL(tablet_id_.deserialize(buf, data_len, pos))) {
    STORAGE_LOG(WARN, "deserialize tablet_id_ failed", K(ret), KP(data_len), K(pos));
  }
  return ret;
}

// shouldn't be called, since we can't set tablet addr here, but tablet addr should be set before deserialization
int ObEmptyShellTabletLog::deserialize(
    const char *buf,
    const int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(serialization::decode(buf, data_len, pos, version_))) {
    STORAGE_LOG(WARN, "deserialize version_ failed", K(ret), KP(data_len), K(pos));
  } else if (OB_FAIL(tablet_id_.deserialize(buf, data_len, pos))) {
    STORAGE_LOG(WARN, "deserialize tablet_id_ failed", K(ret), KP(data_len), K(pos));
  } else if (OB_FAIL(tablet_->deserialize(buf, data_len, pos))) {
    STORAGE_LOG(WARN, "deserialize tablet failed", K(ret), KP(data_len), K(pos));
  }

  return ret;
}

int64_t ObEmptyShellTabletLog::get_serialize_size() const
{
  int64_t size = 0;
  size += serialization::encoded_length(version_);
  size += tablet_id_.get_serialize_size();
  size += tablet_->get_serialize_size();
  return size;
}

bool ObEmptyShellTabletLog::is_valid() const
{
  return tablet_id_.is_valid();
}

DEF_TO_STRING(ObEmptyShellTabletLog)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(tablet_id));
  J_OBJ_END();
  return pos;
}

}
}
