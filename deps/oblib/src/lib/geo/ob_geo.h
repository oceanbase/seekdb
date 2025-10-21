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

#ifndef OCEANBASE_LIB_GEO_OB_GEO_
#define OCEANBASE_LIB_GEO_OB_GEO_

#include "lib/string/ob_string.h"
#include "lib/geo/ob_geo_common.h"

namespace oceanbase {
namespace common {

class ObIGeoVisitor;
class ObGeometry {
public:
    // constructor
    ObGeometry(uint32_t srid = 0)
        : srid_(srid),
          version_(ENCODE_GEO_VERSION(GEO_VESION_1))  {}
    virtual ~ObGeometry() = default;
    ObGeometry(const ObGeometry& g) = default;
    ObGeometry& operator=(const ObGeometry& g) = default;
    // wkb interface
    virtual void set_data(const ObString& data) = 0;
    virtual ObString to_wkb() const { return ObString(); }
    virtual uint64_t length() const { return 0; }
    // val interface, do cast outside by functor
    virtual const char* val() const = 0;
    // Geo interface
    virtual ObGeoType type() const = 0;
    virtual ObGeoCRS crs() const = 0;
    virtual bool is_tree() const = 0;
    virtual bool is_empty() const = 0;
    // visitor
    virtual int do_visit(ObIGeoVisitor &visitor) = 0;
    // srid
    uint32_t get_srid() const { return srid_; }
    void set_srid(uint32_t srid) { srid_ = srid; }
    // version
    uint8_t get_version() { return version_; }
    VIRTUAL_TO_STRING_KV(K_(srid));
protected:
    uint32_t srid_;
    uint8_t version_;
};


} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_OB_GEO_
