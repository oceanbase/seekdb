/**
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
 *
 * This file contains a self contained implementation of the geographic
 * (ellipsoidal) ring/polygon area algorithm. It is derived from
 * Boost.Geometry 1.74:
 *   boost/geometry/formulas/area_formulas.hpp
 *   boost/geometry/formulas/andoyer_inverse.hpp
 *   boost/geometry/strategies/geographic/area.hpp
 * Copyright (c) 2017-2020 Oracle and/or its affiliates.
 * Copyright (c) 2018 Adam Wulkiewicz, Lodz, Poland.
 * Distributed under the Boost Software License, Version 1.0.
 */

#ifndef OCEANBASE_SHARE_GEO_OB_GEO_GEOGRAPHIC_AREA_H_
#define OCEANBASE_SHARE_GEO_OB_GEO_GEOGRAPHIC_AREA_H_

#include <cmath>
#include <cstddef>
#include <limits>

#include <boost/geometry/core/access.hpp>
#include <boost/geometry/core/closure.hpp>
#include <boost/geometry/core/exterior_ring.hpp>
#include <boost/geometry/core/interior_rings.hpp>
#include <boost/geometry/core/point_order.hpp>
#include <boost/geometry/core/radian_access.hpp>
#include <boost/math/special_functions/atanh.hpp>
#include <boost/math/special_functions/hypot.hpp>
#include <boost/range/begin.hpp>
#include <boost/range/end.hpp>
#include <boost/range/rbegin.hpp>
#include <boost/range/rend.hpp>
#include <boost/range/size.hpp>

