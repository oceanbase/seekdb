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

#ifndef OCEANBASE_LIB_OB_GEO_FUNC_COMMON_H_
#define OCEANBASE_LIB_OB_GEO_FUNC_COMMON_H_

#define BOOST_GEOMETRY_DISABLE_DEPRECATED_03_WARNING 1
#define BOOST_ALLOW_DEPRECATED_HEADERS 1

#include <exception>
#pragma push_macro("E")
#undef E
#ifdef _WIN32
#pragma push_macro("S")
#undef S
#endif
#include <boost/geometry.hpp>
#include <boost/geometry/core/exception.hpp>
#ifdef _WIN32
#pragma pop_macro("S")
#endif
#pragma pop_macro("E")
#include "lib/ob_errno.h"
#include "share/geo/ob_geo_bin.h"
#include "share/geo/ob_geo_bin_traits.h"
#include "share/geo/ob_geo_tree_traits.h"
#include "share/geo/ob_geo_eval_ctx.h"
#include "lib/string/ob_string.h"
//#include "lib/allocator/ob_allocator.h"

namespace oceanbase
{
namespace common
{
// boost::geometry strategies
typedef boost::geometry::strategy::within::geographic_winding<common::ObWkbGeogPoint> ObPlPaStrategy;
typedef boost::geometry::strategy::intersection::geographic_segments<> ObLlLaAaStrategy;

} // sql
} // oceanbase
#endif // OCEANBASE_LIB_OB_GEO_FUNC_COMMON_H_
