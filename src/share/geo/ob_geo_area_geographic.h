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

// The strategy body below is adapted from Boost.Geometry:
//
// Boost.Geometry (aka GGL, Generic Geometry Library)
//
// Copyright (c) 2017 Adam Wulkiewicz, Lodz, Poland.
//
// Copyright (c) 2016-2018 Oracle and/or its affiliates.
// Contributed and/or modified by Vissarion Fisikopoulos, on behalf of Oracle
// Contributed and/or modified by Adam Wulkiewicz, on behalf of Oracle
//
// Use, modification and distribution is subject to the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <cstddef>

#include <boost/mpl/if.hpp>
#include <boost/type_traits/is_void.hpp>

#include <boost/geometry/srs/spheroid.hpp>
#include <boost/geometry/formulas/area_formulas.hpp>
#include <boost/geometry/formulas/authalic_radius_sqr.hpp>
#include <boost/geometry/formulas/eccentricity_sqr.hpp>
#include <boost/geometry/strategies/area.hpp>
#include <boost/geometry/strategies/geographic/area.hpp>
#include <boost/geometry/strategies/geographic/parameters.hpp>

// The CORE-MATH backends need Clang 18 or newer on x86-64. Any other Windows
// toolchain, and every non-Windows platform, keeps the upstream strategy
// unchanged, so an unsupported compiler never silently changes this value.
#if defined(_WIN32) && defined(__clang__) && __clang_major__ >= 18 && defined(_M_X64)
#define OB_GEO_AREA_USES_CORE_MATH_ATANH 1
#include "share/ob_math_compat.h"
#else
#define OB_GEO_AREA_USES_CORE_MATH_ATANH 0
#endif

namespace oceanbase
{
namespace common
{
#if OB_GEO_AREA_USES_CORE_MATH_ATANH

namespace ob_geo_detail
{
namespace bg = boost::geometry;

// The authalic radius is the only quantity in the geographic area strategy that
// leaves the implementation through a transcendental function. Boost evaluates
// it with the caller policy's promoted type, so the result depends on the width
// of long double: 80-bit extended on Linux, the same 8 bytes as double on
// Windows. Route only that call through the shared math layer so both platforms
// evaluate the same expression at binary64, and keep everything else in the
// strategy identical to upstream.
template <typename T, typename Spheroid>
inline T ob_authalic_radius_sqr(T a2, T e2)
{
  const T c0 = T(0);
  const T c1 = T(1);
  const T c2 = T(2);
  if (bg::math::equals(e2, c0)) {
    return a2;
  }
  if (math_compat::atanh_uses_core_math()) {
    const T e = bg::math::sqrt(e2);
    return (a2 / c2) * (c1 + (c1 - e2) * math_compat::atanh(e) / e);
  }
  // Every FP environment the CORE-MATH backend does not cover keeps the
  // upstream expression, including boost::math::atanh and its log1p
  // combination. Substituting the CRT atanh here would evaluate a different
  // formula rather than the previous one.
  return bg::formula_dispatch::authalic_radius_sqr<T, Spheroid>::apply(a2, e2);
}
} // namespace ob_geo_detail

/*!
\brief Geographic area calculation
\details Geographic area calculation by trapezoidal rule plus integral
         approximation that gives the ellipsoidal correction. Identical to
         boost::geometry::strategy::area::geographic except that the authalic
         radius uses the shared math layer.
*/
template
<
    typename FormulaPolicy = boost::geometry::strategy::andoyer,
    std::size_t SeriesOrder = boost::geometry::strategy::default_order<FormulaPolicy>::value,
    typename Spheroid = boost::geometry::srs::spheroid<double>,
    typename CalculationType = void
>
class ObGeoAreaGeographic
{
  // Switch between two kinds of approximation(series in eps and n v.s.series in k ^ 2 and e'^2)
  static const bool ExpandEpsN = true;
  // LongSegment Enables special handling of long segments
  static const bool LongSegment = false;

public:
  template <typename Geometry>
  struct result_type
    : boost::geometry::strategy::area::detail::result_type
        <
            Geometry,
            CalculationType
        >
  {};

protected:
  struct spheroid_constants
  {
    typedef typename boost::mpl::if_c
        <
            boost::is_void<CalculationType>::value,
            typename boost::geometry::radius_type<Spheroid>::type,
            CalculationType
        >::type calc_t;

    Spheroid m_spheroid;
    calc_t const m_a2;  // squared equatorial radius
    calc_t const m_e2;  // squared eccentricity
    calc_t const m_ep2; // squared second eccentricity
    calc_t const m_ep;  // second eccentricity
    calc_t const m_c2;  // squared authalic radius