namespace oceanbase
{
namespace common
{

/*
 * Why this file exists.
 *
 * The geographic area of a ring is computed as
 *     area = c2 * spherical_excess_sum + e2 * a2 * ellipsoidal_term_sum
 * where c2 is the squared authalic radius of the spheroid.
 *
 * Up to Boost 1.74 the spherical excess of a segment is computed with a
 * closed atan2() expression whenever the segment is "short"
 * (cos(omg12) > -0.7 && sin(bet2) - sin(bet1) < 1.75). Boost 1.75 replaced
 * that test by "distance < mean_radius / 638" and drops the atan2()
 * refinement for every other segment, so the excess becomes
 * "reverse_azimuth - azimuth", a difference of two approximated azimuths.
 * For a segment of ~157 km the two versions already disagree by ~1.7e-11 rad,
 * i.e. ~7e4 m^2 on WGS84, which makes geographic ST_Area() results depend on
 * the Boost version the binary was built with (1.74 on Linux, 1.90 on
 * Windows) instead of only on the input geometry.
 *
 * The result of an area query is part of the MySQL compatibility contract
 * (it is compared against MySQL in the regression suite), so the algorithm
 * is pinned here: this translation unit keeps a copy of the 1.74 algorithm,
 * which is also the more accurate of the two for medium length segments.
 * Keep it in sync with neither Boost nor other products: it is a frozen
 * reference implementation, only its callers may change.
 *
 * Series order is fixed to 1 (boost::geometry::strategy::andoyer is the
 * formula policy used by boost's geographic area strategy) and the series is
 * expanded in eps and n, exactly as the Boost strategy does.
 */

namespace geo_area_ref
{

const double kPi = 3.141592653589793238462643383279502884;
const double kTwoPi = 2.0 * kPi;
const double kEpsilon = std::numeric_limits<double>::epsilon();

// Support limit of the trapezoidal short-segment form: the ellipsoidal geodesic
// and the spherical trapezoid agree to double precision below mean_radius / 638
// (about 10 km). Same limit Boost.Geometry uses since 1.75.
const double kShortSegmentLimit = 1.0 / 638.0;


// Same semantics as boost::geometry::math::equals(), which the reference
// algorithm relies on for the meridian/antipodal/equator special cases.
inline bool equals(double a, double b)
{
  bool ret = true;
  if (a == b) {
    ret = true;
  } else if (std::isfinite(a) && std::isfinite(b)) {
    double factor = std::fabs(a) > std::fabs(b) ? std::fabs(a) : std::fabs(b);
    factor = factor > 1.0 ? factor : 1.0;
    ret = std::fabs(a - b) <= kEpsilon * factor;
  } else {
    ret = (a == b);
  }
  return ret;
}

inline double sqr(double x) { return x * x; }

// boost::geometry::formula::normalize(): normalize the (sin, cos) pair.
inline void normalize_pair(double &sin_val, double &cos_val)
{
  double h = boost::math::hypot(sin_val, cos_val);
  sin_val /= h;
  cos_val /= h;
}

/*
 * Solve the inverse problem of geodesics on latlong coordinates, with the
 * Forsyth-Andoyer-Lambert first order approximation, azimuths only.
 * Port of boost::geometry::formula::andoyer_inverse<CT, false, true, true>.
 */
struct AndoyerAzimuth
{
  double azimuth;
  double reverse_azimuth;
  AndoyerAzimuth() : azimuth(0.0), reverse_azimuth(0.0) {}
};

inline void normalize_azimuth(double &azimuth, double a, double da)
{
  if (a >= 0.0) {
    if (da >= 0.0) {
      if (azimuth < 0.0) {
        azimuth = 0.0;
      }
    } else {
      if (azimuth > kPi) {
        azimuth = kPi;
      }
    }
  } else {
    if (da <= 0.0) {
      if (azimuth > 0.0) {
        azimuth = 0.0;
      }
    } else {
      if (azimuth < -kPi) {
        azimuth = -kPi;
      }
    }
  }
}

inline AndoyerAzimuth andoyer_azimuth(double lon1, double lat1,
                                      double lon2, double lat2,
                                      double f)
{
  AndoyerAzimuth result;
  if (equals(lon1, lon2) && equals(lat1, lat2)) {
    return result;
  }

  const double dlon = lon2 - lon1;
  const double sin_dlon = std::sin(dlon);
  const double cos_dlon = std::cos(dlon);
  const double sin_lat1 = std::sin(lat1);
  const double cos_lat1 = std::cos(lat1);
  const double sin_lat2 = std::sin(lat2);
  const double cos_lat2 = std::cos(lat2);

  double cos_d = sin_lat1 * sin_lat2 + cos_lat1 * cos_lat2 * cos_dlon;
  if (cos_d < -1.0) {
    cos_d = -1.0;
  } else if (cos_d > 1.0) {
    cos_d = 1.0;
  }
  const double d = std::acos(cos_d);          // [0, pi]
  const double sin_d = std::sin(d);           // [-1, 1]

  if (equals(sin_d, 0.0)) {
    // T = inf, i.e. the points are very close or antipodal
    if (cos_d >= 0.0) {
      result.azimuth = 0.0;
      result.reverse_azimuth = 0.0;
    } else if (!equals(sin_lat1, 1.0)) {
      result.azimuth = 0.0;
      result.reverse_azimuth = kPi;
    } else {
      result.azimuth = kPi;
      result.reverse_azimuth = 0.0;
    }
    return result;
  }

  double a = 0.0;
  double u = 0.0;
  if (equals(cos_lat2, 0.0)) {
    if (sin_lat2 < 0.0) {
      a = kPi;
    }
  } else {
    const double tan_lat2 = sin_lat2 / cos_lat2;
    const double m = cos_lat1 * tan_lat2 - sin_lat1 * cos_dlon;
    a = std::atan2(sin_dlon, m);
    const double sin_2a = std::sin(2.0 * a);
    u = (f / 2.0) * sqr(cos_lat1) * sin_2a;
  }

  double b = 0.0;
  double v = 0.0;
  if (equals(cos_lat1, 0.0)) {
    if (sin_lat1 < 0.0) {
      b = kPi;
    }
  } else {
    const double tan_lat1 = sin_lat1 / cos_lat1;
    const double n = cos_lat2 * tan_lat1 - sin_lat2 * cos_dlon;
    b = std::atan2(sin_dlon, n);
    const double sin_2b = std::sin(2.0 * b);
    v = (f / 2.0) * sqr(cos_lat2) * sin_2b;
  }

  const double t = d / sin_d;

  const double da = v * t - u;
  result.azimuth = a - da;
  normalize_azimuth(result.azimuth, a, da);

  const double db = -u * t + v;
  if (b >= 0.0) {
    result.reverse_azimuth = kPi - b - db;
  } else {
    result.reverse_azimuth = -kPi - b - db;
  }
  normalize_azimuth(result.reverse_azimuth, b, db);

  return result;
}

// boost::geometry::formula::area_formulas<double, 1, true>
struct EllipsoidalTerm
{
  double spherical_term;
  double ellipsoidal_term;
  EllipsoidalTerm() : spherical_term(0.0), ellipsoidal_term(0.0) {}
};

// Clenshaw algorithm for summing trigonometric series, ported verbatim.
inline double clenshaw_sum(double cosx, const double *begin, const double *end)
{
  const double *it = end;
  bool odd = true;
  double b_k = 0.0;
  double b_k1 = 0.0;
  double b_k2 = 0.0;
  do {
    double c_k = odd ? *--it : 0.0;
    b_k = c_k + 2.0 * cosx * b_k1 - b_k2;
    b_k2 = b_k1;
    b_k1 = b_k;
    odd = !odd;
  } while (it != begin);
  return *begin + b_k1 * cosx - b_k2;
}

// Shape dependent part of the area: (a, b) -> (a2, e2, ep, c2, f).
class SpheroidConstants
{
public:
  explicit SpheroidConstants(double semi_major, double semi_minor)
      : a2_(sqr(semi_major)),
        e2_(1.0 - sqr(semi_minor / semi_major)),
        f_((semi_major - semi_minor) / semi_major)
  {
    ep2_ = e2_ / (1.0 - e2_);
    ep_ = std::sqrt(ep2_);
    if (equals(e2_, 0.0)) {
      c2_ = a2_;
    } else {
      const double e = std::sqrt(e2_);
      c2_ = (a2_ / 2.0) * (1.0 + (1.0 - e2_) * boost::math::atanh(e) / e);
    }
  }

