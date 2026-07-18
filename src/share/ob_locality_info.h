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

#ifndef OCEANBASE_SHARE_OB_LOCALITY_INFO_H_
#define OCEANBASE_SHARE_OB_LOCALITY_INFO_H_

#include "lib/net/ob_addr.h"
#include "share/ob_define.h"
#include "common/ob_zone_type.h"
#include "common/ob_zone_status.h"
#include "share/ob_zone_merge_info.h"
#include "lib/container/ob_se_array.h"
#include "lib/lock/ob_spin_rwlock.h"

namespace oceanbase
{
namespace share
{
struct ObLocalityZone
{
public:
  ObLocalityZone() { reset(); }
  ~ObLocalityZone() {}
  void reset();
  ObLocalityZone &operator =(const ObLocalityZone &item);
  TO_STRING_EMPTY();
};

struct ObLocalityInfo
{
public:
  ObLocalityInfo() { reset(); }
  ~ObLocalityInfo() {}
  void reset();
  void destroy();
  int add_locality_zone(const ObLocalityZone &item);
  int get_locality_zone(ObLocalityZone &item);
  void set_version(const int64_t version);
  int64_t get_version() const;
  int copy_to(ObLocalityInfo &locality_info);
  common::ObZoneType get_local_zone_type();
  TO_STRING_KV(K_(version), K_(local_zone),
               K_(local_zone_type), K_(local_zone_status),
               K_(locality_zone_array));
  bool is_valid();
public:
  typedef common::ObFixedLengthString<common::MAX_ZONE_LENGTH> Zone;
  typedef common::ObSEArray<ObLocalityZone, 32> ObLocalityZoneArray;
public:
  int64_t version_;
  Zone local_zone_; // local zone
  common::ObZoneType local_zone_type_; // local zone_type
  ObLocalityZoneArray locality_zone_array_; //tenant level locality info
  ObZoneStatus::Status local_zone_status_;
};

} // end namespace share
} // end namespace oceanbase
#endif
