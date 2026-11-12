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

// The andoyer inverse body below is adapted from Boost.Geometry:
//
// Copyright (c) 2018 Adam Wulkiewicz, Lodz, Poland.
// Copyright (c) 2015-2020 Oracle and/or its affiliates.
//
// Use, modification and distribution is subject to the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <cstddef>

#include <boost/mpl/if.hpp>
#include <boost/mpl/integral_c.hpp>
#include <boost/type_traits/is_void.hpp>

#include <boost/geometry/core/radius.hpp>
#include <boost/geometry/formulas/andoyer_inverse.hpp>
#include <boost/geometry/formulas/area_formulas.hpp>
#include <boost/geometry/formulas/authalic_radius_sqr.hpp>
#include <boost/geometry/formulas/differential_quantities.hpp>
#include <boost/geometry/formulas/eccentricity_sqr.hpp>
#include <boost/geometry/formulas/flattening.hpp>
#include <boost/geometry/formulas/result_inverse.hpp>
#include <boost/geometry/srs/spheroid.hpp>
#include <boost/geometry/strategies/area.hpp>
#include <boost/geometry/strategies/geographic/area.hpp>
#include <boost/geometry/strategies/geographic/parameters.hpp>
#include <boost/geometry/util/condition.hpp>
#include <boost/geometry/util/math.hpp>

// Every platform links the same pinned binary64 backend, so the geographic
// area strategy evaluates identical operations everywhere.
#include "share/ob_math_compat.h"

namespace oceanbase
{
namespace common
{
namespace ob_geo_detail
{
namespace bg = boost::geometry;

// The geographic area strategy reaches a transcendental function in two places:
// the authalic radius below, evaluated once per spheroid, and the Andoyer
// inverse, evaluated per segment through sin/cos/acos/atan2. Both are shipped
// by the platform libm, whose last-bit result is not identical between the
// Windows CRT and Linux glibc for some inputs. The shared math layer defines a
// single value for the functions it covers; everything not covered here keeps
// the upstream expression unchanged.
template <typename T, typename Spheroid>
inline T ob_authalic_radius_sqr(T a2, T e2)
{
  const T c0 = T(0);
  const T c1 = T(1);
  const T c2 = T(2);
  if (bg::math::equals(e2, c0)) {
    return a2;
  }
  // Evaluate the formula in binary64 everywhere; the shared layer defines the
  // single atanh value used by every platform.
  const T e = bg::math::sqrt(e2);
  return (a2 / c2) * (c1 + (c1 - e2) * math_compat::atanh(e) / e);
}

/*!
\brief Andoyer inverse on the shared platform-independent math layer
\details Byte-for-byte copy of boost::geometry::formula::andoyer_inverse except
         that the transcendentals (sin/cos/acos/atan2) are served by the shared
         binary64 layer. Same operations, same order, same branches, same
         normalize_azimuth.
*/
template
<
    typename CT,
    bool EnableDistance,
    bool EnableAzimuth,
    bool EnableReverseAzimuth = false,
    bool EnableReducedLength = false,
    bool EnableGeodesicScale = false
>
class ObAndoyerInverse
{
    static const bool CalcQuantities = EnableReducedLength || EnableGeodesicScale;
    static const bool CalcAzimuths = EnableAzimuth || EnableReverseAzimuth || CalcQuantities;
    static const bool CalcFwdAzimuth = EnableAzimuth || CalcQuantities;
    static const bool CalcRevAzimuth = EnableReverseAzimuth || CalcQuantities;

public:
    typedef bg::formula::result_inverse<CT> result_type;