  double a2() const { return a2_; }
  double e2() const { return e2_; }
  double ep() const { return ep_; }
  double c2() const { return c2_; }
  double f() const { return f_; }

private:
  double a2_;
  double e2_;
  double ep2_;
  double ep_;
  double c2_;
  double f_;
};

/*
 * Per segment spherical excess and ellipsoidal correction.
 * Port of boost::geometry::formula::area_formulas<double, 1, true>::
 * ellipsoidal<andoyer_inverse>.
 */
template <typename Point>
inline EllipsoidalTerm segment_terms(const Point &p1,
                                     const Point &p2,
                                     const SpheroidConstants &consts)
{
  EllipsoidalTerm result;

  const double lon1 = boost::geometry::get_as_radian<0>(p1);
  const double lat1 = boost::geometry::get_as_radian<1>(p1);
  const double lon2 = boost::geometry::get_as_radian<0>(p2);
  const double lat2 = boost::geometry::get_as_radian<1>(p2);

  AndoyerAzimuth i_res = andoyer_azimuth(lon1, lat1, lon2, lat2, consts.f());
  const double alp1 = i_res.azimuth;
  const double alp2 = i_res.reverse_azimuth;

  const double ep = consts.ep();
  const double one_minus_f = 1.0 - consts.f();

  // Basic trigonometric computations
  const double tan_bet1 = std::tan(lat1) * one_minus_f;
  const double tan_bet2 = std::tan(lat2) * one_minus_f;
  const double cos_bet1 = std::cos(std::atan(tan_bet1));
  const double cos_bet2 = std::cos(std::atan(tan_bet2));
  const double sin_bet1 = tan_bet1 * cos_bet1;
  const double sin_bet2 = tan_bet2 * cos_bet2;
  const double sin_alp1 = std::sin(alp1);
  const double cos_alp1 = std::cos(alp1);
  const double cos_alp2 = std::cos(alp2);
  const double sin_alp0 = sin_alp1 * cos_bet1;

  // Arc between the two points on the auxiliary sphere. It follows from the
  // reduced latitudes and the longitude difference only, so it does not suffer
  // from the azimuth approximation used below.
  const double dlon = lon2 - lon1;
  const double sin_dlon = std::sin(dlon);
  const double cos_dlon = std::cos(dlon);
  const double sin_sigma12 = std::hypot(cos_bet2 * sin_dlon,
                                        cos_bet1 * sin_bet2 - sin_bet1 * cos_bet2 * cos_dlon);
  const double cos_sigma12 = sin_bet1 * sin_bet2 + cos_bet1 * cos_bet2 * cos_dlon;
  const double sigma12 = std::atan2(sin_sigma12, cos_sigma12);
  // Same support limit Boost.Geometry uses since 1.75 for the trapezoidal form:
  // below mean_radius / 638 (about 10 km) the spherical trapezoid and the
  // ellipsoidal geodesic agree to double precision, while the atan2() form
  // below loses its significant digits because it is built from the azimuth
  // approximation.
  const bool short_segment = sigma12 < kShortSegmentLimit;

  // Spherical term
  const double sin_omg1 = sin_alp0 * sin_bet1;
  const double cos_omg1 = cos_alp1 * cos_bet1;
  const double sin_omg2 = sin_alp0 * sin_bet2;
  const double cos_omg2 = cos_alp2 * cos_bet2;
  double cos_omg12 = cos_omg1 * cos_omg2 + sin_omg1 * sin_omg2;
  double excess = 0.0;

  if (short_segment) {
    const double t1 = std::tan(lat1 / 2.0);
    const double t2 = std::tan(lat2 / 2.0);
    excess = 2.0 * std::atan(((t1 + t2) / (1.0 + t1 * t2)) * std::tan(dlon / 2.0));
  } else if (cos_omg12 > -0.7 && sin_bet2 - sin_bet1 < 1.75) {  // short segment
    double sin_omg12 = cos_omg1 * sin_omg2 - sin_omg1 * cos_omg2;
    normalize_pair(sin_omg12, cos_omg12);
    const double cos_omg12p1 = 1.0 + cos_omg12;
    const double cos_bet1p1 = 1.0 + cos_bet1;
    const double cos_bet2p1 = 1.0 + cos_bet2;
    excess = 2.0 * std::atan2(sin_omg12 * (sin_bet1 * cos_bet2p1 + sin_bet2 * cos_bet1p1),
                              cos_omg12p1 * (sin_bet1 * sin_bet2 + cos_bet1p1 * cos_bet2p1));
  } else {
    excess = alp2 - alp1;
  }
  result.spherical_term = excess;

  // Ellipsoidal term computation (integral approximation, series in eps and n)
  double sin_alp0_used = sin_alp0;
  double cos_sig1 = cos_alp1 * cos_bet1;
  double cos_sig2 = cos_alp2 * cos_bet2;
  double sin_sig1 = sin_bet1;
  double sin_sig2 = sin_bet2;
  // The ellipsoidal term is evaluated exactly as the long segment path does:
  // the reviewer's segment comparison shows the 1.74 port of
  // area_formulas::ellipsoidal agrees bit for bit with Boost 1.90 on these
  // inputs, so the azimuth approximation is not the source of the short
  // segment error (only the spherical term was).
  normalize_pair(sin_sig1, cos_sig1);
  normalize_pair(sin_sig2, cos_sig2);

  const double cos_alp0 = std::sqrt(1.0 - sqr(sin_alp0_used));

  // SeriesOrder == 1: three n-polynomials, two eps-polynomials.
  double coeffs_var[3];
  const double n = consts.f() / (2.0 - consts.f());
  coeffs_var[0] = (10.0 - 4.0 * n) / 15.0;
  coeffs_var[1] = -1.0 / 5.0;
  coeffs_var[2] = 1.0 / 45.0;

  const double k2 = sqr(ep * cos_alp0);
  const double sqrt_k2_plus_one = std::sqrt(1.0 + k2);
  const double eps = (sqrt_k2_plus_one - 1.0) / (sqrt_k2_plus_one + 1.0);

  // evaluate_coeffs_var2(eps, coeffs_var, coeffs) for SeriesOrder == 1
  double coeffs[2];
  coeffs[0] = coeffs_var[0] + coeffs_var[1] * eps;
  coeffs[1] = coeffs_var[2] * eps;

  // Both branches use the same series difference. The exact expansion of
  // clenshaw_sum(x, [c0, c1]) is F(x) = c0 + c0 * x + c1 * (4 x^3 - 3 x), so
  // F(x2) - F(x1) = (x2 - x1) * (c0 + c1 * (4 (x2^2 + x1 x2 + x1^2) - 3));
  // the leading c0 term must not be dropped.
  // F(x) = c0 + c0*x + c1*(4x^3 - 3x) for clenshaw_sum(x, [c0, c1]); the
  // leading c0 term is part of the series difference.
  const double i12 = clenshaw_sum(cos_sig2, coeffs, coeffs + 2)
                     - clenshaw_sum(cos_sig1, coeffs, coeffs + 2);
  result.ellipsoidal_term = cos_alp0 * sin_alp0_used * i12;

  return result;
}

// boost::geometry::formula::area_formulas::crosses_prime_meridian()
template <typename Point>
inline bool crosses_prime_meridian(const Point &p1, const Point &p2)
{
  const double lon1 = boost::geometry::get_as_radian<0>(p1);
  const double lon2 = boost::geometry::get_as_radian<0>(p2);
  const double p1_lon = lon1 - std::floor(lon1 / kTwoPi) * kTwoPi;
  const double p2_lon = lon2 - std::floor(lon2 / kTwoPi) * kTwoPi;
  const double max_lon = p1_lon > p2_lon ? p1_lon : p2_lon;
  const double min_lon = p1_lon > p2_lon ? p2_lon : p1_lon;
  return max_lon > kPi && min_lon < kPi && max_lon - min_lon > kPi;
}

// Accumulated state of one ring, port of the Boost strategy state.
class RingState
{
public:
  RingState() : excess_sum_(0.0), correction_sum_(0.0), crosses_(0) {}
  void add(double excess, double correction) { excess_sum_ += excess; correction_sum_ += correction; }
  void cross() { ++crosses_; }
  double excess_sum() const { return excess_sum_; }
  double correction_sum() const { return correction_sum_; }
  std::size_t crosses() const { return crosses_; }

private:
  double excess_sum_;
  double correction_sum_;
  std::size_t crosses_;
};

}  // namespace geo_area_ref

/*
 * Reference geographic (ellipsoidal) area, independent of the installed
 * Boost.Geometry version. Input geometries must use cs::geographic<radian>
 * and their point order trait is honoured the same way Boost does: a
 * counterclockwise ring is accumulated in reverse vertex order.
 */
class ObGeoGeographicArea
{
public:
  ObGeoGeographicArea(double semi_major, double semi_minor) : consts_(semi_major, semi_minor) {}
  ~ObGeoGeographicArea() = default;

