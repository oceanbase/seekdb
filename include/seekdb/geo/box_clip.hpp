/*
 * Copyright (c) 2026 OceanBase.
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

#pragma once

#include <boost/geometry/core/access.hpp>
#include <boost/geometry/core/exterior_ring.hpp>
#include <boost/geometry/core/interior_rings.hpp>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <type_traits>
#include <utility>

namespace seekdb {
namespace geo {
namespace cartesian {

enum BoxPosition {
  BOX_INVALID = 0, BOX_INSIDE = 1, BOX_OUTSIDE = 2,
  BOX_LEFT = 4, BOX_RIGHT = 8, BOX_TOP = 16, BOX_BOTTOM = 32
};

struct ClipBox {
  double xmin, ymin, xmax, ymax;

  int position(double x, double y) const
  {
    if (!std::isfinite(x) || !std::isfinite(y)) return BOX_INVALID;
    if (x > xmin && x < xmax && y > ymin && y < ymax) return BOX_INSIDE;
    if (x < xmin || x > xmax || y < ymin || y > ymax) return BOX_OUTSIDE;
    return (x == xmin ? BOX_LEFT : x == xmax ? BOX_RIGHT : 0) |
           (y == ymax ? BOX_TOP : y == ymin ? BOX_BOTTOM : 0);
  }
};

// Extracted from ObGeoBoxClipVisitor, including its strict-edge semantics and
// clockwise reconnection of clipped rings. Factory binds owning containers,
// allocation errors and the existing Within/CoveredBy predicates. The core
// uses its arena models; the plugin uses private STL/Boost models.
template <typename Factory>
class BoxClipper {
  using Point = typename Factory::Point;
  using Line = typename Factory::Line;
  using Ring = typename Factory::Ring;
  using Polygon = typename Factory::Polygon;
  using Lines = typename Factory::Lines;
  using Polygons = typename Factory::Polygons;
  ClipBox box_;
  Factory &factory_;

  template <typename P> static double x(const P &p) { return boost::geometry::get<0>(p); }
  template <typename P> static double y(const P &p) { return boost::geometry::get<1>(p); }
  template <typename P, typename Q> static bool equal(const P &p, const Q &q)
  { return x(p) == x(q) && y(p) == y(q); }
  template <typename Container, typename Value>
  static int append(Container &container, const Value &value)
  {
    if constexpr (std::is_void_v<decltype(container.push_back(value))>) {
      container.push_back(value);
      return 0;
    } else {
      return container.push_back(value);
    }
  }
  static bool same_edge(int a, int b) { return (a & b) > BOX_OUTSIDE; }
  static void edge(Point &out, const Point &in, double value, bool vertical)
  {
    const double o = vertical ? x(out) : y(out), i = vertical ? x(in) : y(in);
    if (i == value) out = in;
    else if (i != o) {
      if (vertical) {
        boost::geometry::set<1>(out, y(out) + (y(in) - y(out)) * (value - o) / (i - o));
        boost::geometry::set<0>(out, value);
      } else {
        boost::geometry::set<0>(out, x(out) + (x(in) - x(out)) * (value - o) / (i - o));
        boost::geometry::set<1>(out, value);
      }
    }
  }
  bool edges(const Point &out, const Point &in, Point &point) const
  {
    point = out;
    if (x(point) < box_.xmin) edge(point, in, box_.xmin, true);
    else if (x(point) > box_.xmax) edge(point, in, box_.xmax, true);
    if (y(point) < box_.ymin) edge(point, in, box_.ymin, false);
    else if (y(point) > box_.ymax) edge(point, in, box_.ymax, false);
    return std::isfinite(x(point)) && std::isfinite(y(point));
  }
  template <typename Input, typename Output>
  static int copy_range(const Input &input, size_t begin, size_t end, Output &output)
  {
    int status = 0;
    for (size_t i = begin; status == 0 && i < end; ++i) status = append(output, input[i]);
    return status;
  }
  template <typename Input>
  int inside(const Input &line, size_t &index, Lines &output, int pos, Line &part, bool &all_inside)
  {
    int status = 0;
    size_t first = index;
    int previous = BOX_INVALID;
    for (++index; status == 0 && pos != BOX_OUTSIDE && index < line.size(); ++index) {
      previous = pos;
      pos = box_.position(x(line[index]), y(line[index]));
      if (pos == BOX_INVALID) return Factory::invalid_geometry;
      if (pos == BOX_OUTSIDE) {
        Point clipped;
        if (!edges(line[index], line[index - 1], clipped)) return Factory::invalid_geometry;
        const bool crossing = !equal(clipped, line[index]) &&
            !same_edge(box_.position(x(clipped), y(clipped)), previous);
        if (first < index - 1 || !part.empty() || crossing) {
          status = copy_range(line, first, index, part);
          if (status == 0 && crossing) status = append(part, clipped);
          if (status == 0) status = append(output, part);
          if (status == 0) part.clear();
        }
      } else if (pos != BOX_INSIDE && same_edge(previous, pos)) {
        if (first < index - 1 || !part.empty()) {
          status = copy_range(line, first, index, part);
          if (status == 0) status = append(output, part);
          if (status == 0) part.clear();
        }
        first = index;
      }
    }
    if (pos == BOX_OUTSIDE) --index;
    if (status == 0 && first == 0 && index >= line.size()) {
      all_inside = true;
      Line whole = factory_.line();
      status = copy_range(line, 0, line.size(), whole);
      if (status == 0) status = append(output, whole);
    } else if (status == 0 && pos != BOX_OUTSIDE && (first < index - 1 || !part.empty())) {
      status = copy_range(line, first, index, part);
      if (status == 0) status = append(output, part);
      if (status == 0) part.clear();
    }
    return status;
  }
  template <typename Input>
  int outside(const Input &line, size_t &index, Lines &output, Line &part)
  {
    const double px = x(line[index]), py = y(line[index]);
    ++index;
    if (px < box_.xmin) while (index < line.size() && x(line[index]) < box_.xmin) ++index;
    else if (px > box_.xmax) while (index < line.size() && x(line[index]) > box_.xmax) ++index;
    else if (py < box_.ymin) while (index < line.size() && y(line[index]) < box_.ymin) ++index;
    else if (py > box_.ymax) while (index < line.size() && y(line[index]) > box_.ymax) ++index;
    int status = 0;
    if (index < line.size()) {
      int pos = box_.position(x(line[index]), y(line[index]));
      if (pos == BOX_INVALID) return Factory::invalid_geometry;
      Point point;
      if (!edges(line[index - 1], line[index], point)) return Factory::invalid_geometry;
      if (pos == BOX_INSIDE) status = append(part, point);
      else if (pos == BOX_OUTSIDE) {
        Point other;
        if (!edges(line[index], point, other)) return Factory::invalid_geometry;
        const int previous = box_.position(x(point), y(point));
        pos = box_.position(x(other), y(other));
        if (!equal(point, other) && previous > BOX_OUTSIDE && pos > BOX_OUTSIDE && !same_edge(previous, pos)) {
          status = append(part, point);
          if (status == 0) status = append(part, other);
          if (status == 0) status = append(output, part);
          if (status == 0) part.clear();
        }
      } else if (!same_edge(box_.position(x(point), y(point)), pos)) status = append(part, point);
    }
    return status;
  }
  int reconnect(Lines &lines)
  {
    if (lines.size() < 2) return 0;
    auto &first = lines.front();
    auto &last = lines[lines.size() - 1];
    if (first.empty() || last.empty() || !equal(first.front(), last.back())) return 0;
    Point previous = last.back();
    for (size_t i = 0; i < first.size(); ++i) {
      if (!equal(first[i], previous)) {
        previous = first[i];
        const int status = append(last, previous);
        if (status != 0) return status;
      }
    }
    lines.front() = last;
    return factory_.erase(lines, lines.size() - 1);
  }
  static int next_edge(int pos)
  {
    switch (pos) {
      case BOX_LEFT: case BOX_BOTTOM | BOX_LEFT: return BOX_TOP;
      case BOX_TOP: case BOX_TOP | BOX_LEFT: return BOX_RIGHT;
      case BOX_RIGHT: case BOX_TOP | BOX_RIGHT: return BOX_BOTTOM;
      case BOX_BOTTOM: case BOX_BOTTOM | BOX_RIGHT: return BOX_LEFT;
      default: return BOX_INVALID;
    }
  }
  bool clockwise_end(int pos, int end, double x1, double y1, double x2, double y2) const
  {
    return (pos & end) && ((x1 == box_.xmin && y2 >= y1) || (y1 == box_.ymax && x2 >= x1) ||
                          (x1 == box_.xmax && y2 <= y1) || (y1 == box_.ymin && x2 <= x1));
  }
  // At most one circuit of the rectangle. The original algorithm terminates
  // on valid boundary points; the bound also makes malformed inputs fail.
  template <typename Emit>
  int walk_boundary(double x1, double y1, double x2, double y2, double &distance, Emit emit) const
  {
    int pos = box_.position(x1, y1), end = box_.position(x2, y2);
    for (int step = 0; step <= 4; ++step) {
      if (pos <= BOX_OUTSIDE || end <= BOX_OUTSIDE) return Factory::invalid_argument;
      if (clockwise_end(pos, end, x1, y1, x2, y2)) {
        distance += std::fabs(x1 - x2) + std::fabs(y1 - y2);
        if (!std::isfinite(distance)) return Factory::invalid_geometry;
        return x1 == x2 && y1 == y2 ? 0 : emit(x2, y2);
      }
      pos = next_edge(pos);
      if (pos == BOX_LEFT) { distance += x1 - box_.xmin; x1 = box_.xmin; }
      else if (pos == BOX_TOP) { distance += box_.ymax - y1; y1 = box_.ymax; }
      else if (pos == BOX_RIGHT) { distance += box_.xmax - x1; x1 = box_.xmax; }
      else if (pos == BOX_BOTTOM) { distance += y1 - box_.ymin; y1 = box_.ymin; }
      else return Factory::invalid_argument;
      const int status = emit(x1, y1);
      if (status != 0) return status;
    }
    return Factory::invalid_argument;
  }
  int boundary_distance(const Point &from, const Point &to, double &distance) const
  { return walk_boundary(x(from), y(from), x(to), y(to), distance, [](double, double) { return 0; }); }
  int close(Ring &ring, const Point &to)
  {
    // The target may refer into ring, so copy before push_back can reallocate.
    const double x2 = x(to), y2 = y(to);
    double unused = 0;
    return walk_boundary(x(ring.back()), y(ring.back()), x2, y2, unused,
                         [&](double px, double py) { return append(ring, Point(px, py)); });
  }
  template <typename Input>
  static void reverse(Input &line, size_t first, size_t last)
  { for (; first < last; ++first, --last) { auto point = line[first]; line[first] = line[last]; line[last] = point; } }
  static void reorder(Ring &ring)
  {
    if (ring.size() < 2) return;
    size_t minimum = 0;
    for (size_t i = 1; i < ring.size(); ++i) {
      if (x(ring[i]) < x(ring[minimum]) || (x(ring[i]) == x(ring[minimum]) && y(ring[i]) < y(ring[minimum]))) minimum = i;
    }
    if (minimum != 0) {
      reverse(ring, 0, minimum - 1);
      reverse(ring, minimum, ring.size() - 2);
      reverse(ring, 0, ring.size() - 2);
      ring.back() = ring.front();
    }
  }
  static void reverse_lines(Lines &lines)
  {
    if (lines.empty()) return;
    for (size_t i = 0; i < lines.size(); ++i) if (!lines[i].empty()) reverse(lines[i], 0, lines[i].size() - 1);
    for (size_t i = 0, j = lines.size() - 1; i < j; ++i, --j) { auto line = lines[i]; lines[i] = lines[j]; lines[j] = line; }
  }
  template <typename Input>
  static bool ccw(const Input &ring)
  {
    if (ring.size() < 4) return false;
    size_t high = 0;
    double previous = y(ring[0]);
    for (size_t i = 1; i < ring.size(); ++i) {
      const double current = y(ring[i]);
      if (current > previous && current >= y(ring[high])) high = i;
      previous = current;
    }
    if (high == 0) return false;
    const size_t size = ring.size() - 1;
    size_t low = (high + 1) % size;
    while (low != high && y(ring[low]) == y(ring[high])) low = (low + 1) % size;
    const size_t down_high = low > 0 ? low - 1 : size - 1;
    if (!equal(ring[high], ring[down_high])) return x(ring[down_high]) - x(ring[high]) < 0;
    const auto &up = ring[high - 1], &top = ring[high], &down = ring[low];
    return !equal(up, top) && !equal(down, top) && !equal(up, down) &&
           (x(top) - x(up)) * (y(down) - y(top)) - (x(down) - x(top)) * (y(top) - y(up)) > 0;
  }
  int make_shells(Lines &lines, Polygons &output)
  {
    int status = 0;
    if (lines.empty()) {
      Polygon polygon = factory_.polygon();
      auto &ring = boost::geometry::exterior_ring(polygon);
      for (const auto &point : {Point(box_.xmin, box_.ymin), Point(box_.xmin, box_.ymax),
                               Point(box_.xmax, box_.ymax), Point(box_.xmax, box_.ymin), Point(box_.xmin, box_.ymin)}) {
        if ((status = append(ring, point)) != 0) return status;
      }
      return append(output, polygon);
    }
    Ring ring = factory_.ring();
    while (status == 0 && (!lines.empty() || !ring.empty())) {
      if (ring.empty()) {
        status = copy_range(lines[0], 0, lines[0].size(), ring);
        if (status == 0) status = factory_.erase(lines, 0);
      }
      if (status != 0) return status;
      if (ring.empty()) return Factory::invalid_geometry;
      double distance = 0, minimum = -1;
      size_t nearest = 0;
      if ((status = boundary_distance(ring.back(), ring.front(), distance)) != 0) return status;
      for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].empty()) return Factory::invalid_geometry;
        double current = 0;
        if ((status = boundary_distance(ring.back(), lines[i].front(), current)) != 0) return status;
        if (minimum < 0 || current < minimum) { minimum = current; nearest = i; }
      }
      if (minimum < 0 || distance < minimum) {
        if ((status = close(ring, ring.front())) != 0) return status;
        reorder(ring);
        if (ring.size() < 4) return Factory::invalid_geometry;
        Polygon polygon = factory_.polygon();
        boost::geometry::exterior_ring(polygon) = ring;
        status = append(output, polygon);
        ring.clear();
      } else {
        status = close(ring, lines[nearest].front());
        if (status == 0) status = copy_range(lines[nearest], 1, lines[nearest].size(), ring);
        if (status == 0) status = factory_.erase(lines, nearest);
      }
    }
    return status;
  }

public:
  BoxClipper(ClipBox box, Factory &factory) : box_(box), factory_(factory) {}

  template <typename Input>
  int clip_line(const Input &line, Lines &output, bool &all_inside)
  {
    all_inside = false;
    Line part = factory_.line();
    size_t index = 0;
    while (index < line.size()) {
      const int pos = box_.position(x(line[index]), y(line[index]));
      if (pos == BOX_INVALID) return Factory::invalid_geometry;
      const int status = pos == BOX_OUTSIDE ? outside(line, index, output, part)
          : inside(line, index, output, pos, part, all_inside);
      if (status != 0) return status;
    }
    return 0;
  }

  int clip_polygon(const Polygon &polygon, Polygons &output)
  {
    const auto &outer = boost::geometry::exterior_ring(polygon);
    if (outer.empty()) return 0;
    Lines fragments = factory_.lines();
    bool all_inside = false, midpoint_inside = true;
    int status = clip_line(outer, fragments, all_inside);
    const Point midpoint(box_.xmin + (box_.xmax - box_.xmin) / 2, box_.ymin + (box_.ymax - box_.ymin) / 2);
    if (status != 0) return status;
    if (fragments.empty()) {
      Polygon shell = factory_.polygon();
      boost::geometry::exterior_ring(shell) = outer;
      if ((status = factory_.within(midpoint, shell, midpoint_inside)) != 0) return status;
    } else if (ccw(outer)) reverse_lines(fragments);
    if (!midpoint_inside) return 0;
    if (all_inside) return append(output, polygon);
    if ((status = reconnect(fragments)) != 0) return status;
    Polygons holes = factory_.polygons();
    const auto &inners = boost::geometry::interior_rings(polygon);
    for (size_t i = 0; i < inners.size(); ++i) {
      Lines inner_lines = factory_.lines();
      Polygon hole = factory_.polygon();
      boost::geometry::exterior_ring(hole) = inners[i];
      bool inside = false;
      if ((status = clip_line(inners[i], inner_lines, inside)) != 0) return status;
      if (inside) status = append(holes, hole);
      else if (inner_lines.empty()) {
        bool within = false;
        if ((status = factory_.within(midpoint, hole, within)) != 0) return status;
        if (within) return 0;
      } else {
        if (!ccw(inners[i])) reverse_lines(inner_lines);
        status = reconnect(inner_lines);
        if (status == 0) status = copy_range(inner_lines, 0, inner_lines.size(), fragments);
      }
      if (status != 0) return status;
    }
    Polygons shells = factory_.polygons();
    if ((status = make_shells(fragments, shells)) != 0) return status;
    for (size_t i = 0; i < holes.size(); ++i) {
      const auto &ring = boost::geometry::exterior_ring(holes[i]);
      for (size_t j = 0; j < shells.size(); ++j) {
        bool covered = shells.size() == 1;
        if (!covered && (status = factory_.covered_by(ring, shells[j], covered)) != 0) return status;
        if (covered && (status = append(boost::geometry::interior_rings(shells[j]), ring)) != 0) return status;
      }
    }
    return copy_range(shells, 0, shells.size(), output);
  }
};

} // namespace cartesian
} // namespace geo
} // namespace seekdb
