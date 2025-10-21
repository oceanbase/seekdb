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

#ifndef OCEANBASE_SHARE_OB_CASCAD_MEMBER_H_
#define OCEANBASE_SHARE_OB_CASCAD_MEMBER_H_

#include "lib/container/ob_se_array.h"
#include "lib/net/ob_addr.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/ob_define.h"
#include "lib/json/ob_yson.h"
#include "common/ob_region.h"

namespace oceanbase
{
namespace share
{
class ObCascadMember
{
  OB_UNIS_VERSION(1);
public:
  ObCascadMember();
  virtual ~ObCascadMember() = default;

  explicit ObCascadMember(const common::ObAddr &server,
                          const int64_t cluster_id);
public:
  const common::ObAddr get_server() const { return server_; }
  int64_t get_cluster_id() const { return ATOMIC_LOAD(&cluster_id_); }
  void set_cluster_id(const int64_t cluster_id) { ATOMIC_STORE(&cluster_id_, cluster_id); }
  virtual void reset();
  virtual bool is_valid() const;
  virtual int64_t hash() const;
  virtual int hash(uint64_t &hash_val) const { hash_val = hash(); return OB_SUCCESS; }
  friend bool operator==(const ObCascadMember &lhs, const ObCascadMember &rhs);
  friend bool operator<(const ObCascadMember &lhs, const ObCascadMember &rhs);
  ObCascadMember &operator=(const ObCascadMember &rhs);

  TO_STRING_KV(K_(server), K_(cluster_id));
  TO_YSON_KV( OB_Y_(server), OB_ID(cluster_id), cluster_id_);
protected:
  common::ObAddr server_;
  int64_t cluster_id_;
};

inline bool operator==(const ObCascadMember &lhs, const ObCascadMember &rhs)
{
  return (lhs.server_ == rhs.server_) && (lhs.cluster_id_ == rhs.cluster_id_);
}

inline bool operator<(const ObCascadMember &lhs, const ObCascadMember &rhs)
{
  return lhs.server_ < rhs.server_;
}

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_CASCAD_MEMBER_H_