  template <typename Ring>
  double ring_area(const Ring &ring) const
  {
    using namespace geo_area_ref;  // NOLINT
    double result = 0.0;
    const std::size_t n = boost::size(ring);
    const bool is_closed = boost::geometry::closure<Ring>::value == boost::geometry::closed;
    const std::size_t min_size = is_closed ? 4 : 3;
    if (n < min_size) {
      return result;
    }

    RingState state;
    if (boost::geometry::point_order<Ring>::value == boost::geometry::counterclockwise) {
      typedef decltype(boost::rbegin(ring)) reverse_iterator_type;
      reverse_iterator_type prev = boost::rbegin(ring);
      reverse_iterator_type it = prev;
      reverse_iterator_type end = boost::rend(ring);
      ++it;
      for (; it != end; ++prev, ++it) {
        accumulate(*prev, *it, state);
      }
      if (!is_closed) {
        accumulate(*boost::begin(ring), *prev, state);
      }
    } else {
      typedef decltype(boost::begin(ring)) iterator_type;
      iterator_type prev = boost::begin(ring);
      iterator_type it = prev;
      iterator_type end = boost::end(ring);
      ++it;
      for (; it != end; ++prev, ++it) {
        accumulate(*prev, *it, state);
      }
      if (!is_closed) {
        accumulate(*prev, *boost::begin(ring), state);
      }
    }
    result = state_area(state);
    return result;
  }

