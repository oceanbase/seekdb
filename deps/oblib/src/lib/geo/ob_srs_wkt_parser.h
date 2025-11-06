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

#ifndef OCEANBASE_LIB_GEO_OB_SRS_WKT_PARSER_
#define OCEANBASE_LIB_GEO_OB_SRS_WKT_PARSER_

#include "lib/geo/ob_srs_info.h"
#include "lib/string/ob_string_buffer.h"

namespace oceanbase
{

namespace  common
{

// read str from qi
struct ObQiString
{
  ObArenaAllocator allocator_;
  ObStringBuffer val_;

  ObQiString():allocator_("QiString"), val_(&allocator_) {}
  ObQiString(const ObQiString& other):allocator_("QiString"), val_(&allocator_) {
    this->val_.append(other.val_.string());
  }
};

class ObSrsWktParser final
{
public:
  ObSrsWktParser() {}
  ~ObSrsWktParser() {}

public:
  static int parse_srs_wkt(common::ObIAllocator& allocator, uint64_t srid,
                           const common::ObString &srs_str,
                           ObSpatialReferenceSystemBase *&srs);

  // for parser test currently
  static int parse_geog_srs_wkt(common::ObIAllocator& allocator, const common::ObString &srs_str, ObGeographicRs &result);
  static int parse_proj_srs_wkt(common::ObIAllocator& allocator, const common::ObString &srs_str, ObProjectionRs &result);

private:
  DISALLOW_COPY_AND_ASSIGN(ObSrsWktParser);
};

} // common
} // oceanbase

#endif /* OCEANBASE_LIB_GIS_OB_SRS_WKT_PARSER_ */