    inline spheroid_constants(Spheroid const& spheroid)
        : m_spheroid(spheroid)
        , m_a2(boost::geometry::math::sqr(boost::geometry::get_radius<0>(spheroid)))
        , m_e2(boost::geometry::formula::eccentricity_sqr<calc_t>(spheroid))
        , m_ep2(m_e2 / (calc_t(1.0) - m_e2))
        , m_ep(boost::geometry::math::sqrt(m_ep2))
        , m_c2(ob_geo_detail::ob_authalic_radius_sqr<calc_t, Spheroid>(m_a2, m_e2))
    {}
  };

public:
  template <typename Geometry>
  class state
  {
    friend class ObGeoAreaGeographic;

    typedef typename result_type<Geometry>::type return_type;

  public:
    inline state()
        : m_excess_sum(0)
        , m_correction_sum(0)
        , m_crosses_prime_meridian(0)
    {}

  private:
    inline return_type area(spheroid_constants const& spheroid_const) const
    {
      return_type result;

      return_type sum = spheroid_const.m_c2 * m_excess_sum
             + spheroid_const.m_e2 * spheroid_const.m_a2 * m_correction_sum;

      // If encircles some pole
      if (m_crosses_prime_meridian % 2 == 1)
      {
        std::size_t times_crosses_prime_meridian
                = 1 + (m_crosses_prime_meridian / 2);

        result = return_type(2.0)
                 * boost::geometry::math::pi<return_type>()
                 * spheroid_const.m_c2
                 * return_type(times_crosses_prime_meridian)
                 - boost::geometry::math::abs(sum);

        if (boost::geometry::math::sign<return_type>(sum) == 1)
        {
          result = - result;
        }
      }
      else
      {
        result = sum;
      }

      return result;
    }

    return_type m_excess_sum;
    return_type m_correction_sum;

    // Keep track if encircles some pole
    std::size_t m_crosses_prime_meridian;
  };

public:
  explicit inline ObGeoAreaGeographic(Spheroid const& spheroid = Spheroid())
      : m_spheroid_constants(spheroid)
  {}

  template <typename PointOfSegment, typename Geometry>
  inline void apply(PointOfSegment const& p1,
                    PointOfSegment const& p2,
                    state<Geometry>& st) const
  {
    if (! boost::geometry::math::equals(boost::geometry::get<0>(p1),
                                        boost::geometry::get<0>(p2)))
    {
      typedef boost::geometry::formula::area_formulas
          <
              typename result_type<Geometry>::type,
              SeriesOrder, ExpandEpsN
          > area_formulas;

      typename area_formulas::return_type_ellipsoidal result =
               area_formulas::template ellipsoidal<FormulaPolicy::template inverse>
                                       (p1, p2, m_spheroid_constants);

      st.m_excess_sum += result.spherical_term;
      st.m_correction_sum += result.ellipsoidal_term;

      // Keep track whenever a segment crosses the prime meridian
      if (area_formulas::crosses_prime_meridian(p1, p2))
      {
        st.m_crosses_prime_meridian++;
      }
    }
  }

  template <typename Geometry>
  inline typename result_type<Geometry>::type
      result(state<Geometry> const& st) const
  {
    return st.area(m_spheroid_constants);
  }

private:
  spheroid_constants m_spheroid_constants;
};

template
<
    typename FormulaPolicy = boost::geometry::strategy::andoyer,
    std::size_t SeriesOrder = boost::geometry::strategy::default_order<FormulaPolicy>::value,
    typename Spheroid = boost::geometry::srs::spheroid<double>,
    typename CalculationType = void
>
using ObGeoAreaStrategy = ObGeoAreaGeographic
    <FormulaPolicy, SeriesOrder, Spheroid, CalculationType>;

#else // !OB_GEO_AREA_USES_CORE_MATH_ATANH

// Other platforms, and Windows toolchains without the CORE-MATH backend, keep
// the upstream strategy and its promoted evaluation.
template
<
    typename FormulaPolicy = boost::geometry::strategy::andoyer,
    std::size_t SeriesOrder = boost::geometry::strategy::default_order<FormulaPolicy>::value,
    typename Spheroid = boost::geometry::srs::spheroid<double>,
    typename CalculationType = void
>
using ObGeoAreaStrategy = boost::geometry::strategy::area::geographic
    <FormulaPolicy, SeriesOrder, Spheroid, CalculationType>;

#endif // OB_GEO_AREA_USES_CORE_MATH_ATANH

} // namespace common
} // namespace oceanbase
