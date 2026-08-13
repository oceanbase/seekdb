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

#ifndef OCEANBASE_SHARE_GEO_OB_SRS_PROVIDER_H_
#define OCEANBASE_SHARE_GEO_OB_SRS_PROVIDER_H_

#include <stdint.h>
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace common
{

class ObSrsItem;
struct ObSrsBoundsItem;

// Stable snapshot seam owned by Share.  The Observer adapter keeps the
// concrete cache and its reference-counting policy behind this interface.
class ObISrsSnapshot
{
public:
  virtual ~ObISrsSnapshot() = default;
  virtual void retain() = 0;
  virtual void release() = 0;
  virtual int get_srs_item(
      uint64_t srs_id,
      const ObSrsItem *&srs_item) = 0;
};

// Owns one reference to a provider snapshot without exposing the concrete
// tenant cache to SQL or object-cast code.
class ObSrsCacheGuard
{
public:
  ObSrsCacheGuard() : snapshot_(nullptr) {}

  ~ObSrsCacheGuard()
  {
    if (nullptr != snapshot_) {
      snapshot_->release();
    }
  }

  int get_srs_item(uint64_t srs_id, const ObSrsItem *&srs_item) const
  {
    return nullptr == snapshot_
        ? OB_NOT_INIT
        : snapshot_->get_srs_item(srs_id, srs_item);
  }
  bool empty() const { return nullptr == snapshot_; }

  void bind(ObISrsSnapshot &snapshot)
  {
    if (nullptr == snapshot_) {
      snapshot.retain();
      snapshot_ = &snapshot;
    }
  }

private:
  ObISrsSnapshot *snapshot_;

  ObSrsCacheGuard(const ObSrsCacheGuard &) = delete;
  ObSrsCacheGuard &operator=(const ObSrsCacheGuard &) = delete;
};

// Share-owned SRS lookup interface.  Observer's tenant SRS module is the
// production adapter; callers receive it through their existing SQL context.
class ObISrsProvider
{
public:
  virtual ~ObISrsProvider() = default;
  virtual int get_tenant_srs_guard(ObSrsCacheGuard &srs_guard) = 0;
  virtual int get_srs_bounds(
      uint64_t srid,
      const ObSrsItem *srs_item,
      const ObSrsBoundsItem *&bounds_item) = 0;
};

} // namespace common

} // namespace oceanbase

#endif // OCEANBASE_SHARE_GEO_OB_SRS_PROVIDER_H_
