/**
 * Copyright (c) 2025 OceanBase
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
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is a modified copy of Boost.Geometry's
 * boost/geometry/strategies/cartesian/buffer_point_circle.hpp at Boost 1.74.
 * The original is:
 *
 *   Copyright (c) 2012-2015 Barend Gehrels, Amsterdam, the Netherlands.
 *
 *   This file was modified by Oracle on 2015, 2018.
 *   Modifications copyright (c) 2015, 2018, Oracle and/or its affiliates.
 *
 *   Contributed and/or modified by Menelaos Karavelas, on behalf of Oracle
 *   Contributed and/or modified by Adam Wulkiewicz, on behalf of Oracle
 *
 *   Use, modification and distribution is subject to the Boost Software
 *   License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
 *   http://www.boost.org/LICENSE_1_0.txt)
 */

#pragma once

#include <cstddef>

#include <boost/geometry/core/access.hpp>
#include <boost/geometry/core/coordinate_type.hpp>
#include <boost/geometry/strategies/buffer.hpp>
#include <boost/geometry/strategies/geographic/parameters.hpp>
#include <boost/geometry/util/math.hpp>
#include <boost/geometry/util/select_most_precise.hpp>
#include <boost/range/value_type.hpp>

#include "share/ob_math_compat.h"

namespace oceanbase
{
namespace share
{
namespace geo
{

// Buffer point-circle strategy mirroring boost::geometry::strategy::buffer::point_circle,
// but evaluating the circle coordinates through the shared math compatibility layer so
// that the same round geometry is produced on every supported platform.
template <typename T = void>
class ObBufferPointCircle
{
public:
  explicit ObBufferPointCircle(std::size_t count = 90)
    : count_((count < 3U) ? 3U : count)
  {}

  template <typename Point, typename OutputRange, typename DistanceStrategy>
  inline void apply(Point const &point,
                    DistanceStrategy const &distance_strategy,
                    OutputRange &output_range) const
  {
    typedef typename boost::range_value<OutputRange>::type output_point_type;
    typedef typename boost::geometry::select_most_precise<
        typename boost::geometry::select_most_precise<
            typename boost::geometry::coordinate_type<Point>::type,
            typename boost::geometry::coordinate_type<output_point_type>::type>::type,
        double>::type promoted_type;

    promoted_type const buffer_distance =
        distance_strategy.apply(point, point, boost::geometry::strategy::buffer::buffer_side_left);
    promoted_type const two_pi = boost::geometry::math::two_pi<promoted_type>();
    promoted_type const diff = two_pi / promoted_type(count_);
    promoted_type a = 0;

    for (std::size_t i = 0; i < count_; i++, a -= diff) {
      output_point_type p;
      boost::geometry::set<0>(p, boost::geometry::get<0>(point) + buffer_distance * math_compat::cos(a));
      boost::geometry::set<1>(p, boost::geometry::get<1>(point) + buffer_distance * math_compat::sin(a));
      output_range.push_back(p);
    }

    // Close it:
    output_range.push_back(output_range.front());
  }

private:
  std::size_t count_;
};

} // namespace geo
} // namespace share
} // namespace oceanbase
