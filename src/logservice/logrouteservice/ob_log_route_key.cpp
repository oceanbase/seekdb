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
#define USING_LOG_PREFIX OBLOG_FETCHER

#include "ob_log_route_key.h"

namespace oceanbase
{
namespace logservice
{
ObLSRouterKey::ObLSRouterKey()
    : cluster_id_(OB_INVALID_CLUSTER_ID),
      tenant_id_(OB_INVALID_TENANT_ID),
      ls_id_()
{
}

ObLSRouterKey::ObLSRouterKey(
    const int64_t cluster_id,
    const uint64_t tenant_id,
    const share::ObLSID &ls_id)
    : cluster_id_(cluster_id), tenant_id_(tenant_id), ls_id_(ls_id)
{
}

void ObLSRouterKey::reset()
{
  cluster_id_ = OB_INVALID_CLUSTER_ID;
  tenant_id_ = OB_INVALID_TENANT_ID;
  ls_id_.reset();
}

bool ObLSRouterKey::operator ==(const ObLSRouterKey &other_key) const
{
  return cluster_id_ == other_key.cluster_id_
      && tenant_id_ == other_key.tenant_id_
      && ls_id_ == other_key.ls_id_;
}

bool ObLSRouterKey::operator !=(const ObLSRouterKey &other) const
{
  return !(*this == other);
}

bool ObLSRouterKey::is_valid() const
{
  return OB_INVALID_CLUSTER_ID != cluster_id_
      && OB_INVALID_TENANT_ID != tenant_id_
      && ls_id_.is_valid();
}

uint64_t ObLSRouterKey::hash() const
{
  uint64_t hash_val = 0;

  hash_val = murmurhash(&cluster_id_, sizeof(cluster_id_), hash_val);
  hash_val = murmurhash(&tenant_id_, sizeof(tenant_id_), hash_val);
  hash_val = murmurhash(&ls_id_, sizeof(ls_id_), hash_val);

  return hash_val;
}

int ObLSRouterKey::hash(uint64_t &hash_val) const
{
  hash_val = hash();
  return OB_SUCCESS;
}

int ObLSRouterKey::deep_copy(char *buf,
    const int64_t buf_len,
    ObLSRouterKey *&key) const
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < size())) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(buf), K(buf_len), "size", size());
  } else {
    ObLSRouterKey *pkey = new (buf) ObLSRouterKey();
    *pkey = *this;
    key = pkey;
  }

  return ret;
}

} // namespace logservice
} // namespace oceanbase
