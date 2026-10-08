
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

// Port of ObWktParser's grammar over plugin-owned Geometry values. Numeric
// conversion shares the original bounded dtoa/strtod engine with the kernel.
class WktParser {
public:
  explicit WktParser(const char *data, size_t size)
      : input_(data, size), current_(input_.c_str()), end_(current_ + size) {}

  bool parse(Geometry &geometry, uint32_t srid)
  {
    if (!parse_geometry(geometry, srid)) return false;
    skip_space();
    return current_ == end_;
  }

private:
  static bool space(unsigned char c)
  { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
  static bool alpha(unsigned char c)
  { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
  static bool number_start(char c)
  { return (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'; }
  void skip_space() { while (current_ < end_ && space(*current_)) ++current_; }
  char peek() { skip_space(); return current_ == end_ ? '\0' : *current_; }
  bool consume(char expected)
  {
    if (peek() != expected || current_ == end_) return false;
    ++current_;
    return true;
  }
  bool word(std::string &out)
  {
    skip_space();
    const char *begin = current_;
    while (current_ < end_ && alpha(*current_)) ++current_;
    if (begin == current_) return false;
    out.assign(begin, current_);
    for (char &c : out) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    return true;
  }
  bool number(double &out)
  {
    skip_space();
    if (current_ == end_ || !number_start(*current_)) return false;
    const char *next = current_;
    if (!seekdb_gis::parse_wkt_number(current_, end_, out, next)) return false;
    current_ = next;
    return true;
  }
  bool set_dimensions(uint32_t value)
  {
    if (dimensions_ != 0 && dimensions_ != value) return false;
    dimensions_ = value;
    return true;
  }
  bool point(Point &point)
  {
    if (!number(point.x) || !number(point.y)) return false;
    const char next = peek();
    if (number_start(next)) return set_dimensions(3) && number(point.z);
    return (next == ')' || next == ',') && set_dimensions(2);
  }
  bool point_list(std::vector<Point> &points, bool ring = false)
  {
    if (!consume('(')) return false;
    do {
      Point value;
      if (!point(value)) return false;
      points.push_back(value);
    } while (consume(','));
    if (!consume(')') || points.size() < (ring ? 4u : 2u)) return false;
    // The original parser compares encoded X/Y bits, including signed zero.
    // Ring closure deliberately ignores Z.
    return !ring || (std::memcmp(&points.front().x, &points.back().x, sizeof(double)) == 0 &&
                     std::memcmp(&points.front().y, &points.back().y, sizeof(double)) == 0);
  }
  bool polygon(Geometry &geometry)
  {
    if (!consume('(')) return false;
    do {
      std::vector<Point> ring;
      if (!point_list(ring, true)) return false;
      geometry.rings.push_back(std::move(ring));
    } while (consume(','));
    return consume(')');
  }
  bool parse_geometry(Geometry &geometry, uint32_t srid, unsigned depth = 0)
  {
    if (depth > 64) return false;
    std::string name;
    if (!word(name)) return false;
    // Both POINTZ and POINT Z are accepted; a duplicate Z token is not.
    const bool joined_z = !name.empty() && name.back() == 'Z';
    if (joined_z) {
      name.pop_back();
      if (!set_dimensions(3)) return false;
    } else {
      skip_space();
      const char *saved = current_;
      std::string dimension;
      if (word(dimension) && dimension == "Z") {
        if (!set_dimensions(3)) return false;
      } else current_ = saved;
    }
    uint32_t type = 0;
    if (name == "POINT") type = 1;
    else if (name == "LINESTRING") type = 2;
    else if (name == "POLYGON") type = 3;
    else if (name == "MULTIPOINT") type = 4;
    else if (name == "MULTILINESTRING") type = 5;
    else if (name == "MULTIPOLYGON") type = 6;
    else if (name == "GEOMETRYCOLLECTION") type = 7;
    else return false;
    geometry.type = type;
    geometry.srid = srid;
    if (type == 1) {
      Point value;
      if (!consume('(') || !point(value) || !consume(')')) return false;
      geometry.points.push_back(value);
    } else if (type == 2) {
      if (!point_list(geometry.points)) return false;
    } else if (type == 3) {
      if (!polygon(geometry)) return false;
    } else {
      bool empty = false;
      if (type == 7) {
        std::string marker;
        if (word(marker)) {
          if (marker != "EMPTY") return false;
          empty = true;
        }
      }
      if (!empty) {
        if (!consume('(')) return false;
        if (type == 7 && consume(')')) empty = true;
        if (!empty) {
          // ObWktParser chooses one bracket form for the entire MultiPoint.
          const bool point_brackets = type == 4 && peek() == '(';
          do {
            Geometry child;
            child.srid = srid;
            if (type == 7) {
              if (!parse_geometry(child, srid, depth + 1)) return false;
            } else {
              child.type = type - 3;
              if (type == 4) {
                Point value;
                if ((point_brackets && !consume('(')) || !point(value) ||
                    (point_brackets && !consume(')'))) return false;
                child.points.push_back(value);
              } else if (type == 5) {
                if (!point_list(child.points)) return false;
              } else if (!polygon(child)) return false;
              child.dimensions = dimensions_ == 3 ? 3 : 2;
            }
            geometry.children.push_back(std::move(child));
          } while (consume(','));
          if (!consume(')')) return false;
        }
      }
    }
    // Match refresh_type() timing: an earlier empty child is not retroactively
    // promoted when a later sibling establishes 3D for the outer collection.
    geometry.dimensions = dimensions_ == 3 ? 3 : 2;
    return true;
  }

  const std::string input_;
  const char *current_;
  const char *end_;
  uint32_t dimensions_ = 0;
};
