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

#define USING_LOG_PREFIX SHARE
#include "ob_transfer_info.h"
#include "observer/ob_inner_sql_connection.h" // ObInnerSQLConnection
#include "storage/tablelock/ob_lock_inner_connection_util.h" // ObInnerConnectionLockUtil

using namespace oceanbase;
using namespace share;
using namespace common;
using namespace palf;
using namespace share::schema;
using namespace share;
using namespace transaction::tablelock;
using namespace observer;

ObTransferTabletInfo::ObTransferTabletInfo()
  : tablet_id_(),
    transfer_seq_(OB_INVALID_TRANSFER_SEQ)
{
}

void ObTransferTabletInfo::reset()
{
  tablet_id_.reset();
  transfer_seq_ = OB_INVALID_TRANSFER_SEQ;
}

int ObTransferTabletInfo::init(
    const ObTabletID &tablet_id,
    const int64_t transfer_seq)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid() || transfer_seq <= OB_INVALID_TRANSFER_SEQ)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(tablet_id), K(transfer_seq));
  } else {
    tablet_id_ = tablet_id;
    transfer_seq_ = transfer_seq;
  }
  return ret;
}

int ObTransferTabletInfo::parse_from_display_str(const common::ObString &str)
{
  int ret = OB_SUCCESS;
  uint64_t tablet_id = 0;
  errno = 0;
  if (OB_UNLIKELY(2 != sscanf(str.ptr(), "%lu:%ld", &tablet_id, &transfer_seq_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ObTransferTabletInfo str", KR(ret), K(str), K(errno), KERRMSG);
  } else {
    tablet_id_ = tablet_id; // ObTabletID <- uint64_t
  }
  return ret;
}

int ObTransferTabletInfo::to_display_str(char *buf, const int64_t len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(len <= 0 || pos < 0 || pos >= len || !is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(buf), K(len), K(pos), KPC(this));
  } else if (OB_FAIL(databuff_printf(buf, len, pos, "%lu:%ld", tablet_id_.id(), transfer_seq_))) {
    LOG_WARN("databuff_printf failed", KR(ret), K(len), K(pos), K(buf), KPC(this));
  }
  return ret;
}

bool ObTransferTabletInfo::operator==(const ObTransferTabletInfo &other) const
{
  return other.tablet_id_ == tablet_id_
      && other.transfer_seq_ == transfer_seq_;
}

OB_SERIALIZE_MEMBER(ObTransferTabletInfo, tablet_id_, transfer_seq_);

void ObTransferPartInfo::reset()
{
  table_id_ = OB_INVALID_ID;
  part_object_id_ = OB_INVALID_ID;
}

int ObTransferPartInfo::init(const ObObjectID &table_id, const ObObjectID &part_object_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_ID == table_id || OB_INVALID_ID == part_object_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid id", KR(ret), K(table_id), K(part_object_id));
  } else {
    table_id_ = table_id;
    part_object_id_ = part_object_id;
  }
  return ret;
}


int ObTransferPartInfo::parse_from_display_str(const common::ObString &str)
{
  int ret = OB_SUCCESS;
  errno = 0;
  if (OB_UNLIKELY(2 != sscanf(str.ptr(), "%lu:%lu", &table_id_, &part_object_id_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ObTransferTabletInfo str", KR(ret), K(str), K(errno), KERRMSG);
  }
  return ret;
}

int ObTransferPartInfo::to_display_str(char *buf, const int64_t len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(len <= 0 || pos < 0 || pos >= len || !is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(buf), K(len), K(pos), KPC(this));
  } else if (OB_FAIL(databuff_printf(buf, len, pos, "%lu:%lu", table_id_, part_object_id_))) {
    LOG_WARN("databuff_printf failed", KR(ret), K(len), K(pos), K(buf), KPC(this));
  }
  return ret;
}

bool ObTransferPartInfo::operator==(const ObTransferPartInfo &other) const
{
  return other.table_id_ == table_id_
      && other.part_object_id_ == part_object_id_;
}

int ObDisplayTabletID::parse_from_display_str(const common::ObString &str)
{
  int ret = OB_SUCCESS;
  uint64_t tablet_id = 0;
  errno = 0;
  if (OB_UNLIKELY(1 != sscanf(str.ptr(), "%lu", &tablet_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ObDisplayTabletID str", KR(ret), K(str), K(errno), KERRMSG);
  } else {
    tablet_id_ = tablet_id; // ObTabletID <- uint64_t
  }
  return ret;
}

int ObDisplayTabletID::to_display_str(char *buf, const int64_t len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(len <= 0 || pos < 0 || pos >= len || !is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(buf), K(len), K(pos), KPC(this));
  } else if (OB_FAIL(databuff_printf(buf, len, pos, "%lu", tablet_id_.id()))) {
    LOG_WARN("databuff_printf failed", KR(ret), K(len), K(pos), K(buf), KPC(this));
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObDisplayTabletID, tablet_id_);

