/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_XA_ID_H_
#define OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_XA_ID_H_

#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace transaction
{

enum class ObGlobalTxType : uint8_t
{
  PLAIN = 0,
  XA_TRANS = 1,
};

class ObXADefault
{
public:
  static constexpr int64_t OB_XA_TIMEOUT_SECONDS = 60;
  static constexpr const char *OB_XA_TIMEOUT_NAME = "ob_xa_timeout";
};

// XA protocol flags cross the query/data-plane seam together with the XID.
// Keep the wire-level vocabulary here; validation remains implemented by the
// transaction module.
class ObXAFlag
{
public:
  enum
  {
    OBTMNOFLAGS = 0,
    OBTMREADONLY = 0x100,
    OBTMSERIALIZABLE = 0x400,
    OBLOOSELY = 0x10000,
    OBTMJOIN = 0x200000,
    OBTMSUSPEND = 0x2000000,
    OBTMSUCCESS = 0x4000000,
    OBTMRESUME = 0x8000000,
    OBTMFAIL = 0x20000000,
    OBTMONEPHASE = 0x40000000,
    OBTEMPTABLE = 0x100000000,
  };

  static bool is_valid(const int64_t flag, const int64_t xa_req_type);
  static bool contain_tmreadonly(const int64_t flag) { return flag & OBTMREADONLY; }
  static bool contain_tmserializable(const int64_t flag) { return flag & OBTMSERIALIZABLE; }
  static bool is_tmnoflags_for_mysql(const int64_t flag) { return OBTMNOFLAGS == flag; }
  static bool contain_loosely(const int64_t flag) { return flag & OBLOOSELY; }
  static bool contain_tmjoin(const int64_t flag) { return flag & OBTMJOIN; }
  static bool is_tmjoin(const int64_t flag) { return flag == OBTMJOIN; }
  static bool contain_tmresume(const int64_t flag) { return flag & OBTMRESUME; }
  static bool is_tmresume(const int64_t flag) { return flag == OBTMRESUME; }
  static bool contain_tmsuccess(const int64_t flag) { return flag & OBTMSUCCESS; }
  static bool contain_tmsuspend(const int64_t flag) { return flag & OBTMSUSPEND; }
  static bool contain_tmonephase(const int64_t flag) { return flag & OBTMONEPHASE; }
  static bool is_tmonephase(const int64_t flag) { return flag == OBTMONEPHASE; }
  static bool contain_tmfail(const int64_t flag) { return flag & OBTMFAIL; }
  static int64_t add_end_flag(const int64_t flag, const int64_t end_flag)
  {
    int64_t ret = end_flag;
    if (contain_loosely(flag)) {
      ret |= OBLOOSELY;
    }
    return ret;
  }
  static bool contain_temp_table(const int64_t flag)
  {
    return flag & OBTEMPTABLE;
  }
};

// XA transaction identifier exchanged between SQL/session and the data plane.
class ObXATransID
{
  OB_UNIS_VERSION(1);
public:
  ObXATransID() { reset(); }
  ObXATransID(const ObXATransID &xid);
  ~ObXATransID() { destroy(); }
  void reset();
  void destroy() { reset(); }
  int set(const common::ObString &gtrid,
          const common::ObString &bqual,
          int64_t format_id);
  int set(const ObXATransID &xid);
  const common::ObString &get_gtrid_str() const { return gtrid_str_; }
  const common::ObString &get_bqual_str() const { return bqual_str_; }
  int64_t get_format_id() const { return format_id_; }
  uint64_t get_gtrid_hash() const { return g_hv_; }
  uint64_t get_bqual_hash() const { return b_hv_; }
  uint64_t get_hash() const
  {
    if (0 == g_hv_ || 0 == b_hv_) {
      g_hv_ = murmurhash(gtrid_str_.ptr(), gtrid_str_.length(), 0) % HASH_SIZE;
      b_hv_ = murmurhash(bqual_str_.ptr(), bqual_str_.length(), 0) % HASH_SIZE;
    }
    return (g_hv_ + b_hv_) / 11;
  }
  bool empty() const;
  bool is_valid() const;
  ObXATransID &operator=(const ObXATransID &xid);
  bool operator==(const ObXATransID &xid) const;
  bool operator!=(const ObXATransID &xid) const;
  TO_STRING_KV(K_(gtrid_str), K_(bqual_str), K_(format_id),
      KPHEX(gtrid_str_.ptr(), gtrid_str_.length()),
      KPHEX(bqual_str_.ptr(), bqual_str_.length()),
      K_(g_hv), K_(b_hv));

  static const int32_t HASH_SIZE = 1000000000;
  static const int32_t MAX_GTRID_LENGTH = 64;
  static const int32_t MAX_BQUAL_LENGTH = 64;
  static const int32_t MAX_XID_LENGTH = MAX_GTRID_LENGTH + MAX_BQUAL_LENGTH;
private:
  char gtrid_buf_[MAX_GTRID_LENGTH];
  common::ObString gtrid_str_;
  char bqual_buf_[MAX_BQUAL_LENGTH];
  common::ObString bqual_str_;
  int64_t format_id_;
  mutable uint64_t g_hv_;
  mutable uint64_t b_hv_;
};

} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_XA_ID_H_
