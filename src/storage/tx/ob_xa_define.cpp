/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include "ob_xa_define.h"

namespace oceanbase
{

using namespace common;

namespace transaction
{

const int64_t LOCK_FORMAT_ID = -2;
const int64_t XA_INNER_TABLE_TIMEOUT = 10 * 1000 * 1000;

const bool ENABLE_NEW_XA = true;

OB_SERIALIZE_MEMBER(ObXATransID, gtrid_str_, bqual_str_, format_id_);

ObXATransID::ObXATransID(const ObXATransID &xid)
{
  format_id_ = xid.format_id_;
  gtrid_str_.reset();
  bqual_str_.reset();
  gtrid_str_.assign_buffer(gtrid_buf_, sizeof(gtrid_buf_));
  bqual_str_.assign_buffer(bqual_buf_, sizeof(bqual_buf_));
  gtrid_str_.write(xid.gtrid_str_.ptr(), xid.gtrid_str_.length());
  bqual_str_.write(xid.bqual_str_.ptr(), xid.bqual_str_.length());
  g_hv_ = xid.g_hv_;
  b_hv_ = xid.b_hv_;
}

void ObXATransID::reset()
{
  gtrid_str_.reset();
  (void)gtrid_str_.assign_buffer(gtrid_buf_, sizeof(gtrid_buf_));
  bqual_str_.reset();
  (void)bqual_str_.assign_buffer(bqual_buf_, sizeof(bqual_buf_));
  format_id_ = 1;
  g_hv_ = 0;
  b_hv_ = 0;
}

int ObXATransID::set(const ObString &gtrid_str,
                       const ObString &bqual_str,
                       const int64_t format_id)
{
  int ret = OB_SUCCESS;
  if (0 > gtrid_str.length() || 0 > bqual_str.length()
      || MAX_GTRID_LENGTH < gtrid_str.length()
      || MAX_BQUAL_LENGTH < bqual_str.length()) {
    TRANS_LOG(WARN, "invalid arguments", K(gtrid_str), K(bqual_str), K(format_id));
    ret = OB_INVALID_ARGUMENT;
  } else {
    format_id_ = format_id;
    gtrid_str_.reset();
    bqual_str_.reset();
    gtrid_str_.assign_buffer(gtrid_buf_, sizeof(gtrid_buf_));
    bqual_str_.assign_buffer(bqual_buf_, sizeof(bqual_buf_));
    gtrid_str_.write(gtrid_str.ptr(), gtrid_str.length());
    bqual_str_.write(bqual_str.ptr(), bqual_str.length());
    g_hv_ = murmurhash(gtrid_str.ptr(), gtrid_str.length(), 0) % HASH_SIZE;
    b_hv_ = murmurhash(bqual_str.ptr(), bqual_str.length(), 0) % HASH_SIZE;
  }
  return ret;
}

int ObXATransID::set(const ObXATransID &xid)
{
  int ret = OB_SUCCESS;
  if (!xid.is_valid()) {
    TRANS_LOG(WARN, "invalid xid", K(xid));
    ret = OB_INVALID_ARGUMENT;
  } else {
    format_id_ = xid.format_id_;
    gtrid_str_.reset();
    bqual_str_.reset();
    gtrid_str_.assign_buffer(gtrid_buf_, sizeof(gtrid_buf_));
    bqual_str_.assign_buffer(bqual_buf_, sizeof(bqual_buf_));
    gtrid_str_.write(xid.gtrid_str_.ptr(), xid.gtrid_str_.length());
    bqual_str_.write(xid.bqual_str_.ptr(), xid.bqual_str_.length());
    g_hv_ = xid.g_hv_;
    b_hv_ = xid.b_hv_;
  }
  return ret;
}

bool ObXATransID::empty() const
{
  return gtrid_str_.empty();
}

bool ObXATransID::is_valid() const
{
  return 0 <= gtrid_str_.length() && 0 <= bqual_str_.length()
         && MAX_GTRID_LENGTH >= gtrid_str_.length()
         && MAX_BQUAL_LENGTH >= bqual_str_.length();
}

ObXATransID &ObXATransID::operator=(const ObXATransID &xid)
{
  if (this != &xid) {
    format_id_ = xid.format_id_;
    gtrid_str_.reset();
    bqual_str_.reset();
    gtrid_str_.assign_buffer(gtrid_buf_, sizeof(gtrid_buf_));
    bqual_str_.assign_buffer(bqual_buf_, sizeof(bqual_buf_));
    gtrid_str_.write(xid.gtrid_str_.ptr(), xid.gtrid_str_.length());
    bqual_str_.write(xid.bqual_str_.ptr(), xid.bqual_str_.length());
    g_hv_ = xid.g_hv_;
    b_hv_ = xid.b_hv_;
  }
  return *this;
}

bool ObXATransID::operator==(const ObXATransID &xid) const
{
  return gtrid_str_ == xid.gtrid_str_
         && bqual_str_ == xid.bqual_str_
         && format_id_ == xid.format_id_;
}

bool ObXATransID::operator!=(const ObXATransID &xid) const
{
  return gtrid_str_ != xid.gtrid_str_;
         //|| bqual_str_ != xid.bqual_str_
         //|| format_id_ != xid.format_id_;
}






bool ObXAFlag::is_valid(const int64_t flag, const int64_t xa_req_type)
{
  bool ret_bool = true;

  switch (xa_req_type) {
    case ObXAReqType::XA_START: {
      if (((flag & OBTMRESUME) && ((flag & OBTMJOIN) || (flag & OBLOOSELY)))
         || ((flag & OBLOOSELY) && (flag & OBTMJOIN))) {
        ret_bool = false;
      } else {
        const bool is_resumejoin = flag & (OBTMRESUME | OBTMJOIN);
        if (!is_resumejoin) {
          const int64_t mask = OBLOOSELY | OBTMREADONLY | OBTMSERIALIZABLE;
          if (mask != (flag | mask)) {
            ret_bool = false;
          } else {
            ret_bool = true;
          }
        } else {
          if ((flag & OBTMJOIN) && (flag & OBTMRESUME)) {
            ret_bool = false;
          } else {
            ret_bool = true;
          }
        }
      }
      break;
    }
    case ObXAReqType::XA_END: {
      const int64_t mask = 0x00000000FFFFFFFF;
      if ((flag & mask) != OBTMSUSPEND && (flag & mask) != OBTMSUCCESS && (flag & mask) != OBTMFAIL) {
        ret_bool = false;
      } else {
        ret_bool = true;
      }
      break;
    }
    case ObXAReqType::XA_PREPARE: {
      // oracle would not carry flag in a xa prepare req
      ret_bool = true;
      TRANS_LOG(INFO, "no need to check flag for xa prepare", K(xa_req_type), K(flag));
      break;
    }
    case ObXAReqType::XA_COMMIT: {
      // noflags or onephase
      if (flag != OBTMNOFLAGS && flag != OBTMONEPHASE) {
        ret_bool = false;
      } else {
        ret_bool = true;
      }
      break;
    }
    case ObXAReqType::XA_ROLLBACK: {
      // oracle would not carry flag in a xa rollback req
      ret_bool = true;
      TRANS_LOG(INFO, "no need to check flag for xa rollback", K(xa_req_type), K(flag));
      break;
    }
    default: {
      ret_bool = false;
      TRANS_LOG_RET(WARN, OB_INVALID_ARGUMENT, "invalid xa request type", K(xa_req_type), K(flag));
    }
  }
  TRANS_LOG(INFO, "check xa flag", K(ret_bool), K(xa_req_type), KPHEX(&flag, sizeof(int64_t)));

  return ret_bool;
}




void ObXAStatistics::reset()
{
  ATOMIC_STORE(&total_standby_clearup_count_, 0);
  ATOMIC_STORE(&total_success_xa_start_, 0);
  ATOMIC_STORE(&total_failure_xa_start_, 0);
  ATOMIC_STORE(&total_success_xa_prepare_, 0);
  ATOMIC_STORE(&total_failure_xa_prepare_, 0);
  ATOMIC_STORE(&total_success_xa_1pc_commit_, 0);
  ATOMIC_STORE(&total_failure_xa_1pc_commit_, 0);
  ATOMIC_STORE(&total_success_xa_2pc_commit_, 0);
  ATOMIC_STORE(&total_failure_xa_2pc_commit_, 0);
  ATOMIC_STORE(&total_xa_rollback_, 0);
  ATOMIC_STORE(&total_success_dblink_promotion_, 0);
  ATOMIC_STORE(&total_failure_dblink_promotion_, 0);
  ATOMIC_STORE(&total_success_dblink_, 0);
  ATOMIC_STORE(&total_failure_dblink_, 0);
}


}//transaction

}//oceanbase
