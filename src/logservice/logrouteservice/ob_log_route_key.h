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
#ifndef OCEANBASE_LOG_ROUTE_KEY_H_
#define OCEANBASE_LOG_ROUTE_KEY_H_

#include "share/ob_ls_id.h"       // ObLSID

namespace oceanbase
{
namespace logservice
{
class ObLSRouterKey
{
public:
  ObLSRouterKey();
  ObLSRouterKey(
      const int64_t cluster_id,
      const uint64_t tenant_id,
      const share::ObLSID &ls_id);
  virtual ~ObLSRouterKey() {}
  void reset();
  virtual bool operator ==(const ObLSRouterKey &other) const;
  virtual bool operator !=(const ObLSRouterKey &other) const;
  virtual bool is_valid() const;
  virtual uint64_t hash() const;
  virtual int hash(uint64_t &hash_val) const;
  virtual int64_t size() const { return sizeof(*this); }
  virtual int deep_copy(char *buf, const int64_t buf_len, ObLSRouterKey *&key) const;
  inline int64_t get_cluster_id() const { return cluster_id_; }
  inline uint64_t get_tenant_id() const { return tenant_id_; }
  inline share::ObLSID get_ls_id() const { return ls_id_; }

  TO_STRING_KV(K_(cluster_id),
      K_(tenant_id),
      K_(ls_id));
private:
  int64_t cluster_id_;
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
};

} // namespace logservice
} // namespace oceanbase

#endif
