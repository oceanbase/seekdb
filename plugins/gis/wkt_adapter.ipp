
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

// Plugin-private port of the original WKT visitor's traversal and buffer policy.
// Track the original logical capacity independently from std::string: gcvt's
// fixed/scientific choice depends on remaining bytes when precision is disabled.
class WktWriter {
public:
  explicit WktWriter(int64_t precision) : precision_(precision) {}

  void srid_prefix(uint32_t srid)
  {
    if (srid != 0) {
      const std::string digits = std::to_string(srid);
      reserve(6 + digits.size());
      append("SRID=");
      append(srid == UINT32_MAX ? "NULL" : digits);
      append(";");
    }
  }
  bool write(const Geometry &geometry)
  {
    return geometry.dimensions == 3 ? write_3d(geometry) : write_2d(geometry, false, 0);
  }
  std::string take() { return std::move(text_); }

private:
  static const char *name(uint32_t type)
  {
    static const char *const names[] = {"", "POINT", "LINESTRING", "POLYGON",
        "MULTIPOINT", "MULTILINESTRING", "MULTIPOLYGON", "GEOMETRYCOLLECTION"};
    return type >= 1 && type <= 7 ? names[type] : "";
  }
  void reserve(size_t additional)
  {
    if (additional > text_.max_size() - 8 || text_.size() > text_.max_size() - 8 - additional)
      throw std::length_error("WKT output size");
    const size_t needed = text_.size() + additional + 8;
    if (capacity_ < needed) {
      if (capacity_ == 0) capacity_ = 512;
      while (capacity_ < needed) {
        if (capacity_ > text_.max_size() / 2) throw std::length_error("WKT output size");
        capacity_ *= 2;
      }
      // Only the logical capacity controls formatting. Do not allocate the
      // legacy (possibly quadratic) polygon estimate; std::string grows with
      // bytes actually emitted, not the number of empty inner rings squared.
    }
  }
  void append(std::string_view value) { reserve(value.size()); text_.append(value); }
  size_t remain() const { return capacity_ - text_.size(); }
  void remove_comma() { if (!text_.empty() && text_.back() == ',') text_.pop_back(); }
  bool point(const Point &point, bool three_dimensional)
  {
    const size_t minimum = three_dimensional ? 77 : 50;
    if (remain() < minimum) reserve(minimum);
    const double values[] = {point.x, point.y, point.z};
    std::string number;
    for (unsigned i = 0; i < (three_dimensional ? 3u : 2u); ++i) {
      if (i != 0) append(" ");
      const int width = three_dimensional ? 25 : static_cast<int>(std::min<size_t>(remain(), 256));
      // The old convert_double_to_str checks this even in scaled mode.
      if (width < 25) return false;
      if (!seekdb_gis::format_ewkt_number(values[i], three_dimensional ? -1 : precision_, number, width))
        return false;
      // Like set_length() in the legacy visitor, this does not reserve again.
      if (number.size() > remain()) return false;
      text_ += number;
    }
    return true;
  }
  bool line(const std::vector<Point> &points, bool three_dimensional)
  {
    for (const auto &value : points) {
      if (!point(value, three_dimensional)) return false;
      append(",");
    }
    remove_comma();
    return true;
  }
  bool write_2d(const Geometry &geometry, bool in_multi, unsigned collection_depth)
  {
    if (geometry.dimensions != 2 || geometry.type < 1 || geometry.type > 7) return false;
    const uint32_t type = geometry.type;
    if (type >= 4 && type <= 6) {
      reserve(2 + std::strlen(name(type)));
      append(name(type)); append("(");
      for (const auto &child : geometry.children)
        if (!write_2d(child, true, collection_depth)) return false;
      remove_comma(); append(")");
      if (collection_depth) append(",");
    } else if (type == 7) {
      reserve(2); append(name(type));
      if (geometry.children.empty()) append(" EMPTY");
      else {
        append("(");
        for (const auto &child : geometry.children)
          if (!write_2d(child, false, collection_depth + 1)) return false;
        remove_comma(); append(")");
      }
      if (collection_depth) append(",");
    } else {
      if (type == 1) reserve(34 + (in_multi ? 5 : 0));
      else if (type == 2) reserve(2 + 16 * geometry.points.size() + (in_multi ? 10 : 0));
      else {
        // Preserve the original visitor's estimate (inner ring count squared,
        // not total inner points): changing it can change unscaled formatting.
        const size_t inners = geometry.rings.empty() ? 0 : geometry.rings.size() - 1;
        const size_t points = geometry.rings.empty() ? 0 : geometry.rings.front().size() + inners * inners;
        reserve(geometry.rings.size() * 3 + points * 16 + (in_multi ? 7 : 0));
      }
      if (!in_multi) append(name(type));
      append("(");
      if (type == 1) {
        if (geometry.points.size() != 1 || !point(geometry.points.front(), false)) return false;
      } else if (type == 2) {
        if (!line(geometry.points, false)) return false;
      } else {
        for (const auto &ring : geometry.rings) {
          append("(");
          if (!line(ring, false)) return false;
          append(")"); append(",");
        }
        remove_comma();
      }
      append(")");
      if (in_multi || collection_depth) append(",");
    }
    return true;
  }
  bool write_3d(const Geometry &geometry, bool include_type = true)
  {
    if (geometry.dimensions != 3 || geometry.type < 1 || geometry.type > 7) return false;
    if (include_type) { append(name(geometry.type)); append(" Z "); }
    if (geometry.type == 7 && geometry.children.empty()) { append("EMPTY"); return true; }
    append("(");
    if (geometry.type == 1) {
      if (geometry.points.size() != 1 || !point(geometry.points.front(), true)) return false;
    } else if (geometry.type == 2) {
      if (!line(geometry.points, true)) return false;
    } else if (geometry.type == 3) {
      for (const auto &ring : geometry.rings) {
        append("(");
        if (!line(ring, true)) return false;
        append(")"); append(",");
      }
      remove_comma();
    } else {
      for (const auto &child : geometry.children) {
        if (!write_3d(child, geometry.type == 7)) return false;
        append(",");
      }
      remove_comma();
    }
    append(")");
    return true;
  }

  int64_t precision_;
  size_t capacity_ = 0;
  std::string text_;
};

static bool geometry_to_wkt(const Geometry &geometry, std::string &text,
                            int64_t precision = -1, bool with_srid = false)
{
  WktWriter writer(precision);
  if (with_srid) writer.srid_prefix(geometry.srid);
  if (!writer.write(geometry)) return false;
  text = writer.take();
  return true;
}
