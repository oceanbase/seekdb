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

#ifndef OCEANBASE_logservice_H_
#define OCEANBASE_logservice_H_

#include "common/ob_zone.h"                               // ObZone
#include "common/ob_region.h"                             // ObRegin
#include "common/ob_zone_type.h"                          // ObZoneType

namespace oceanbase
{
namespace logservice
{
struct AllZoneRecord
{
  common::ObZone zone_;
  common::ObRegion region_;

  AllZoneRecord() { reset(); }

  void reset()
  {
    zone_.reset();
    region_.reset();
  }

  int init(
      ObString &zone,
      ObString &region);

  TO_STRING_KV(K_(zone), K_(region));
};

struct AllZoneTypeRecord
{
  common::ObZone zone_;
  common::ObZoneType zone_type_;

  AllZoneTypeRecord() { reset(); }

  void reset()
  {
    zone_.reset();
    zone_type_ = common::ZONE_TYPE_INVALID;
  }

  int init(ObString &zone,
      common::ObZoneType &zone_type);

  TO_STRING_KV(K_(zone), K_(zone_type));
};

class ObAllZoneInfo
{
public:
  static const int64_t DEFAULT_RECORDS_NUM = 16;
  typedef common::ObSEArray<AllZoneRecord, DEFAULT_RECORDS_NUM> AllZoneRecordArray;
  ObAllZoneInfo() { reset(); }
  virtual ~ObAllZoneInfo() { reset(); }

  int init(const int64_t cluster_id);
  void reset();
  inline int64_t get_cluster_id() { return cluster_id_; }
  inline AllZoneRecordArray &get_all_zone_array() { return all_zone_array_; }
  int add(AllZoneRecord &record);

  TO_STRING_KV(K_(cluster_id), K_(all_zone_array));

private:
  int64_t cluster_id_;
  AllZoneRecordArray all_zone_array_;

  DISALLOW_COPY_AND_ASSIGN(ObAllZoneInfo);
};

class ObAllZoneTypeInfo
{
public:
  static const int64_t DEFAULT_RECORDS_NUM = 16;
  typedef common::ObSEArray<AllZoneTypeRecord, DEFAULT_RECORDS_NUM> AllZoneTypeRecordArray;
  ObAllZoneTypeInfo() { reset(); }
  virtual ~ObAllZoneTypeInfo() { reset(); }

  int init(const int64_t cluster_id);
  void reset();
  inline int64_t get_cluster_id() { return cluster_id_; }
  inline AllZoneTypeRecordArray &get_all_zone_type_array() { return all_zone_type_array_; }
  int add(AllZoneTypeRecord &record);

  TO_STRING_KV(K_(cluster_id), K_(all_zone_type_array));

private:
  int64_t cluster_id_;
  AllZoneTypeRecordArray all_zone_type_array_;

  DISALLOW_COPY_AND_ASSIGN(ObAllZoneTypeInfo);
};

} // namespace logservice
} // namespace oceanbase

#endif
