//
// Copyright (c) 2025 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// This file is a modified copy of Boost.Geometry's
// boost/geometry/strategies/cartesian/buffer_join_round.hpp at Boost 1.74.
// The original is:
//
//   Copyright (c) 2012-2015 Barend Gehrels, Amsterdam, the Netherlands.
//
//   This file was modified by Oracle on 2015.
//   Modifications copyright (c) 2015, Oracle and/or its affiliates.
//
//   Contributed and/or modified by Menelaos Karavelas, on behalf of Oracle
//
//   Use, modification and distribution is subject to the Boost Software
//   License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
//   http://www.boost.org/LICENSE_1_0.txt)
//
// Modification (OceanBase): the general-angle trigonometric calls inside
// generate_points use oceanbase::math_compat::cos/sin instead of the C library
// so every platform follows the same unified binary64 math path. Angle
// normalization, ceil/max step count, promoted type selection,
// equal-perpendicular handling, absolute distance, point push order and
// max_distance are unchanged.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <boost/geometry/arithmetic/arithmetic.hpp>
#include <boost/geometry/core/access.hpp>
#include <boost/geometry/core/coordinate_type.hpp>
#include <boost/geometry/policies/compare.hpp>
#include <boost/geometry/core/radian_access.hpp>
#include <boost/geometry/core/radius.hpp>
#include <boost/geometry/core/tag.hpp>
#include <boost/geometry/core/tags.hpp>
#include <boost/geometry/strategies/buffer.hpp>
#include <boost/geometry/strategies/tags.hpp>
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

// Same public surface as boost::geometry::strategy::buffer::join_round so it can
// be passed directly to bg::buffer in place of the stock strategy.
template <typename PointInConst = void, typename Point = void, typename PointOfSegment = void>
struct ObBufferJoinRound
{
  //! \brief Constructor
  //! \param points_per_circle points which would be used for a full circle
  explicit inline ObBufferJoinRound(std::size_t points_per_circle = 90)
    : m_points_per_circle(points_per_circle)
  {}

private:
  template <typename PromotedType, typename PointT, typename DistanceType, typename RangeOut>
  inline void generate_points(PointT const &vertex,
                              PointT const &perp1,
                              PointT const &perp2,
                              DistanceType const &buffer_distance,
                              RangeOut &range_out) const
  {
    PromotedType const dx1 = boost::geometry::get<0>(perp1) - boost::geometry::get<0>(vertex);
    PromotedType const dy1 = boost::geometry::get<1>(perp1) - boost::geometry::get<1>(vertex);
    PromotedType const dx2 = boost::geometry::get<0>(perp2) - boost::geometry::get<0>(vertex);
    PromotedType const dy2 = boost::geometry::get<1>(perp2) - boost::geometry::get<1>(vertex);

    PromotedType const two_pi = boost::geometry::math::two_pi<PromotedType>();

    PromotedType const angle1 = static_cast<PromotedType>(
        math_compat::atan2(static_cast<double>(dy1), static_cast<double>(dx1)));
    PromotedType angle2 = static_cast<PromotedType>(
        math_compat::atan2(static_cast<double>(dy2), static_cast<double>(dx2)));
    while (angle2 > angle1)
    {
      angle2 -= two_pi;
    }
    PromotedType const angle_diff = angle1 - angle2;

    std::size_t const n = (std::max)(static_cast<std::size_t>(
        std::ceil(m_points_per_circle * angle_diff / two_pi)), std::size_t(1));

    PromotedType const diff = angle_diff / static_cast<PromotedType>(n);
    PromotedType a = angle1 - diff;

    // Walk to n - 1 to avoid generating the last point
    for (std::size_t i = 0; i < n - 1; i++, a -= diff)
    {
      PointT p;
      boost::geometry::set<0>(p, boost::geometry::get<0>(vertex) + buffer_distance * math_compat::cos(a));
      boost::geometry::set<1>(p, boost::geometry::get<1>(vertex) + buffer_distance * math_compat::sin(a));
      range_out.push_back(p);
    }
  }

public:
  //! Fills output_range with a rounded shape around a vertex
  template <typename PT, typename DistanceType, typename RangeOut>
  inline bool apply(PT const &ip, PT const &vertex, PT const &perp1, PT const &perp2,
                    DistanceType const &buffer_distance, RangeOut &range_out) const
  {
    typedef typename boost::geometry::coordinate_type<PT>::type coordinate_type;
    typedef typename boost::range_value<RangeOut>::type output_point_type;

    typedef typename boost::geometry::select_most_precise<
        typename boost::geometry::select_most_precise<
            coordinate_type,
            typename boost::geometry::coordinate_type<output_point_type>::type>::type,
        double>::type promoted_type;

    boost::geometry::equal_to<PT> equals;
    if (equals(perp1, perp2))
    {
      return false;
    }

    // Generate 'vectors'
    coordinate_type vix = (boost::geometry::get<0>(ip) - boost::geometry::get<0>(vertex));
    coordinate_type viy = (boost::geometry::get<1>(ip) - boost::geometry::get<1>(vertex));

    promoted_type length_i = boost::geometry::math::sqrt(vix * vix + viy * viy);
    DistanceType const bd = boost::geometry::math::abs(buffer_distance);
    promoted_type prop = bd / length_i;

    PT bp;
    boost::geometry::set<0>(bp, boost::geometry::get<0>(vertex) + vix * prop);
    boost::geometry::set<1>(bp, boost::geometry::get<1>(vertex) + viy * prop);

    range_out.push_back(perp1);
    generate_points<promoted_type>(vertex, perp1, perp2, bd, range_out);
    range_out.push_back(perp2);
    return true;
  }

  template <typename NumericType>
  static inline NumericType max_distance(NumericType const &distance)
  {
    return distance;
  }

private:
  std::size_t m_points_per_circle;
};

} // namespace geo
} // namespace share
} // namespace oceanbase