    template <typename T1, typename T2, typename Spheroid>
    static inline result_type apply(T1 const& lon1,
                                    T1 const& lat1,
                                    T2 const& lon2,
                                    T2 const& lat2,
                                    Spheroid const& spheroid)
    {
        result_type result;

        // coordinates in radians

        if ( bg::math::equals(lon1, lon2) && bg::math::equals(lat1, lat2) )
        {
            return result;
        }

        CT const c0 = CT(0);
        CT const c1 = CT(1);
        CT const pi = bg::math::pi<CT>();
        CT const f = bg::formula::flattening<CT>(spheroid);

        CT const dlon = lon2 - lon1;
        CT const sin_dlon = math_compat::sin(dlon);
        CT const cos_dlon = math_compat::cos(dlon);
        CT const sin_lat1 = math_compat::sin(lat1);
        CT const cos_lat1 = math_compat::cos(lat1);
        CT const sin_lat2 = math_compat::sin(lat2);
        CT const cos_lat2 = math_compat::cos(lat2);

        CT cos_d = sin_lat1*sin_lat2 + cos_lat1*cos_lat2*cos_dlon;
        // on some platforms cos_d may be outside valid range
        if (cos_d < -c1)
            cos_d = -c1;
        else if (cos_d > c1)
            cos_d = c1;

        CT const d = static_cast<CT>(math_compat::acos(static_cast<double>(cos_d))); // [0, pi]
        CT const sin_d = math_compat::sin(d);  // [-1, 1]

        if ( BOOST_GEOMETRY_CONDITION(EnableDistance) )
        {
            CT const K = bg::math::sqr(sin_lat1-sin_lat2);
            CT const L = bg::math::sqr(sin_lat1+sin_lat2);
            CT const three_sin_d = CT(3) * sin_d;

            CT const one_minus_cos_d = c1 - cos_d;
            CT const one_plus_cos_d = c1 + cos_d;

            CT const H = bg::math::equals(one_minus_cos_d, c0) ?
                            c0 :
                            (d + three_sin_d) / one_minus_cos_d;
            CT const G = bg::math::equals(one_plus_cos_d, c0) ?
                            c0 :
                            (d - three_sin_d) / one_plus_cos_d;

            CT const dd = -(f/CT(4))*(H*K+G*L);

            CT const a = CT(bg::get_radius<0>(spheroid));

            result.distance = a * (d + dd);
        }

        if ( BOOST_GEOMETRY_CONDITION(CalcAzimuths) )
        {
            // sin_d = 0 <=> antipodal points (incl. poles) or very close
            if (bg::math::equals(sin_d, c0))
            {
                // points very close
                if (cos_d >= c0)
                {
                    result.azimuth = c0;
                    result.reverse_azimuth = c0;
                }
                // antipodal points
                else
                {
                    // Set azimuth to 0 unless the first endpoint is the north pole
                    if (! bg::math::equals(sin_lat1, c1))
                    {
                        result.azimuth = c0;
                        result.reverse_azimuth = pi;
                    }
                    else
                    {
                        result.azimuth = pi;
                        result.reverse_azimuth = c0;
                    }
                }
            }
            else
            {
                CT const c2 = CT(2);

                CT A = c0;
                CT U = c0;
                if (bg::math::equals(cos_lat2, c0))
                {
                    if (sin_lat2 < c0)
                    {
                        A = pi;
                    }
                }
                else
                {
                    CT const tan_lat2 = sin_lat2/cos_lat2;
                    CT const M = cos_lat1*tan_lat2-sin_lat1*cos_dlon;
                    A = static_cast<CT>(math_compat::atan2(static_cast<double>(sin_dlon), static_cast<double>(M)));
                    CT const sin_2A = math_compat::sin(c2*A);
                    U = (f/ c2)*bg::math::sqr(cos_lat1)*sin_2A;
                }

                CT B = c0;
                CT V = c0;
                if (bg::math::equals(cos_lat1, c0))
                {
                    if (sin_lat1 < c0)
                    {
                        B = pi;
                    }
                }
                else
                {
                    CT const tan_lat1 = sin_lat1/cos_lat1;
                    CT const N = cos_lat2*tan_lat1-sin_lat2*cos_dlon;
                    B = static_cast<CT>(math_compat::atan2(static_cast<double>(sin_dlon), static_cast<double>(N)));
                    CT const sin_2B = math_compat::sin(c2*B);
                    V = (f/ c2)*bg::math::sqr(cos_lat2)*sin_2B;
                }

                CT const T = d / sin_d;

                if (BOOST_GEOMETRY_CONDITION(CalcFwdAzimuth))
                {
                    CT const dA = V*T - U;
                    result.azimuth = A - dA;
                    normalize_azimuth(result.azimuth, A, dA);
                }

                if (BOOST_GEOMETRY_CONDITION(CalcRevAzimuth))
                {
                    CT const dB = -U*T + V;
                    if (B >= 0)
                        result.reverse_azimuth = pi - B - dB;
                    else
                        result.reverse_azimuth = -pi - B - dB;
                    normalize_azimuth(result.reverse_azimuth, B, dB);
                }
            }
        }

        if (BOOST_GEOMETRY_CONDITION(CalcQuantities))
        {
            CT const b = CT(bg::get_radius<2>(spheroid));

            typedef bg::formula::differential_quantities
                <
                    CT, EnableReducedLength, EnableGeodesicScale, 1
                > quantities;
            quantities::apply(dlon, sin_lat1, cos_lat1, sin_lat2, cos_lat2,
                              result.azimuth, result.reverse_azimuth,
                              b, f,
                              result.reduced_length, result.geodesic_scale);
        }

        return result;
    }

private:
    static inline void normalize_azimuth(CT & azimuth, CT const& A, CT const& dA)
    {
        CT const c0 = 0;

        if (A >= c0) // A indicates Eastern hemisphere
        {
            if (dA >= c0) // A altered towards 0
            {
                if (azimuth < c0)
                {
                    azimuth = c0;
                }
            }
            else // dA < 0, A altered towards pi
            {
                CT const pi = bg::math::pi<CT>();
                if (azimuth > pi)
                {
                    azimuth = pi;
                }
            }
        }
        else // A indicates Western hemisphere
        {
            if (dA <= c0) // A altered towards 0
            {
                if (azimuth > c0)
                {
                    azimuth = c0;
                }
            }
            else // dA > 0, A altered towards -pi
            {
                CT const minus_pi = -bg::math::pi<CT>();
                if (azimuth < minus_pi)
                {
                    azimuth = minus_pi;
                }
            }
        }
    }
};

/*!
\brief Area formula policy whose inverse uses the defined cosine
\details Mirrors boost::geometry::strategy::andoyer for every member the area
         formulas need; only the inverse is replaced.
*/
struct ObAndoyerPolicy
    : public bg::strategy::andoyer
{
    template
    <
        typename CT,
        bool EnableDistance,
        bool EnableAzimuth,
        bool EnableReverseAzimuth = false,
        bool EnableReducedLength = false,
        bool EnableGeodesicScale = false
    >
    struct inverse
        : public ObAndoyerInverse
            <
                CT, EnableDistance, EnableAzimuth,
                EnableReverseAzimuth, EnableReducedLength, EnableGeodesicScale
            >
    {};
};

/*!
\brief Area formulas on the shared platform-independent math layer
\details Same body as boost::geometry::formula::area_formulas::ellipsoidal,
         including the short-segment atan2 branch, the long-segment
         alp2 - alp1 branch, the series expansion selected by ExpandEpsN, and
         every coefficient helper inherited unchanged from upstream. The
         transcendentals are served by the shared binary64 layer.
*/
template <typename CT, std::size_t SeriesOrder, bool ExpandEpsN>
struct ObAreaFormulas
    : public bg::formula::area_formulas<CT, SeriesOrder, ExpandEpsN>
{
  typedef bg::formula::area_formulas<CT, SeriesOrder, ExpandEpsN> base_type;
  using return_type_ellipsoidal = typename base_type::return_type_ellipsoidal;

  template <template <typename, bool, bool, bool, bool, bool> class Inverse,
            typename PointOfSegment,
            typename SpheroidConst>
  static inline return_type_ellipsoidal ellipsoidal(
      PointOfSegment const& p1,
      PointOfSegment const& p2,
      SpheroidConst const& spheroid_const)
  {
    return_type_ellipsoidal result;

    // Azimuth Approximation

    typedef Inverse<CT, false, true, true, false, false> inverse_type;
    typedef typename inverse_type::result_type inverse_result;

    inverse_result i_res = inverse_type::apply(bg::get_as_radian<0>(p1),
                                               bg::get_as_radian<1>(p1),
                                               bg::get_as_radian<0>(p2),
                                               bg::get_as_radian<1>(p2),
                                               spheroid_const.m_spheroid);

    CT alp1 = i_res.azimuth;
    CT alp2 = i_res.reverse_azimuth;

    // Constants

    CT const ep = spheroid_const.m_ep;
    CT const f = bg::formula::flattening<CT>(spheroid_const.m_spheroid);
    CT const one_minus_f = CT(1) - f;
    std::size_t const series_order_plus_one = SeriesOrder + 1;
    std::size_t const series_order_plus_two = SeriesOrder + 2;

    // Basic trigonometric computations

    CT tan_bet1 = math_compat::tan(bg::get_as_radian<1>(p1)) * one_minus_f;
    CT tan_bet2 = math_compat::tan(bg::get_as_radian<1>(p2)) * one_minus_f;
    CT cos_bet1 = math_compat::cos(math_compat::atan(tan_bet1));
    CT cos_bet2 = math_compat::cos(math_compat::atan(tan_bet2));
    CT sin_bet1 = tan_bet1 * cos_bet1;
    CT sin_bet2 = tan_bet2 * cos_bet2;
    CT sin_alp1 = static_cast<CT>(math_compat::sin(static_cast<double>(alp1)));
    CT cos_alp1 = math_compat::cos(alp1);
    CT cos_alp2 = math_compat::cos(alp2);
    CT sin_alp0 = sin_alp1 * cos_bet1;

    // Spherical term computation

    CT sin_omg1 = sin_alp0 * sin_bet1;
    CT cos_omg1 = cos_alp1 * cos_bet1;
    CT sin_omg2 = sin_alp0 * sin_bet2;
    CT cos_omg2 = cos_alp2 * cos_bet2;
    CT cos_omg12 =  cos_omg1 * cos_omg2 + sin_omg1 * sin_omg2;
    CT excess;

    bool meridian = bg::get<0>(p2) - bg::get<0>(p1) == CT(0)
          || bg::get<1>(p1) == CT(90) || bg::get<1>(p1) == -CT(90)
          || bg::get<1>(p2) == CT(90) || bg::get<1>(p2) == -CT(90);

    if (!meridian && cos_omg12 > -CT(0.7)
                  && sin_bet2 - sin_bet1 < CT(1.75)) // short segment
    {
        CT sin_omg12 =  cos_omg1 * sin_omg2 - sin_omg1 * cos_omg2;
        base_type::normalize(sin_omg12, cos_omg12);

        CT cos_omg12p1 = CT(1) + cos_omg12;
        CT cos_bet1p1 = CT(1) + cos_bet1;
        CT cos_bet2p1 = CT(1) + cos_bet2;
        excess = CT(2) * static_cast<CT>(math_compat::atan2(
            static_cast<double>(sin_omg12 * (sin_bet1 * cos_bet2p1 + sin_bet2 * cos_bet1p1)),
            static_cast<double>(cos_omg12p1 * (sin_bet1 * sin_bet2 + cos_bet1p1 * cos_bet2p1))));
    }
    else
    {
        excess = alp2 - alp1;
    }

    result.spherical_term = excess;

    // Ellipsoidal term computation (uses integral approximation)

    CT cos_alp0 = bg::math::sqrt(CT(1) - bg::math::sqr(sin_alp0));
    CT cos_sig1 = cos_alp1 * cos_bet1;
    CT cos_sig2 = cos_alp2 * cos_bet2;
    CT sin_sig1 = sin_bet1;
    CT sin_sig2 = sin_bet2;

    base_type::normalize(sin_sig1, cos_sig1);
    base_type::normalize(sin_sig2, cos_sig2);

    CT coeffs[SeriesOrder + 1];
    const std::size_t coeffs_var_size = (series_order_plus_two
                                        * series_order_plus_one) / 2;
    CT coeffs_var[coeffs_var_size];

    if (ExpandEpsN) { // expand by eps and n

        CT k2 = bg::math::sqr(ep * cos_alp0);
        CT sqrt_k2_plus_one = bg::math::sqrt(CT(1) + k2);
        CT eps = (sqrt_k2_plus_one - CT(1)) / (sqrt_k2_plus_one + CT(1));
        CT n = f / (CT(2) - f);

        // Generate and evaluate the polynomials on n
        // to get the series coefficients (that depend on eps)
        base_type::evaluate_coeffs_n(n, coeffs_var);

        // Generate and evaluate the polynomials on eps (i.e. var2 = eps)
        // to get the final series coefficients
        base_type::evaluate_coeffs_var2(eps, coeffs_var, coeffs);

    } else { // expand by k2 and ep

        CT k2 = bg::math::sqr(ep * cos_alp0);
        CT ep2 = bg::math::sqr(ep);

        // Generate and evaluate the polynomials on ep2
        base_type::evaluate_coeffs_ep(ep2, coeffs_var);

        // Generate and evaluate the polynomials on k2 (i.e. var2 = k2)
        base_type::evaluate_coeffs_var2(k2, coeffs_var, coeffs);

    }

    // Evaluate the trigonometric sum
    CT I12 = base_type::clenshaw_sum(cos_sig2, coeffs, coeffs + series_order_plus_one)
           - base_type::clenshaw_sum(cos_sig1, coeffs, coeffs + series_order_plus_one);

    // The part of the ellipsodal correction that depends on
    // point coordinates
    result.ellipsoidal_term = cos_alp0 * sin_alp0 * I12;

    return result;
  }
};
} // namespace ob_geo_detail

/*!
\brief Geographic area calculation
\details Geographic area calculation by trapezoidal rule plus integral
         approximation that gives the ellipsoidal correction. Identical to
         boost::geometry::strategy::area::geographic except that the authalic
         radius and the Andoyer inverse cosine use the shared math layer.
*/
template
<
    typename FormulaPolicy = ob_geo_detail::ObAndoyerPolicy,
    std::size_t SeriesOrder = boost::geometry::strategy::default_order<boost::geometry::strategy::andoyer>::value,
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
      typedef ob_geo_detail::ObAreaFormulas
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
    typename FormulaPolicy = ob_geo_detail::ObAndoyerPolicy,
    std::size_t SeriesOrder = boost::geometry::strategy::default_order<boost::geometry::strategy::andoyer>::value,
    typename Spheroid = boost::geometry::srs::spheroid<double>,
    typename CalculationType = void
>
using ObGeoAreaStrategy = ObGeoAreaGeographic
    <FormulaPolicy, SeriesOrder, Spheroid, CalculationType>;



} // namespace common
} // namespace oceanbase
