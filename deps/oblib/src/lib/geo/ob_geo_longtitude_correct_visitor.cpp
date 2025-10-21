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

#define USING_LOG_PREFIX LIB
#include "ob_geo_longtitude_correct_visitor.h"

namespace oceanbase {
namespace common {

int ObGeoLongtitudeCorrectVisitor::visit(ObGeographPoint *geo)
{
  int32_t ret = OB_SUCCESS;
  if (OB_ISNULL(srs_)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("srs is null", K(ret));
  } else if (srs_->srs_type() == ObSrsType::PROJECTED_SRS) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is projected type", K(srs_));
  } else {
    double longti = geo->x();
    longti -= srs_->prime_meridian() * srs_->angular_unit();
    if (!srs_->is_longtitude_east()) {
      longti *= -1.0;
    }
    if (longti <= -M_PI) { // longtitude < -180
      geo->x(longti + 2.0 * M_PI);
    } else if (longti > M_PI) {
      geo->x(longti - 2.0 * M_PI);
    }
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
