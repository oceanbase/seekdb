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

// Compiles the actual plugin adapter and shared production algorithms. Covers
// numeric/topology semantics and the C callback's error/emit boundary, not SQL
// storage. The companion gis_sql test exercises the real DSO through SQL.
#include "../../../plugins/gis/geometry_engine.cpp"
#include <iostream>

int main()
{
  int failures = 0;
  const auto check = [&](const char *name, double actual, double expected) {
    const bool matches = std::isfinite(actual) && std::abs(actual - expected) <= 1e-12;
    std::cout << (matches ? "PASS " : "FAIL ") << name
              << ": actual=" << actual << ", expected=" << expected << '\n';
    if (!matches) ++failures;
  };
  Geometry diagonal, opposite, parallel, point, horizontal;
  diagonal.type = opposite.type = parallel.type = horizontal.type = 2;
  point.type = 1;
  diagonal.points = {{0, 0}, {2, 2}};
  opposite.points = {{0, 2}, {2, 0}};
  parallel.points = {{0, 1}, {1, 2}};
  point.points = {{1, 1}};
  horizontal.points = {{0, 0}, {2, 0}};

  check("distinct diagonals are not equal",
        relation_result(SEEKDB_GIS_REL_EQUALS, diagonal, opposite, 0), 0);
  check("disjoint parallel segments do not intersect",
        relation_result(SEEKDB_GIS_REL_INTERSECTS, diagonal, parallel, 0), 0);
  check("point-to-segment distance uses its interior", geometry_distance(point, horizontal), 1);
  const auto united = combine_polygons(rectangle(0, 0, 0, 1, 1), rectangle(0, 2, 0, 3, 1),
                                        SEEKDB_GIS_OP_UNION);
  check("union of disjoint unit squares has area two", geometry_area(united), 2);
  Geometry reversed = diagonal;
  std::reverse(reversed.points.begin(), reversed.points.end());
  check("reversed line is topologically equal", relation_result(SEEKDB_GIS_REL_EQUALS, diagonal, reversed, 0), 1);
  check("crossing lines intersect", relation_result(SEEKDB_GIS_REL_INTERSECTS, diagonal, opposite, 0), 1);
  check("crossing lines cross", relation_result(SEEKDB_GIS_REL_CROSSES, diagonal, opposite, 0), 1);
  check("crossing lines have zero distance", geometry_distance(diagonal, opposite), 0);
  check("DWithin uses geometry not bounding boxes", relation_result(SEEKDB_GIS_REL_DWITHIN, diagonal, parallel, 0.5), 0);
  check("DWithin includes exact threshold", relation_result(SEEKDB_GIS_REL_DWITHIN, point, horizontal, 1), 1);
  auto outer = rectangle(0, 0, 0, 4, 4);
  auto inner = rectangle(0, 1, 1, 3, 3);
  auto hole = outer;
  hole.rings.push_back(inner.rings.front()); // Same winding: ring position defines the hole.
  auto center = point;
  center.points = {{2, 2}};
  check("hole excludes its interior", relation_result(SEEKDB_GIS_REL_CONTAINS, hole, center, 0), 0);
  check("distance to polygon hole boundary", geometry_distance(hole, center), 1);
  auto boundary = point;
  boundary.points = {{0, 2}};
  check("contains excludes boundary", relation_result(SEEKDB_GIS_REL_CONTAINS, outer, boundary, 0), 0);
  check("covers includes boundary", relation_result(SEEKDB_GIS_REL_COVERS, outer, boundary, 0), 1);
  check("point touches polygon boundary", relation_result(SEEKDB_GIS_REL_TOUCHES, boundary, outer, 0), 1);
  check("polygon contains whole polygon", relation_result(SEEKDB_GIS_REL_CONTAINS, outer, inner, 0), 1);
  check("polygon within whole polygon", relation_result(SEEKDB_GIS_REL_WITHIN, inner, outer, 0), 1);
  check("point cannot contain polygon", relation_result(SEEKDB_GIS_REL_CONTAINS, center, outer, 0), 0);
  check("difference preserves hole", geometry_area(combine_polygons(outer, inner, SEEKDB_GIS_OP_DIFFERENCE)), 12);
  check("union fills hole", geometry_area(combine_polygons(hole, inner, SEEKDB_GIS_OP_UNION)), 16);
  auto shifted = rectangle(0, 2, 0, 6, 4);
  check("overlapping polygons overlap", relation_result(SEEKDB_GIS_REL_OVERLAPS, outer, shifted, 0), 1);
  check("symmetric difference removes intersection", geometry_area(combine_polygons(outer, shifted, SEEKDB_GIS_OP_SYMMETRIC_DIFFERENCE)), 16);
  Geometry triangle;
  triangle.type = 3;
  triangle.rings = {{{0, 0}, {4, 0}, {0, 4}, {0, 0}}};
  check("nonrectangular union is not an envelope", geometry_area(combine_polygons(triangle, triangle, SEEKDB_GIS_OP_UNION)), 8);
  auto multi = point;
  multi.type = 4;
  multi.points.clear();
  multi.children = {point, center};
  check("multipoint distance", geometry_distance(multi, horizontal), 1);
  check("empty polygon union", geometry_area(combine_polygons(Geometry{3}, triangle, SEEKDB_GIS_OP_UNION)), 8);

  check("area subtracts same-winding hole", geometry_area(hole), 12);
  auto asymmetric_hole = outer;
  asymmetric_hole.rings.push_back(rectangle(0, 2, 2, 3, 3).rings.front());
  const auto hole_centroid = centroid(asymmetric_hole);
  check("centroid subtracts hole mass X", hole_centroid.points[0].x, 29.5 / 15);
  check("centroid subtracts hole mass Y", hole_centroid.points[0].y, 29.5 / 15);
  auto long_line = horizontal;
  long_line.points = {{0, 10}, {6, 10}};
  auto distant_point = point;
  distant_point.points = {{1000, 1000}};
  Geometry mixed;
  mixed.type = 7;
  mixed.children = {horizontal, long_line, distant_point};
  auto weighted = centroid(mixed);
  check("collection centroid uses length weights X", weighted.points[0].x, 2.5);
  check("collection centroid uses length weights Y", weighted.points[0].y, 7.5);
  check("collection length ignores points", geometry_length(mixed), 8);
  mixed.children.push_back(triangle);
  weighted = centroid(mixed);
  check("collection centroid prefers polygons", weighted.points[0].x, 4.0 / 3);
  check("collection centroid ignores lower-dimensional members", weighted.points[0].y, 4.0 / 3);
  check("collection area ignores lines", geometry_area(mixed), 8);
  check("empty centroid is not origin", centroid(empty_geometry(0)).type, 7);
  BufferOptions buffer;
  buffer.distance = -1;
  check("negative polygon buffer erodes", geometry_area(buffer_geometry(outer, buffer)), 4);
  buffer.distance = -3;
  check("complete erosion returns empty collection", buffer_geometry(outer, buffer).type, 7);
  buffer.distance = 0;
  check("zero buffer preserves input", buffer_geometry(horizontal, buffer).type, 2);
  buffer.distance = -1e-12;
  check("near-zero negative point buffer preserves input", buffer_geometry(point, buffer).type, 1);
  buffer.distance = 1;
  buffer.has_end = true;
  buffer.state = 2;
  check("flat line buffer is not expanded box", geometry_area(buffer_geometry(horizontal, buffer)), 4);
  buffer = BufferOptions{};
  buffer.distance = 1;
  buffer.has_point = true;
  buffer.state = 1;
  check("square point buffer", geometry_area(buffer_geometry(point, buffer)), 4);
  buffer.state = 0;
  buffer.point_count = 4;
  check("circle strategy controls tessellation", geometry_area(buffer_geometry(point, buffer)), 2);
  buffer.has_point = false;
  buffer.point_count = 32;
  const double round_area = geometry_area(buffer_geometry(horizontal, buffer));
  check("round line buffer has semicircular ends", round_area, 4 + 16 * std::sin(2 * std::acos(-1.0) / 32));
  auto elbow = horizontal;
  elbow.points = {{0, 0}, {2, 0}, {2, 2}};
  buffer.has_join = true;
  buffer.has_end = true;
  buffer.state = 6;
  check("miter join with flat ends", geometry_area(buffer_geometry(elbow, buffer)), 8);

  const auto on_hole = point_on_surface(hole);
  check("surface point avoids hole X", on_hole.points[0].x, 0.5);
  check("surface point avoids hole Y", on_hole.points[0].y, 2);
  check("surface point is inside polygon", relation_result(SEEKDB_GIS_REL_CONTAINS, hole, on_hole, 0), 1);
  Geometry concave;
  concave.type = 3;
  concave.rings = {{{0,0},{4,0},{4,4},{3,4},{3,1},{1,1},{1,4},{0,4},{0,0}}};
  auto on_concave = point_on_surface(concave);
  check("concave surface point X", on_concave.points[0].x, 0.5);
  check("concave surface point Y", on_concave.points[0].y, 2.5);
  check("concave centroid is not a surface point", relation_result(SEEKDB_GIS_REL_CONTAINS, concave, centroid(concave), 0), 0);
  check("concave surface point is inside", relation_result(SEEKDB_GIS_REL_CONTAINS, concave, on_concave, 0), 1);
  auto on_triangle = point_on_surface(triangle);
  check("slanted edge scanline X", on_triangle.points[0].x, 1);
  check("slanted edge scanline Y", on_triangle.points[0].y, 2);
  auto surface_line = horizontal;
  surface_line.points = {{0,0},{1,5},{10,0}};
  auto on_line = point_on_surface(surface_line);
  check("line selects actual interior vertex X", on_line.points[0].x, 1);
  check("line selects actual interior vertex Y", on_line.points[0].y, 5);
  check("two-vertex line preserves endpoint fallback", point_on_surface(horizontal).points[0].x, 0);
  Geometry nearest = multi;
  nearest.children = {point, point, point};
  nearest.children[0].points = {{0,0}};
  nearest.children[1].points = {{10,0}};
  nearest.children[2].points = {{11,0}};
  check("multipoint selects member not average", point_on_surface(nearest).points[0].x, 10);
  Geometry surface_mixed;
  surface_mixed.type = 7;
  surface_mixed.children = {distant_point, surface_line, hole};
  check("surface collection prefers polygon", point_on_surface(surface_mixed).points[0].x, 0.5);
  surface_mixed.children = {hole, rectangle(0, 10, 0, 16, 2)};
  check("surface collection selects widest interval", point_on_surface(surface_mixed).points[0].x, 13);
  auto collapsed = triangle;
  collapsed.rings = {{{2,3},{3,3},{4,3},{2,3}}};
  check("collapsed polygon uses original first vertex", point_on_surface(collapsed).points[0].x, 2);
  check("surface empty remains empty collection", point_on_surface(empty_geometry(0)).type, 7);
  auto projected = hole;
  projected.srid = 3857;
  check("surface preserves SRID", point_on_surface(projected).srid, 3857);

  Geometry bowtie = triangle;
  bowtie.rings = {{{0,0},{4,4},{0,4},{4,0},{0,0}}};
  check("validity rejects self-intersecting shell", valid_geometry(bowtie), 0);
  check("validity accepts opposite-winding shell and hole", valid_geometry(hole), 1);
  auto outside_hole = outer;
  outside_hole.rings.push_back(rectangle(0, 5, 5, 6, 6).rings[0]);
  check("validity rejects exterior hole", valid_geometry(outside_hole), 0);
  auto overlapping_holes = hole;
  overlapping_holes.rings.push_back(rectangle(0, 2, 2, 3.5, 3.5).rings[0]);
  check("validity rejects overlapping holes", valid_geometry(overlapping_holes), 0);
  Geometry overlapping_multi;
  overlapping_multi.type = 6;
  overlapping_multi.children = {outer, shifted};
  check("validity checks multipolygon member overlap", valid_geometry(overlapping_multi), 0);
  auto overlapping_collection = overlapping_multi;
  overlapping_collection.type = 7;
  check("collection members checked independently", valid_geometry(overlapping_collection), 1);
  overlapping_collection.children.push_back(bowtie);
  check("invalid member makes collection invalid", valid_geometry(overlapping_collection), 0);
  check("validity accepts empty collection", valid_geometry(empty_geometry(0)), 1);
  auto zero_line = horizontal;
  zero_line.points = {{1,1},{1,1}};
  check("validity rejects collapsed line", valid_geometry(zero_line), 0);
  check("validity rejects collapsed polygon", valid_geometry(collapsed), 0);
  auto unclosed = outer;
  unclosed.rings[0].pop_back();
  check("validity applies original ring closure", valid_geometry(unclosed), 1);
  check("validity does not mutate caller rings", unclosed.rings[0].size(), 4);

  const auto repaired_bowtie = make_valid_geometry(bowtie);
  check("repair splits self-intersecting polygon", repaired_bowtie.type, 6);
  check("repair retains both bowtie lobes", geometry_area(repaired_bowtie), 8);
  check("repaired bowtie is valid", valid_geometry(repaired_bowtie), 1);
  const auto repaired_outside = make_valid_geometry(outside_hole);
  check("outside hole becomes independent shell", geometry_area(repaired_outside), 17);
  check("outside shell repair is valid", valid_geometry(repaired_outside), 1);
  check("overlapping holes are unioned before subtraction", geometry_area(make_valid_geometry(overlapping_holes)), 10.75);
  check("multipolygon repair unions overlapping members", geometry_area(make_valid_geometry(overlapping_multi)), 24);
  auto crossing_hole = outer;
  crossing_hole.rings.push_back(rectangle(0, 3, 1, 5, 3).rings[0]);
  check("crossing hole follows original symmetric difference", geometry_area(make_valid_geometry(crossing_hole)), 16);
  check("repair preserves valid hole", geometry_area(make_valid_geometry(hole)), 12);
  check("repair closes unclosed input", make_valid_geometry(unclosed).rings[0].size(), 5);
  check("repair leaves input unchanged", unclosed.rings[0].size(), 4);
  check("collapsed repair retains legacy nonempty result", make_valid_geometry(collapsed).type, 3);
  check("collapsed repair does not promise validity", valid_geometry(make_valid_geometry(collapsed)), 0);
  check("empty repair retains collection", make_valid_geometry(empty_geometry(0)).type, 7);
  check("empty polygon repair stays empty", make_valid_geometry(Geometry{3}).rings.size(), 0);
  check("repair preserves projected SRID", make_valid_geometry(projected).srid, 3857);
  const auto unit_box = rectangle(0, 0, 0, 1, 1);
  const auto clipped = [&](const Geometry &input, const Geometry &box) {
    auto result = clip_by_box(input, box);
    if (!result) { check("unexpected NULL clipping result", 0, 1); return empty_geometry(input.srid); }
    return std::move(*result);
  };
  auto crossing = horizontal;
  crossing.points = {{-1,0.5},{2,0.5}};
  check("box clip keeps line type", clipped(crossing, unit_box).type, 2);
  check("box clip uses line intersections", geometry_length(clipped(crossing, unit_box)), 1);
  crossing.points = {{-1,-1},{2,2}};
  check("box clip diagonal keeps shape", geometry_length(clipped(crossing, unit_box)), std::sqrt(2.0));
  crossing.points = {{-1,0.5},{0.5,2}};
  check("overlapping envelopes do not imply clipped geometry", clipped(crossing, unit_box).type, 7);
  crossing.points = {{-1,1},{1,-1}};
  check("corner-only line contact is empty", clipped(crossing, unit_box).type, 7);
  crossing.points = {{-1,0},{2,0}};
  check("partial overlap drops edge-only line", clipped(crossing, unit_box).type, 7);
  crossing.points = {{0,0},{1,0}};
  check("contained fast path retains boundary line", geometry_length(clipped(crossing, unit_box)), 1);
  check("triangle clip is not bounding rectangle", geometry_area(clipped(triangle, rectangle(0,1,1,4,4))), 2);
  check("box inside hole is empty", clipped(hole, rectangle(0,1.5,1.5,2.5,2.5)).type, 7);
  // Clipped shell is 2*4; the retained part of the hole is 1*2.
  check("clipping across hole opens ring", geometry_area(clipped(hole, rectangle(0,0,0,2,4))), 6);
  auto surrounding = rectangle(0,-1,-1,5,5);
  surrounding.rings.push_back(inner.rings[0]);
  check("clip retains entire interior hole", geometry_area(clipped(surrounding, outer)), 12);
  const auto split_clip = clipped(concave, rectangle(0,0,2,4,4));
  check("clip separates concave polygon pieces", split_clip.type, 6);
  check("clip concave pieces area", geometry_area(split_clip), 4);
  auto concave_hole = concave;
  concave_hole.rings.push_back(rectangle(0,0.2,2.5,0.8,3.5).rings[0]);
  check("clip assigns hole to correct disconnected shell",
        geometry_area(clipped(concave_hole, rectangle(0,0,2,4,4))), 3.4);
  struct PredicateFailingClipFactory : PluginClipFactory {
    int covered_by(const Ring &, const Polygon &, bool &) const { return 73; }
  };
  PredicateFailingClipFactory failed_factory;
  cartesian::BoxClipper<PredicateFailingClipFactory> failed_clipper({0,2,4,4}, failed_factory);
  CartesianPolygons failed_output;
  check("clip propagates hole-assignment predicate error",
        failed_clipper.clip_polygon(cartesian_polygon(concave_hole), failed_output), 73);
  check("failed clip does not publish partial polygons", failed_output.size(), 0);
  check("disjoint shapes with overlapping boxes clip empty", clipped(triangle, rectangle(0,3,3,4,4)).type, 7);
  check("degenerate overlapping clip box returns NULL", clip_by_box(outer, point).has_value(), 0);
  check("empty clip box returns NULL", clip_by_box(outer, empty_geometry(0)).has_value(), 0);
  check("empty input stays empty", clipped(empty_geometry(0), unit_box).type, 7);
  auto inside_point = point;
  inside_point.points = {{0.5,0.5}};
  check("point inside clip is not lost", clipped(inside_point, unit_box).type, 1);
  auto clip_points = multi;
  auto edge_point = point;
  edge_point.points = {{0,0.5}};
  clip_points.children = {edge_point, inside_point, distant_point};
  check("partially clipped multipoint follows strict edge rule", clipped(clip_points, unit_box).type, 1);
  check("contained point on edge survives fast path", clipped(edge_point, unit_box).type, 1);
  check("clip preserves input SRID", clipped(projected, rectangle(0,0,0,2,4)).srid, 3857);
  for (int i = -1; i <= 4; ++i) {
    for (int j = -1; j <= 4; ++j) {
      const auto box = rectangle(0,i,j,i+1.5,j+1.5);
      CartesianPolygons reference;
      bg::intersection(cartesian_polygon(triangle), cartesian_polygon(box), reference);
      check("triangle clip area agrees with independent overlay",
            geometry_area(clipped(triangle, box)), cartesian::area(reference));
    }
  }
  for (int fail_at = 0; fail_at < 4; ++fail_at) {
    using Handle = std::optional<int>;
    Handle shell(1), result(99);
    const auto hole_at = [&](unsigned long, Handle &hole) { hole = 2; return fail_at == 0 ? 73 : 0; };
    const auto intersects = [&](const Handle &, const Handle &, bool &value) {
      value = true; return fail_at == 1 ? 73 : 0;
    };
    const auto merge = [&](const Handle &, Handle &) { return fail_at == 2 ? 73 : 0; };
    const auto difference = [&](const Handle &, const Handle &, Handle &value) {
      value = 3; return fail_at == 3 ? 73 : 0;
    };
    check("shared repair propagates callback failure",
          cartesian::repair_polygon_holes(shell, 2, hole_at, intersects, merge, difference, result), 73);
    check("shared repair failure does not publish partial output", *result, 99);
  }

  const auto tile_box = rectangle(0, 0, 0, 10, 10);
  const auto mvt = [&](const char *name, const Geometry &input, double extent = 10,
                       double buffer = 0, bool clip = true) {
    auto result = as_mvt_geometry(input, tile_box, extent, buffer, clip);
    check(name, result.has_value(), 1);
    return result.value_or(empty_geometry(input.srid));
  };
  auto tie_point = point;
  tie_point.points = {{0.5, 1.5}};
  auto tile = mvt("MVT point survives", tie_point);
  check("MVT snaps X ties to even", tile.points.at(0).x, 0);
  check("MVT flips Y then snaps ties to even", tile.points.at(0).y, 8);
  tie_point.srid = 3857;
  check("MVT retains input SRID", mvt("MVT projected point", tie_point).srid, 3857);
  auto outside_tile = point;
  outside_tile.points = {{20, 20}};
  check("MVT outside point is NULL", as_mvt_geometry(outside_tile, tile_box, 10, 0, true).has_value(), 0);
  tile = mvt("MVT clip=false preserves outside point", outside_tile, 10, 0, false);
  check("MVT unclipped X", tile.points.at(0).x, 20);
  check("MVT unclipped Y", tile.points.at(0).y, -10);
  check("MVT buffer admits outside point", mvt("MVT buffer", outside_tile, 10, 10).points.at(0).x, 20);
  auto crossing_tile = horizontal;
  crossing_tile.points = {{-5, 5}, {15, 0}};
  tile = mvt("MVT clips slanted segment", crossing_tile);
  check("MVT clip is actual intersection X", tile.points.at(0).x, 0);
  check("MVT re-snaps first intersection Y", tile.points.at(0).y, 6);
  check("MVT re-snaps last intersection Y", tile.points.at(1).y, 9);
  check("MVT clipped length is not endpoint clamp", geometry_length(tile), std::sqrt(109));
  auto tiny_tile = horizontal;
  tiny_tile.points = {{0.49, 1}, {0.51, 1}};
  check("MVT precheck removes subpixel line before rounding", as_mvt_geometry(tiny_tile, tile_box, 10, 0, false).has_value(), 0);
  check("MVT empty input is NULL", as_mvt_geometry(empty_geometry(0), tile_box, 10, 0, true).has_value(), 0);
  check("MVT collapsed polygon is NULL", as_mvt_geometry(rectangle(0, 1.1, 1.1, 1.2, 1.2), tile_box, 10, 0, true).has_value(), 0);
  auto collinear_tile = horizontal;
  collinear_tile.points = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
  check("MVT removes collinear grid vertices", mvt("MVT collinear", collinear_tile).points.size(), 2);
  collinear_tile.points = {{0, 0}, {2, 2}, {1, 1}, {3, 3}};
  check("MVT retains direction reversals", mvt("MVT reversal", collinear_tile).points.size(), 4);
  auto tile_multi = multi;
  tile_multi.children = {point, point, point, point};
  tile_multi.children[0].points = {{0.1, 0.1}};
  tile_multi.children[1].points = {{0.2, 0.2}};
  tile_multi.children[2].points = {{1.1, 1.1}};
  tile_multi.children[3].points = {{0.1, 0.1}};
  check("MVT deduplicates adjacent points only", mvt("MVT multipoint", tile_multi).children.size(), 3);
  auto tile_mixed = empty_geometry(0);
  tile_mixed.children = {outside_tile, horizontal, outer};
  tile = mvt("MVT collection selects polygons", tile_mixed);
  check("MVT highest-dimensional output", tile.type, 3);
  check("MVT ignores distant lower-dimensional members", geometry_area(tile), 16);
  tile = mvt("MVT repairs shell and hole after inversion", hole);
  check("MVT retains hole area", geometry_area(tile), 12);
  check("MVT repairs output winding", valid_geometry(tile), 1);
  auto clipped_triangle = triangle;
  clipped_triangle.rings = {{{-5,0},{15,0},{-5,20},{-5,0}}};
  for (int winding = 0; winding < 2; ++winding) {
    tile = mvt("MVT clips triangle before repair", clipped_triangle);
    check("MVT partial triangle area", geometry_area(tile), 87.5);
    check("MVT clipped triangle valid", valid_geometry(tile), 1);
    std::reverse(clipped_triangle.rings[0].begin(), clipped_triangle.rings[0].end());
  }
  auto cut_hole = rectangle(0, 0, 0, 10, 10);
  cut_hole.rings.push_back(rectangle(0, 2, 2, 8, 8).rings[0]);
  auto cut_tile = as_mvt_geometry(cut_hole, rectangle(0, 0, 0, 5, 10), 10, 0, true);
  check("MVT crossing hole remains after clip", cut_tile.has_value(), 1);
  check("MVT clipped hole opens shell", geometry_area(cut_tile.value_or(empty_geometry(0))), 64);
  auto u_shape = triangle;
  u_shape.rings = {{{0,0},{4,0},{4,4},{3,4},{3,1},{1,1},{1,4},{0,4},{0,0}}};
  auto split_tile = as_mvt_geometry(u_shape, rectangle(0, 0, 2, 4, 4), 4, 0, true);
  check("MVT concave clipping yields output", split_tile.has_value(), 1);
  check("MVT concave clipping retains both polygons", split_tile.value_or(empty_geometry(0)).type, 6);
  check("MVT concave clipping area", geometry_area(split_tile.value_or(empty_geometry(0))), 8);
  auto mvt_bowtie = bowtie;
  tile = mvt("MVT repairs self crossing polygon without clipping", mvt_bowtie, 10, 0, false);
  check("MVT bowtie repair area", geometry_area(tile), 8);
  check("MVT bowtie repair validity", valid_geometry(tile), 1);
  auto empty_shell_collection = empty_geometry(0);
  empty_shell_collection.children = {point, Geometry{3}};
  check("MVT highest empty polygon does not fall back to points",
        as_mvt_geometry(empty_shell_collection, tile_box, 10, 0, false).has_value(), 0);
  std::vector<Point> floor_points = {{1.9, -1.1}, {1.1, -1.9}, {2.9, -2.1}};
  tile_grid_sequence(floor_points, 1, true);
  check("MVT final polygon grid uses floor", floor_points.at(0).y, -2);
  check("MVT floor grid deduplicates", floor_points.size(), 2);

  check("spherical dot includes both Z operands", spherical::dot(spherical::Vector3{1,2,3}, spherical::Vector3{4,5,6}), 32);
  check("spherical identical points recognized", spherical::same(spherical::Vector3{1,0,0}, spherical::Vector3{1,0,0}), 1);
  check("spherical equal Z alone does not mean same point", spherical::same(spherical::Vector3{1,0,0}, spherical::Vector3{0,1,0}), 0);
  spherical::Box arc_box{};
  check("spherical great-circle segment accepted", spherical::line_box(
      spherical::from_degrees(-45,60), spherical::from_degrees(45,60), arc_box), 1);
  check("spherical segment includes interior latitude extremum", arc_box.zmax, std::sqrt(6.0 / 7));
  check("spherical segment includes interior X extremum", arc_box.xmax, std::sqrt(1.0 / 7));
  check("spherical date-line segment accepted", spherical::line_box(
      spherical::from_degrees(170,0), spherical::from_degrees(-170,0), arc_box), 1);
  check("spherical date-line segment crosses negative X axis", arc_box.xmin, -1);
  arc_box.xmin = 73;
  check("spherical antipodal segment rejected", spherical::line_box(
      spherical::from_degrees(0,0), spherical::from_degrees(180,0), arc_box), 0);
  check("spherical invalid arc does not publish partial box", arc_box.xmin, 73);
  unsigned arc_escapes = 0, arc_asymmetries = 0, loose_boxes = 0;
  for (unsigned i = 0; i < 200; ++i) {
    const auto a = spherical::from_degrees(-170.13 + (i * 71 % 330), -75.37 + (i * 19 % 150));
    const auto b = spherical::from_degrees(-169.73 + (i * 43 % 330), -74.61 + (i * 31 % 150));
    spherical::Box box{}, reverse{}, sampled{};
    if (!spherical::line_box(a, b, box) || !spherical::line_box(b, a, reverse)) { ++arc_escapes; continue; }
    const double angle = std::acos(spherical::unit_range(spherical::dot(a,b)));
    spherical::point_box(a, sampled);
    for (unsigned j = 0; j <= 512; ++j) {
      const double t = j / 512.0;
      const double w1 = std::sin((1-t)*angle) / std::sin(angle), w2 = std::sin(t*angle) / std::sin(angle);
      const spherical::Vector3 p{w1*a.x+w2*b.x, w1*a.y+w2*b.y, w1*a.z+w2*b.z};
      if (p.x < box.xmin-1e-12 || p.x > box.xmax+1e-12 || p.y < box.ymin-1e-12 ||
          p.y > box.ymax+1e-12 || p.z < box.zmin-1e-12 || p.z > box.zmax+1e-12) ++arc_escapes;
      spherical::include_point(p, sampled);
    }
    const double exact[] = {box.xmin,box.xmax,box.ymin,box.ymax,box.zmin,box.zmax};
    const double reversed[] = {reverse.xmin,reverse.xmax,reverse.ymin,reverse.ymax,reverse.zmin,reverse.zmax};
    const double samples[] = {sampled.xmin,sampled.xmax,sampled.ymin,sampled.ymax,sampled.zmin,sampled.zmax};
    for (unsigned j = 0; j < 6; ++j) {
      if (std::abs(exact[j] - reversed[j]) > 1e-12) ++arc_asymmetries;
      if (std::abs(exact[j] - samples[j]) > 1e-4) ++loose_boxes;
    }
  }
  check("spherical boxes contain 102600 independent arc samples", arc_escapes, 0);
  check("spherical box is invariant under endpoint reversal", arc_asymmetries, 0);
  check("spherical extrema are tight against dense sampling", loose_boxes, 0);
  check("best SRID north pole branch", spherical::select_srid(0,71,20,44), 999061);
  check("best SRID south pole inclusive threshold", spherical::select_srid(0,-70,20,44), 999161);
  check("best SRID north latitude threshold is strict", spherical::select_srid(0,70,5,0), 999031);
  check("best SRID polar height threshold is strict", spherical::select_srid(0,71,7,45), 999000);
  check("best SRID north UTM", spherical::select_srid(2,49,0,0), 999031);
  check("best SRID south UTM", spherical::select_srid(18,-33,0,0), 999134);
  check("best SRID last UTM zone is clamped", spherical::select_srid(180,0,0,0), 999060);
  check("best SRID equatorial north LAEA", spherical::select_srid(0,10,20,0), 999229);
  check("best SRID equatorial south LAEA", spherical::select_srid(0,-10,20,0), 999209);
  check("best SRID middle-latitude LAEA", spherical::select_srid(0,40,40,0), 999247);
  check("best SRID high-latitude LAEA", spherical::select_srid(0,60,80,0), 999265);
  check("best SRID UTM width threshold is strict", spherical::select_srid(0,0,6,0), 999229);
  check("best SRID LAEA width threshold is strict", spherical::select_srid(0,0,30,0), 999000);
  check("best SRID LAEA height threshold is strict", spherical::select_srid(0,0,20,25), 999000);
  auto geographic_point = point;
  geographic_point.srid = 4326; geographic_point.points = {{2,49}};
  check("best SRID is selected projection not input SRID", geometry_best_srid(geographic_point), 999031);
  auto best_south = geographic_point; best_south.points = {{18,-33}};
  check("best SRID southern hemisphere", geometry_best_srid(best_south), 999134);
  auto polar = geographic_point; polar.points = {{0,80}};
  check("best SRID north Lambert geometry", geometry_best_srid(polar), 999061);
  polar.points = {{0,-80}};
  check("best SRID south Lambert geometry", geometry_best_srid(polar), 999161);
  auto date_line = geographic_point; date_line.points = {{180,0}};
  check("best SRID date-line point", geometry_best_srid(date_line), 999060);
  auto best_empty = empty_geometry(0);
  check("best SRID empty uses private world Mercator", geometry_best_srid(best_empty), 999000);
  check("best SRID empty first does not discard second", geometry_best_srid(best_empty, &geographic_point), 999031);
  check("best SRID empty second does not discard first", geometry_best_srid(geographic_point, &best_empty), 999031);
  auto best_z = geographic_point; best_z.dimensions = 3; best_z.points[0].z = 999;
  check("best SRID 3D uses XY", geometry_best_srid(best_z), 999031);
  auto best_pair = multi;
  best_pair.srid = 4326; best_pair.children = {geographic_point,geographic_point};
  best_pair.children[0].points = {{-10,5}}; best_pair.children[1].points = {{10,5}};
  check("best SRID multipoint selects LAEA", geometry_best_srid(best_pair), 999229);
  check("best SRID merges both argument bounds", geometry_best_srid(best_pair.children[0], &best_pair.children[1]), 999229);

  const auto hash_of = [&](const Geometry &input, int64_t precision = 0) {
    return geometry_geohash(input, precision).value_or("<NULL>");
  };
  Geometry hash_origin = point;
  hash_origin.points = {{0, 0}};
  check("GeoHash default point precision is 20", hash_of(hash_origin).size(), 20);
  check("GeoHash exact origin splits use >=", hash_of(hash_origin, 5) == "s0000", 1);
  check("GeoHash negative precision selects automatic", hash_of(hash_origin, -20) == hash_of(hash_origin), 1);
  check("GeoHash explicit precision is not limited to 32", hash_of(hash_origin, 64) == "s" + std::string(63, '0'), 1);
  auto corner_hash = hash_origin;
  corner_hash.points = {{-180, -90}};
  check("GeoHash southwest boundary", hash_of(corner_hash, 6) == "000000", 1);
  corner_hash.points = {{180, 90}};
  check("GeoHash northeast boundary", hash_of(corner_hash, 6) == "zzzzzz", 1);
  check("GeoHash crossing equator and prime meridian returns empty string",
        hash_of(rectangle(0, -1, -1, 1, 1)).empty(), 1);
  check("GeoHash empty geometry is NULL", geometry_geohash(empty_geometry(0), 5).has_value(), 0);
  check("GeoHash empty polygon is NULL", geometry_geohash(Geometry{3}, 5).has_value(), 0);
  auto hash_center = hash_origin;
  hash_center.points = {{2, 2}};
  check("GeoHash triangle uses box center not centroid", hash_of(triangle, 8) == hash_of(hash_center, 8), 1);
  auto skew_line = horizontal;
  skew_line.points = {{0,0},{0,0},{0,0},{4,4}};
  check("GeoHash line ignores vertex weighting", hash_of(skew_line, 8) == hash_of(hash_center, 8), 1);
  auto hash_hole = outer;
  hash_hole.rings.push_back(rectangle(0, 500, 500, 600, 600).rings[0]);
  check("GeoHash PG bounds exclude interior rings", hash_of(hash_hole, 8) == hash_of(hash_center, 8), 1);
  auto hash_collection = empty_geometry(0);
  hash_collection.children = {triangle, skew_line};
  check("GeoHash collection uses union bounds", hash_of(hash_collection, 8) == hash_of(hash_center, 8), 1);
  auto hash_z = hash_center;
  hash_z.dimensions = 3;
  hash_z.points[0].z = 9999;
  check("GeoHash original 3D projection uses XY", hash_of(hash_z, 8) == hash_of(hash_center, 8), 1);
  for (uint32_t srid : {4326u, 3857u}) {
    auto tagged = hash_center; tagged.srid = srid;
    check("GeoHash does not transform or swap raw XY", hash_of(tagged, 8) == hash_of(hash_center, 8), 1);
  }
  // Independent integer-bin encoder (no iterative floating range bisection).
  const auto reference_hash = [](double x, double y, unsigned length) {
    const unsigned x_bits = (length * 5 + 1) / 2, y_bits = length * 5 / 2;
    const uint64_t x_count = UINT64_C(1) << x_bits, y_count = UINT64_C(1) << y_bits;
    const uint64_t x_index = std::min(x_count - 1, uint64_t((x + 180) / 360 * x_count));
    const uint64_t y_index = std::min(y_count - 1, uint64_t((y + 90) / 180 * y_count));
    std::string result;
    unsigned digit = 0;
    for (unsigned bit = 0; bit < length * 5; ++bit) {
      const bool use_x = (bit % 2) == 0;
      const unsigned shift = (use_x ? x_bits : y_bits) - bit / 2 - 1;
      digit = digit * 2 + (((use_x ? x_index : y_index) >> shift) & 1);
      if (bit % 5 == 4) {
        result.push_back("0123456789bcdefghjkmnpqrstuvwxyz"[digit]);
        digit = 0;
      }
    }
    return result;
  };
  unsigned hash_differences = 0, precision_differences = 0;
  for (unsigned i = 0; i < 120; ++i) {
    const double x = -170.123 + (i * 71 % 330), y = -80.321 + (i * 43 % 155);
    auto sample = hash_origin; sample.points = {{x, y}};
    for (unsigned length = 1; length <= 10; ++length) {
      if (hash_of(sample, length) != reference_hash(x, y, length)) ++hash_differences;
    }
    const auto low = reference_hash(x, y, 10), high = reference_hash(x + 0.073, y + 0.041, 10);
    size_t common = 0;
    while (common < low.size() && low[common] == high[common]) ++common;
    if (hash_of(rectangle(0, x, y, x + 0.073, y + 0.041)) != low.substr(0, common)) ++precision_differences;
  }
  check("GeoHash 1200 integer-bin reference comparisons", hash_differences, 0);
  check("GeoHash 120 automatic common-prefix comparisons", precision_differences, 0);
  int hash_appends = 0;
  check("GeoHash shared helper preserves append failure", seekdb::geo::geohash::encode(
      {0,0,0,0}, 100, [&](char) { return ++hash_appends == 3 ? 73 : 0; }), 73);
  check("GeoHash stops on append failure", hash_appends, 3);

  struct Sink { int emits = 0; int boolean = -1; int32_t srid = 0; bool is_null = false; std::string text; std::string type; uint64_t size = 0; seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK; } sink;
  seekdb_plugin_execution_context_v1_t context = {};
  context.struct_size = sizeof(context);
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.emit_result = [](seekdb_plugin_host_handle_t *host, const seekdb_plugin_execution_result_v1_t *result) {
    auto &sink = *reinterpret_cast<Sink *>(host);
    ++sink.emits;
    sink.is_null = result->is_null != 0;
    sink.type = result->type_id ? result->type_id : "";
    sink.size = result->data_size;
    if (result->type_id && std::strcmp(result->type_id, "org.seekdb.gis.scalar.int32") == 0 &&
        !result->is_null && result->data_size == sizeof(sink.srid)) std::memcpy(&sink.srid, result->data, sizeof(sink.srid));
    if (result->type_id && std::strcmp(result->type_id, "org.seekdb.gis.scalar.bytes") == 0) {
      sink.text = result->data && result->data_size
          ? std::string(reinterpret_cast<const char *>(result->data), result->data_size) : "";
    }
    if (result->data != nullptr && result->data_size == 1) sink.boolean = result->data[0];
    return sink.status;
  };
  auto *instance = reinterpret_cast<seekdb_plugin_instance_handle_t *>(&sink);
  for (auto operation : {seekdb_gis_spatial_cellid_operation, seekdb_gis_mbr_operation}) {
    seekdb_plugin_execution_value_v1_t argument = {};
    argument.struct_size = sizeof(argument);
    argument.type_id = "org.seekdb.gis.geometry";
    // Payload deliberately absent: placeholders must not decode the operand.
    for (uint8_t is_null : {0, 1}) {
      argument.is_null = is_null;
      sink.emits = 0; sink.is_null = false;
      check("spatial index placeholder ABI succeeds", operation(instance, &context, &argument, 1), SEEKDB_PLUGIN_STATUS_OK);
      check("spatial index placeholder emits once", sink.emits, 1);
      check("spatial index placeholder is NULL", sink.is_null, 1);
      check("spatial index placeholder has no fabricated bytes", sink.size, 0);
      check("spatial index placeholder preserves result type", sink.type ==
          (operation == seekdb_gis_spatial_cellid_operation ? "org.seekdb.gis.scalar.uint64" : "org.seekdb.gis.scalar.bytes"), 1);
    }
    sink.emits = 0;
    argument.struct_size = 0;
    check("spatial index placeholder rejects short ABI", operation(instance, &context, &argument, 1), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
    argument.struct_size = sizeof(argument);
    argument.type_id = "core.type.int64";
    check("spatial index placeholder rejects wrong type", operation(instance, &context, &argument, 1), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
    argument.type_id = "org.seekdb.gis.geometry";
    check("spatial index placeholder rejects missing instance", operation(nullptr, &context, &argument, 1), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
    check("spatial index placeholder rejects missing context", operation(instance, nullptr, &argument, 1), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
    check("spatial index placeholder rejects missing arguments", operation(instance, &context, nullptr, 1), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
    check("spatial index placeholder rejects wrong arity", operation(instance, &context, &argument, 0), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
    check("spatial index invalid ABI emits nothing", sink.emits, 0);
    sink.status = SEEKDB_PLUGIN_STATUS_BUSY;
    check("spatial index placeholder propagates callback error", operation(instance, &context, &argument, 1), SEEKDB_PLUGIN_STATUS_BUSY);
    check("spatial index placeholder never retries callback", sink.emits, 1);
    sink.status = SEEKDB_PLUGIN_STATUS_OK;
    auto throwing = context;
    throwing.emit_result = [](seekdb_plugin_host_handle_t *, const seekdb_plugin_execution_result_v1_t *) -> seekdb_plugin_status_t {
      throw std::bad_alloc();
    };
    check("spatial index placeholder fences allocation exception", operation(instance, &throwing, &argument, 1), SEEKDB_PLUGIN_STATUS_NO_MEMORY);
    throwing.emit_result = [](seekdb_plugin_host_handle_t *, const seekdb_plugin_execution_result_v1_t *) -> seekdb_plugin_status_t {
      throw 1;
    };
    check("spatial index placeholder fences other exceptions", operation(instance, &throwing, &argument, 1), SEEKDB_PLUGIN_STATUS_INTERNAL);
  }
  const auto best_call = [&](const Geometry &first, const Geometry *second = nullptr) {
    std::vector<uint8_t> bytes[2]; encode(first, bytes[0]);
    if (second) encode(*second, bytes[1]);
    seekdb_plugin_execution_value_v1_t args[2] = {};
    for (unsigned i = 0; i < 2; ++i) {
      args[i].struct_size = sizeof(args[i]); args[i].type_id = "org.seekdb.gis.geometry";
      args[i].data = bytes[i].data(); args[i].data_size = bytes[i].size();
    }
    sink.emits = 0; sink.srid = 0;
    return seekdb_gis_best_srid_operation(instance, &context, args, second ? 2 : 1);
  };
  check("best SRID C ABI success", best_call(geographic_point), SEEKDB_PLUGIN_STATUS_OK);
  check("best SRID C ABI signed int32 result", sink.srid, 999031);
  check("best SRID C ABI emits once", sink.emits, 1);
  check("best SRID projected nonempty input rejected", best_call(point), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  check("best SRID rejected input emits nothing", sink.emits, 0);
  auto invalid_best = geographic_point; invalid_best.srid = 99999;
  check("best SRID unknown SRS rejected", best_call(invalid_best), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  invalid_best = geographic_point; invalid_best.points[0].y = 91;
  check("best SRID invalid coordinates rejected", best_call(invalid_best), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  auto antipodal = horizontal; antipodal.srid = 4326; antipodal.points = {{0,0},{180,0}};
  check("best SRID antipodal line error reaches ABI", best_call(antipodal), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  check("best SRID antipodal error emits nothing", sink.emits, 0);
  const auto hash_call = [&](const Geometry &input, int64_t precision, unsigned null_mask = 0) {
    std::vector<uint8_t> bytes;
    encode(input, bytes);
    seekdb_plugin_execution_value_v1_t arguments[2] = {};
    for (auto &argument : arguments) argument.struct_size = sizeof(argument);
    arguments[0].type_id = "org.seekdb.gis.geometry";
    arguments[0].data = bytes.data(); arguments[0].data_size = bytes.size();
    arguments[1].type_id = "core.type.int64";
    // Exercise the unaligned integer ABI as well.
    uint8_t integer_bytes[sizeof(precision) + 1] = {};
    std::memcpy(integer_bytes + 1, &precision, sizeof(precision));
    arguments[1].data = integer_bytes + 1; arguments[1].data_size = sizeof(precision);
    arguments[0].is_null = null_mask & 1; arguments[1].is_null = (null_mask >> 1) & 1;
    sink.emits = 0; sink.is_null = false; sink.text.clear();
    return seekdb_gis_geohash_operation(instance, &context, arguments, 2);
  };
  check("GeoHash nullable precision ABI", hash_call(hash_origin, 5, 2), SEEKDB_PLUGIN_STATUS_OK);
  check("GeoHash NULL precision selects automatic", sink.text.size(), 20);
  check("GeoHash NULL precision is not NULL result", sink.is_null, 0);
  check("GeoHash NULL geometry ABI", hash_call(hash_origin, INT64_MAX, 1), SEEKDB_PLUGIN_STATUS_OK);
  check("GeoHash NULL geometry short circuits precision", sink.is_null, 1);
  check("GeoHash empty geometry short circuits precision", hash_call(empty_geometry(0), INT64_MAX), SEEKDB_PLUGIN_STATUS_OK);
  check("GeoHash empty geometry emits NULL", sink.is_null, 1);
  check("GeoHash empty string ABI", hash_call(rectangle(0,-1,-1,1,1), 0), SEEKDB_PLUGIN_STATUS_OK);
  check("GeoHash empty string is not NULL", sink.is_null, 0);
  check("GeoHash empty string has zero bytes", sink.text.size(), 0);
  check("GeoHash negative precision ABI", hash_call(hash_origin, INT32_MIN), SEEKDB_PLUGIN_STATUS_OK);
  check("GeoHash negative precision returns 20 bytes", sink.text.size(), 20);
  for (const int64_t precision : {INT64_MIN, int64_t(INT32_MAX) + 1, INT64_MAX}) {
    check("GeoHash rejects precision overflow", hash_call(hash_origin, precision), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
    check("GeoHash rejected precision emits nothing", sink.emits, 0);
  }
  check("GeoHash output budget reports exhaustion", hash_call(hash_origin, 16777217), SEEKDB_PLUGIN_STATUS_NO_MEMORY);
  check("GeoHash exhausted output is not truncated", sink.emits, 0);
  auto bad_hash = hash_origin; bad_hash.points[0].x = 181;
  check("GeoHash rejects longitude without clamping", hash_call(bad_hash, 5), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  bad_hash.points[0] = {0, -91};
  check("GeoHash rejects latitude without clamping", hash_call(bad_hash, 5), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  bad_hash = hash_origin; bad_hash.srid = 99999;
  check("GeoHash unknown SRS not fabricated", hash_call(bad_hash, 5), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  auto bad_hash_multi = multi; bad_hash_multi.children[0] = triangle;
  check("GeoHash rejects malformed MultiPoint", hash_call(bad_hash_multi, 5), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  const auto mvt_call = [&](const Geometry &input, const Geometry &box, double extent,
                            double buffer, double clip, unsigned null_mask = 0) {
    std::vector<uint8_t> bytes[2];
    encode(input, bytes[0]); encode(box, bytes[1]);
    const double numbers[] = {extent, buffer, clip};
    seekdb_plugin_execution_value_v1_t arguments[5] = {};
    for (int i = 0; i < 5; ++i) {
      arguments[i].struct_size = sizeof(arguments[i]);
      arguments[i].type_id = i < 2 ? "org.seekdb.gis.geometry" : "org.seekdb.gis.scalar.float64";
      arguments[i].data = i < 2 ? bytes[i].data() : reinterpret_cast<const uint8_t *>(&numbers[i - 2]);
      arguments[i].data_size = i < 2 ? bytes[i].size() : sizeof(double);
      arguments[i].is_null = (null_mask >> i) & 1;
    }
    sink.emits = 0;
    sink.is_null = false;
    return seekdb_gis_geometry_operation(SEEKDB_GIS_OP_ASMVTGEOM, instance, &context, arguments, 5);
  };
  check("MVT NULL controls use defaults", mvt_call(point, tile_box, 0, -1, 999, 28), SEEKDB_PLUGIN_STATUS_OK);
  check("MVT NULL controls do not propagate NULL", sink.is_null, 0);
  check("MVT NULL geometry succeeds", mvt_call(point, tile_box, 10, 0, 1, 1), SEEKDB_PLUGIN_STATUS_OK);
  check("MVT NULL geometry emits NULL", sink.is_null, 1);
  check("MVT NULL input does not hide invalid controls", mvt_call(point, tile_box, 0, 0, 1, 1), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  check("MVT invalid controls with NULL input emit nothing", sink.emits, 0);
  check("MVT NULL bounds errors", mvt_call(point, tile_box, 10, 0, 1, 2), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  check("MVT NULL bounds emits nothing", sink.emits, 0);
  for (const double extent : {0.0, -1.0, 0.5, 2147483648.0}) {
    check("MVT rejects invalid extent", mvt_call(point, tile_box, extent, 0, 1), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
    check("MVT extent failure emits nothing", sink.emits, 0);
  }
  for (const double buffer : {-1.0, 0.5, 2147483648.0}) {
    check("MVT rejects invalid buffer", mvt_call(point, tile_box, 10, buffer, 1), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
    check("MVT buffer failure emits nothing", sink.emits, 0);
  }
  check("MVT permits INT32_MAX extent", mvt_call(point, tile_box, 2147483647.0, 0, 1), SEEKDB_PLUGIN_STATUS_OK);
  check("MVT rejects invalid clip control", mvt_call(point, tile_box, 10, 0, 128), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  check("MVT rejects degenerate bounds", mvt_call(point, point, 10, 0, 1), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  auto invalid_mvt = point;
  invalid_mvt.dimensions = 3;
  check("MVT does not discard Z", mvt_call(invalid_mvt, tile_box, 10, 0, 1), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  invalid_mvt = point; invalid_mvt.srid = 4326;
  check("MVT rejects geographic coordinates", mvt_call(invalid_mvt, tile_box, 10, 0, 1), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  invalid_mvt = point; invalid_mvt.points[0] = {1e308, 1e308};
  check("MVT rejects affine overflow", mvt_call(invalid_mvt, tile_box, 100, 0, 1), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  check("MVT overflow emits no partial result", sink.emits, 0);
  const auto clip_call = [&](const Geometry &input, const Geometry &box) {
    std::vector<uint8_t> bytes[2];
    encode(input, bytes[0]); encode(box, bytes[1]);
    seekdb_plugin_execution_value_v1_t arguments[2] = {};
    for (int i = 0; i < 2; ++i) {
      arguments[i].struct_size = sizeof(arguments[i]);
      arguments[i].type_id = "org.seekdb.gis.geometry";
      arguments[i].data = bytes[i].data();
      arguments[i].data_size = bytes[i].size();
    }
    sink.emits = 0;
    sink.is_null = false;
    return seekdb_gis_geometry_operation(SEEKDB_GIS_OP_CLIP_BY_BOX, instance, &context, arguments, 2);
  };
  check("empty box emits SQL NULL successfully", clip_call(point, empty_geometry(0)), SEEKDB_PLUGIN_STATUS_OK);
  check("empty box result is NULL not empty geometry", sink.is_null, 1);
  check("empty box emits once", sink.emits, 1);
  auto unsupported_clip = point;
  unsupported_clip.srid = 4326;
  check("empty box does not hide unsupported input", clip_call(unsupported_clip, empty_geometry(0)), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  check("rejected clip emits nothing", sink.emits, 0);
  const auto validity_call = [&](const Geometry &geometry) {
    std::vector<uint8_t> bytes;
    encode(geometry, bytes);
    seekdb_plugin_execution_value_v1_t argument = {};
    argument.struct_size = sizeof(argument);
    argument.type_id = "org.seekdb.gis.geometry";
    argument.data = bytes.data();
    argument.data_size = bytes.size();
    sink.emits = 0;
    sink.boolean = -1;
    return seekdb_gis_valid_operation(instance, &context, &argument, 1);
  };
  check("invalid topology is a boolean not an error", validity_call(bowtie), SEEKDB_PLUGIN_STATUS_OK);
  check("invalid topology emits false", sink.boolean, 0);
  check("invalid topology emits once", sink.emits, 1);
  auto malformed_multi = overlapping_multi;
  malformed_multi.children[0] = horizontal;
  check("malformed multipolygon is an error", validity_call(malformed_multi), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  check("malformed validity input emits nothing", sink.emits, 0);
  auto malformed_tail = overlapping_collection;
  malformed_tail.children = {bowtie, malformed_multi};
  check("invalid prefix cannot hide malformed collection tail", validity_call(malformed_tail), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  check("malformed collection emits nothing", sink.emits, 0);
  const auto repair_call = [&](const Geometry &geometry) {
    std::vector<uint8_t> bytes;
    encode(geometry, bytes);
    seekdb_plugin_execution_value_v1_t argument = {};
    argument.struct_size = sizeof(argument);
    argument.type_id = "org.seekdb.gis.geometry";
    argument.data = bytes.data();
    argument.data_size = bytes.size();
    sink.emits = 0;
    return seekdb_gis_geometry_operation(SEEKDB_GIS_OP_MAKE_VALID, instance, &context, &argument, 1);
  };
  check("repair rejects collapsed nonpolygon", repair_call(zero_line), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  check("failed nonpolygon repair emits nothing", sink.emits, 0);
  check("repair does not recursively dissolve collection members", repair_call(overlapping_collection), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  check("failed collection repair emits nothing", sink.emits, 0);
  const auto surface_call = [&](const Geometry &geometry) {
    std::vector<uint8_t> bytes;
    encode(geometry, bytes);
    seekdb_plugin_execution_value_v1_t argument = {};
    argument.struct_size = sizeof(argument);
    argument.type_id = "org.seekdb.gis.geometry";
    argument.data = bytes.data();
    argument.data_size = bytes.size();
    sink.emits = 0;
    return seekdb_gis_centroid_operation(1, instance, &context, &argument, 1);
  };
  auto overflow_points = nearest;
  overflow_points.children.resize(2);
  overflow_points.children[0].points = {{-1e200, 0}};
  overflow_points.children[1].points = {{1e200, 0}};
  check("surface distance overflow is an error", surface_call(overflow_points), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  check("surface overflow emits no fabricated empty", sink.emits, 0);
  auto unclosed_surface = triangle;
  unclosed_surface.rings[0].back() = {1, 1};
  check("surface rejects unclosed ring", surface_call(unclosed_surface), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  check("surface invalid ring emits nothing", sink.emits, 0);
  const auto call = [&](const Geometry &a, const Geometry &b, bool overlay, bool metric) {
    std::vector<uint8_t> bytes[2];
    encode(a, bytes[0]); encode(b, bytes[1]);
    seekdb_plugin_execution_value_v1_t arguments[2] = {};
    for (int i = 0; i < 2; ++i) {
      arguments[i].struct_size = sizeof(arguments[i]);
      arguments[i].type_id = "org.seekdb.gis.geometry";
      arguments[i].data = bytes[i].data();
      arguments[i].data_size = bytes[i].size();
    }
    sink.emits = 0;
    if (overlay) return seekdb_gis_geometry_operation(SEEKDB_GIS_OP_UNION, instance, &context, arguments, 2);
    if (metric) return seekdb_gis_metric_operation(SEEKDB_GIS_METRIC_DISTANCE, instance, &context, arguments, 2);
    return seekdb_gis_relation_operation(SEEKDB_GIS_REL_INTERSECTS, instance, &context, arguments, 2);
  };
  const auto reject = [&](const char *name, const Geometry &a, const Geometry &b, bool overlay = false, bool metric = false) {
    check(name, call(a, b, overlay, metric), SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
    check("rejected input emits nothing", sink.emits, 0);
  };
  auto geographic = point;
  geographic.srid = 4326;
  reject("unknown geographic strategy", geographic, geographic);
  reject("mixed SRID", geographic, point, false, true);
  auto point_z = point;
  point_z.dimensions = 3;
  reject("do not discard Z", point_z, point_z, false, true);
  reject("mixed dimension overlay unsupported", point, outer, true);
  reject("collection requires separate dispatch", empty_geometry(0), point);
  reject("empty distance not zero", Geometry{3}, point, false, true);
  auto invalid = triangle;
  invalid.rings = {{{0, 0}, {4, 4}, {0, 4}, {4, 0}, {0, 0}}};
  reject("invalid polygon rejected", invalid, outer, true);
  auto invalid_child = multi;
  invalid_child.children.front().srid = 4326;
  // Wire encoding does not embed child SRIDs; test adapter admission directly.
  bool rejected = false;
  try { (void)to_cartesian(invalid_child); } catch (const CartesianInputError &) { rejected = true; }
  check("nested mixed SRID rejected", rejected, 1);
  sink.status = SEEKDB_PLUGIN_STATUS_BUSY;
  check("best SRID emit failure preserved", best_call(geographic_point), SEEKDB_PLUGIN_STATUS_BUSY);
  check("best SRID failed emit is not retried", sink.emits, 1);
  check("GeoHash emit failure preserved", hash_call(hash_origin, 5), SEEKDB_PLUGIN_STATUS_BUSY);
  check("GeoHash failed emit is not retried", sink.emits, 1);
  check("MVT emit error preserved", mvt_call(point, tile_box, 10, 0, 1), SEEKDB_PLUGIN_STATUS_BUSY);
  check("MVT failed emit is not retried", sink.emits, 1);
  check("clip emit error preserved", clip_call(triangle, unit_box), SEEKDB_PLUGIN_STATUS_BUSY);
  check("clip failed emit is not retried", sink.emits, 1);
  check("repair emit error preserved", repair_call(bowtie), SEEKDB_PLUGIN_STATUS_BUSY);
  check("repair failed emit is not retried", sink.emits, 1);
  check("validity emit error preserved", validity_call(bowtie), SEEKDB_PLUGIN_STATUS_BUSY);
  check("validity failed emit is not retried", sink.emits, 1);
  check("surface emit error preserved", surface_call(hole), SEEKDB_PLUGIN_STATUS_BUSY);
  check("surface failed emit is not retried", sink.emits, 1);
  check("emit error preserved", call(outer, inner, true, false), SEEKDB_PLUGIN_STATUS_BUSY);
  check("failed emit is not retried", sink.emits, 1);
  std::cout << "Topology mismatches: " << failures << '\n';
  return failures == 0 ? 0 : 1;
}