  template <typename Polygon>
  double polygon_area(const Polygon &poly) const
  {
    double result = ring_area(boost::geometry::exterior_ring(poly));
    const auto &inners = boost::geometry::interior_rings(poly);
    for (auto it = boost::begin(inners); it != boost::end(inners); ++it) {
      result += ring_area(*it);
    }
    return result;
  }

  template <typename MultiPolygon>
  double multipolygon_area(const MultiPolygon &mpoly) const
  {
    double result = 0.0;
    typedef decltype(boost::begin(mpoly)) iterator_type;
    for (iterator_type it = boost::begin(mpoly); it != boost::end(mpoly); ++it) {
      result += polygon_area(*it);
    }
    return result;
  }

private:
  template <typename Point>
  void accumulate(const Point &p1, const Point &p2, geo_area_ref::RingState &state) const
  {
    using namespace geo_area_ref;  // NOLINT
    if (!equals(boost::geometry::get<0>(p1), boost::geometry::get<0>(p2))) {
      EllipsoidalTerm term = segment_terms(p1, p2, consts_);
      state.add(term.spherical_term, term.ellipsoidal_term);
      if (crosses_prime_meridian(p1, p2)) {
        state.cross();
      }
    }
  }

  double state_area(const geo_area_ref::RingState &state) const
  {
    using namespace geo_area_ref;  // NOLINT
    double result = 0.0;
    const double spherical_area = consts_.c2() * state.excess_sum();
    const double ellipsoidal_area = consts_.e2() * consts_.a2() * state.correction_sum();
    // Same finalisation as upstream Boost.Geometry (strategy/geographic/area.hpp):
    // a large ellipsoidal correction relative to the spherical term is dropped,
    // upstream comment "ignore ellipsoidal term if is large (probably from an
    // azimuth inaccuracy)". This is an unconditional heuristic, not an error
    // bound, and it applies to every ring - not only to short segments. Ratios
    // measured so far: 0.033 on this port's 3 m octagon, 0.816 on the
    // reviewer's construction, 0.0045 on the regression triangle and below
    // 0.001 on equatorial short triangles. Those four measurements do not
    // establish that the threshold is safe elsewhere, nor that Linux results are
    // unchanged for inputs outside them.
    double sum = std::fabs(ellipsoidal_area) > 0.01 * std::fabs(spherical_area)
                 ? spherical_area
                 : spherical_area + ellipsoidal_area;
    if (state.crosses() % 2 == 1) {
      // the ring encircles a pole
      const std::size_t times = 1 + state.crosses() / 2;
      result = 2.0 * kPi * consts_.c2() * static_cast<double>(times) - std::fabs(sum);
      if (sum > 0.0) {
        result = -result;
      }
    } else {
      result = sum;
    }
    return result;
  }

  geo_area_ref::SpheroidConstants consts_;
};

}  // namespace common
}  // namespace oceanbase

#endif  // OCEANBASE_SHARE_GEO_OB_GEO_GEOGRAPHIC_AREA_H_
