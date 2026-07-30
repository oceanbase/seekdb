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
#ifndef OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_OWNER_ID_
#define OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_OWNER_ID_

#include <stdint.h>
#include "lib/ob_errno.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace transaction
{
namespace tablelock
{

enum class ObLockOwnerType : unsigned char {
#define DEF_LOCK_OWNER_TYPE(n, type)                    \
  type##_OWNER_TYPE = n,
#include "ob_table_lock_def.h"
#undef DEF_LOCK_OWNER_TYPE
  // make sure this is smaller than INVALID_OWNER_TYPE
  MAX_OWNER_TYPE,

  INVALID_OWNER_TYPE    = 255,
};

static constexpr int64_t FORK_TABLE_LOCK_OWNER_ID = 1;

const char *get_name(const ObLockOwnerType intype);
static inline
bool is_lock_owner_type_valid(const ObLockOwnerType &type)
{
  return (type < ObLockOwnerType::MAX_OWNER_TYPE);
}

class ObTableLockOwnerID
{
public:
  static constexpr int64_t MAGIC_NUM = -0xABC;
  static const int64_t INVALID_ID = -1;
  static const int64_t SESS_CREATE_TS_BIT = 22;
  static const int64_t SESS_ID_BIT = 32;
#ifndef _WIN32
  static const int64_t INVALID_RAW_OWNER_ID = ((1ULL << 54) - 1);
  static const int64_t SESS_CREATE_TS_MASK = (1L << SESS_CREATE_TS_BIT) - 1;
  static const int64_t SESS_ID_MASK = (1L << SESS_ID_BIT) - 1;
#else
  static const int64_t INVALID_RAW_OWNER_ID = ((UINT64_C(1) << 54) - 1);
  static const int64_t SESS_CREATE_TS_MASK = (INT64_C(1) << SESS_CREATE_TS_BIT) - 1;
  static const int64_t SESS_ID_MASK = (INT64_C(1) << SESS_ID_BIT) - 1;
#endif
  ObTableLockOwnerID() :
    type_(static_cast<unsigned char>(ObLockOwnerType::INVALID_OWNER_TYPE)),
    id_(INVALID_ID) {}
  ObTableLockOwnerID(const ObTableLockOwnerID &other) :
    type_(other.type_), id_(other.id_)
  { hash_value_ = inner_hash(); }
  ObTableLockOwnerID(unsigned char type, int64_t id) :
    type_(type), id_(id)
  { hash_value_ = inner_hash(); }
  ~ObTableLockOwnerID() { reset(); }
public:
  int get_ddl_owner_id(int64_t &id) const;
  int64_t id() const { return id_; }
  unsigned char type() const { return type_; }
  bool is_session_id_owner() const
  { return type_ == static_cast<unsigned char>(ObLockOwnerType::SESS_ID_OWNER_TYPE); }
  bool is_default() const
  { return 0 == type_ && 0 == id_; }
  void reset()
  {
    type_ = static_cast<unsigned char>(ObLockOwnerType::INVALID_OWNER_TYPE);
    id_ = INVALID_ID;
  }
  bool is_valid() const
  {
    return (INVALID_ID != id_ &&
            is_lock_owner_type_valid(static_cast<ObLockOwnerType>(type_)));
  }
  static ObTableLockOwnerID default_owner();
  static ObTableLockOwnerID get_owner(const unsigned char type,
                                      const int64_t id);
  void set_default()
  { type_ = 0; id_ = 0; hash_value_ = inner_hash(); }
  // check valid.
  void convert_from_value_ignore_ret(const unsigned char owner_type,
                                     const int64_t id);
  int convert_from_value(const ObLockOwnerType owner_type,
                         const int64_t id);
  int convert_from_session_id(const uint32_t sessid,
                              const uint64_t sess_create_ts);
  int convert_to_sessid(uint32_t &sessid) const;
  // assignment
  ObTableLockOwnerID &operator=(const ObTableLockOwnerID &other)
  {
    type_ = other.type_; id_ = other.id_;
    hash_value_ = inner_hash();
    return *this;
  }

  // compare operator
  bool operator == (const ObTableLockOwnerID &other) const
  { return type_ == other.type_ && id_ == other.id_; }
  bool operator >  (const ObTableLockOwnerID &other) const
  {
    return (type_ > other.type_
            || (type_ == other.type_ && id_ > other.id_));
  }
  bool operator != (const ObTableLockOwnerID &other) const
  { return type_ != other.type_ || id_ != other.id_; }
  bool operator <  (const ObTableLockOwnerID &other) const
  {
    return (type_ < other.type_
            || (type_ == other.type_ && id_ < other.id_));
  }
  bool operator <= (const ObTableLockOwnerID &other) const
  {
    return (type_ <= other.type_
            || (type_ == other.type_ && id_ <= other.id_));
  }
  bool operator >= (const ObTableLockOwnerID &other) const
  {
    return (type_ >= other.type_
            || (type_ == other.type_ && id_ >= other.id_));
  }
  int compare(const ObTableLockOwnerID &other) const
  {
    if (type_ == other.type_ && id_ == other.id_) {
      return 0;
    } else if (type_ < other.type_
               || (type_ == other.type_ && id_ < other.id_)) {
      return -1;
    } else {
      return 1;
    }
  }

  uint64_t hash() const
  { return hash_value_; }
  int hash(uint64_t &hash_val) const { hash_val = hash(); return OB_SUCCESS; }
  uint64_t inner_hash() const
  {
    uint64_t hash_val = 0;
    hash_val = murmurhash(&type_, sizeof(type_), hash_val);
    hash_val = murmurhash(&id_, sizeof(id_), hash_val);
    return hash_val;
  }
  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_KV("type_name", get_name(static_cast<ObLockOwnerType>(type_)), K_(id), K_(hash_value));

private:
  unsigned char type_;
  int64_t id_;
  uint64_t hash_value_;
};

}  // namespace tablelock
}  // namespace transaction
}  // namespace oceanbase

#endif /* OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_OWNER_ID_ */
