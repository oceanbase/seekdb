// Copyright (c) 2026 OceanBase.
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

use crate::*;
use s2rst::r3::Vector;
use s2rst::s2::{CellId, CellUnion, LatLng, Level, Point};

const GEOGRAPHIC: CoveringConfig = CoveringConfig {
    kind: CoveringKind::Index,
    geographic: true,
    projected_bounds: None,
    distance_radians: 0.0,
};

fn projected(kind: CoveringKind) -> CoveringConfig {
    CoveringConfig {
        kind,
        geographic: false,
        projected_bounds: Some(Bounds {
            min_x: 0.0,
            max_x: 100.0,
            min_y: 0.0,
            max_y: 100.0,
        }),
        distance_radians: 0.0,
    }
}

fn with_kind(mut config: CoveringConfig, kind: CoveringKind) -> CoveringConfig {
    config.kind = kind;
    config
}

fn header(kind: u32) -> Vec<u8> {
    let mut bytes = vec![0, 0, 0, 0, 1];
    bytes.extend_from_slice(&kind.to_le_bytes());
    bytes
}

fn push_point(bytes: &mut Vec<u8>, x: f64, y: f64) {
    bytes.extend_from_slice(&x.to_le_bytes());
    bytes.extend_from_slice(&y.to_le_bytes());
}

fn point(x: f64, y: f64) -> Vec<u8> {
    let mut bytes = header(1);
    push_point(&mut bytes, x, y);
    bytes
}

fn point_z(x: f64, y: f64, z: f64) -> Vec<u8> {
    let mut bytes = header(1001);
    push_point(&mut bytes, x, y);
    bytes.extend_from_slice(&z.to_le_bytes());
    bytes
}

fn line(points: &[(f64, f64)]) -> Vec<u8> {
    let mut bytes = header(2);
    bytes.extend_from_slice(&(points.len() as u32).to_le_bytes());
    for &(x, y) in points {
        push_point(&mut bytes, x, y);
    }
    bytes
}

fn polygon(rings: &[&[(f64, f64)]]) -> Vec<u8> {
    let mut bytes = header(3);
    bytes.extend_from_slice(&(rings.len() as u32).to_le_bytes());
    for ring in rings {
        bytes.extend_from_slice(&(ring.len() as u32).to_le_bytes());
        for &(x, y) in *ring {
            push_point(&mut bytes, x, y);
        }
    }
    bytes
}

fn collection(members: &[Vec<u8>]) -> Vec<u8> {
    let mut bytes = header(7);
    bytes.extend_from_slice(&(members.len() as u32).to_le_bytes());
    for member in members {
        bytes.extend_from_slice(&member[4..]);
    }
    bytes
}

fn union(ids: &[u64]) -> CellUnion {
    CellUnion::from_cell_ids(
        ids.iter()
            .copied()
            .filter(|&id| id != OUT_OF_BOUNDS_CELL_ID)
            .map(CellId)
            .collect(),
    )
}

fn query_finds_index(index: &Covering, query: &Covering) -> bool {
    index.cells.iter().any(|cell| {
        query.ancestors.contains(cell)
            || (query.ranges.is_empty() && query.cells.contains(cell))
            || query
                .ranges
                .iter()
                .any(|range| range.start <= *cell && *cell <= range.end)
    })
}

fn rectangle(min_x: f64, min_y: f64, max_x: f64, max_y: f64) -> Vec<u8> {
    polygon(&[&[
        (min_x, min_y),
        (max_x, min_y),
        (max_x, max_y),
        (min_x, max_y),
        (min_x, min_y),
    ]])
}

#[test]
fn point_covering_has_hierarchy_and_exact_mbr() {
    let wkb = point(30.0, 40.0);
    let index = cover_swkb(&wkb, GEOGRAPHIC).unwrap();
    let expected = CellId::from_point(&LatLng::from_degrees(40.0, 30.0).to_point())
        .parent_at_level(Level::new(30))
        .id();
    assert_eq!(index.cells, vec![expected]);
    assert!(index.ancestors.is_empty());
    assert!(index.ranges.is_empty());
    assert!((index.mbr.x_min - 30.0).abs() < 1e-12);
    assert!((index.mbr.y_max - 40.0).abs() < 1e-12);
    assert!(index.mbr.is_point);

    let query = cover_swkb(&wkb, with_kind(GEOGRAPHIC, CoveringKind::Query)).unwrap();
    assert_eq!(query.cells, vec![expected]);
    assert_eq!(query.ancestors.len(), 30);
    assert!(query.ranges.is_empty());
    for &ancestor in &query.ancestors {
        assert!(CellId(ancestor).is_valid());
        assert!(CellId(ancestor).level() < Level::new(30));
    }
}

#[test]
fn lines_polygons_and_holes_cover_their_vertices() {
    let geometries = [
        (
            line(&[(0.0, 0.0), (20.0, 20.0), (40.0, 20.0)]),
            &[(0.0, 0.0), (20.0, 20.0), (40.0, 20.0)][..],
        ),
        (
            polygon(&[
                &[
                    (0.0, 0.0),
                    (40.0, 0.0),
                    (40.0, 40.0),
                    (0.0, 40.0),
                    (0.0, 0.0),
                ],
                &[
                    (10.0, 10.0),
                    (10.0, 20.0),
                    (20.0, 20.0),
                    (20.0, 10.0),
                    (10.0, 10.0),
                ],
            ]),
            &[
                (0.0, 0.0),
                (40.0, 0.0),
                (0.0, 40.0),
                (10.0, 10.0),
                (20.0, 10.0),
            ][..],
        ),
    ];
    for (geometry, witness) in geometries {
        let covering = cover_swkb(&geometry, GEOGRAPHIC).unwrap();
        assert!(!covering.cells.is_empty());
        let cells = union(&covering.cells);
        for &(x, y) in witness {
            let point = LatLng::from_degrees(y, x).to_point();
            if covering
                .cells
                .iter()
                .any(|&id| CellId(id) == CellId::from_point(&point).parent_at_level(Level::new(30)))
                || cells.contains_point(point)
            {
                continue;
            }
            panic!("covering lost witness point ({x}, {y})");
        }
    }
}

#[test]
fn geographic_lines_cover_geodesic_samples() {
    let lines = [
        vec![(-120.0, 0.0), (0.0, 0.0), (120.0, 0.0)],
        vec![(179.0, 10.0), (-179.0, 10.0)],
        vec![(45.0, 85.0), (-135.0, 85.0)],
        vec![(0.0, 0.0), (0.0, 0.0)],
        (0..32)
            .map(|i| {
                (
                    -20.0 + i as f64 * 0.005,
                    -20.0 + (i as f64 * 0.4).sin() * 0.04,
                )
            })
            .collect(),
    ];
    for coordinates in lines {
        let wkb = line(&coordinates);
        for kind in [CoveringKind::Index, CoveringKind::Query] {
            let covering = cover_swkb(&wkb, with_kind(GEOGRAPHIC, kind)).unwrap();
            for edge in coordinates.windows(2) {
                let a = LatLng::from_degrees(edge[0].1, edge[0].0).to_point();
                let b = LatLng::from_degrees(edge[1].1, edge[1].0).to_point();
                for step in 0..=8 {
                    let sample = s2rst::s2::edge_distances::interpolate(step as f64 / 8.0, a, b);
                    assert!(
                        covering.cells.iter().any(|&id| {
                            s2rst::s2::Cell::from_cell_id(CellId(id)).contains_point(sample)
                        }),
                        "lost geodesic sample {sample:?}"
                    );
                    assert!(covering.mbr.to_rect().contains_point(sample));
                }
            }
        }
    }
}

#[test]
fn covered_by_uses_vertices_without_region_covering() {
    let wkb = polygon(&[&[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0), (0.0, 0.0)]]);
    let covering = cover_swkb(&wkb, with_kind(GEOGRAPHIC, CoveringKind::CoveredBy)).unwrap();
    assert!(!covering.cells.is_empty());
    assert!(covering.cells.iter().all(|&id| CellId(id).is_leaf()));
    assert!(covering.cells.len() <= 4);
    assert!(!covering.ancestors.is_empty());
    assert!(covering.ranges.is_empty());
}

#[test]
fn projection_matches_face_zero_quadratic_mapping() {
    let coordinate = (25.0, 75.0);
    let covering = cover_swkb(
        &point(coordinate.0, coordinate.1),
        projected(CoveringKind::Index),
    )
    .unwrap();
    let expected_point = Point::new(Vector {
        x: 1.0,
        y: -5.0 / 12.0,
        z: 5.0 / 12.0,
    });
    let expected = CellId::from_point(&expected_point)
        .parent_at_level(Level::new(30))
        .id();
    assert_eq!(covering.cells, vec![expected]);
    assert_eq!(covering.mbr.x_min, 25.0);
    assert_eq!(covering.mbr.y_max, 75.0);
}

#[test]
fn out_of_bounds_geometry_keeps_sentinel_and_original_mbr() {
    let wkb = line(&[(50.0, 50.0), (150.0, 50.0)]);
    let covering = cover_swkb(&wkb, projected(CoveringKind::Query)).unwrap();
    assert_eq!(covering.cells.last(), Some(&OUT_OF_BOUNDS_CELL_ID));
    assert!(covering.cells.len() > 1);
    assert!(covering.ranges.contains(&CellRange {
        start: OUT_OF_BOUNDS_CELL_ID,
        end: OUT_OF_BOUNDS_CELL_ID
    }));
    assert_eq!(covering.mbr.x_min, 50.0);
    assert_eq!(covering.mbr.x_max, 150.0);

    let disjoint = cover_swkb(&point(150.0, 50.0), projected(CoveringKind::Index)).unwrap();
    assert_eq!(disjoint.cells, vec![OUT_OF_BOUNDS_CELL_ID]);
    assert_eq!(disjoint.mbr.x_min, 150.0);

    let index = cover_swkb(&wkb, projected(CoveringKind::Index)).unwrap();
    assert!(query_finds_index(&index, &covering));
}

#[test]
fn query_ranges_find_intersecting_generated_rectangles() {
    let mut random = 0x5eed_c0de_d15c_a11eu64;
    let mut next = move || {
        random = random
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        (random >> 32) as f64 / u32::MAX as f64
    };
    for geographic in [false, true] {
        for _ in 0..64 {
            let min_x = 10.0 + 60.0 * next();
            let min_y = 10.0 + 60.0 * next();
            let width = 0.01 + 4.0 * next();
            let height = 0.01 + 4.0 * next();
            let query_x = min_x - width + 2.0 * width * next();
            let query_y = min_y - height + 2.0 * height * next();
            let index_wkb = rectangle(min_x, min_y, min_x + width, min_y + height);
            let query_wkb = rectangle(query_x, query_y, query_x + width, query_y + height);
            let base = if geographic {
                GEOGRAPHIC
            } else {
                projected(CoveringKind::Index)
            };
            let index = cover_swkb(&index_wkb, base).unwrap();
            let query = cover_swkb(&query_wkb, with_kind(base, CoveringKind::Query)).unwrap();
            assert!(
                query_finds_index(&index, &query),
                "lost intersecting rectangle: geographic={geographic}, index={index_wkb:?}, query={query_wkb:?}"
            );
        }
    }
}

#[test]
fn z_coordinates_do_not_change_covering() {
    let two_d = cover_swkb(&point(12.0, 34.0), GEOGRAPHIC).unwrap();
    let three_d = cover_swkb(&point_z(12.0, 34.0, 999.0), GEOGRAPHIC).unwrap();
    assert_eq!(two_d.cells, three_d.cells);
    assert_eq!(two_d.mbr, three_d.mbr);
}

#[test]
fn empty_and_invalid_geometries_are_distinguished() {
    let empty = cover_swkb(&collection(&[]), GEOGRAPHIC).unwrap();
    assert!(empty.cells.is_empty());
    assert!(empty.mbr.is_empty());

    let empty_polygon = polygon(&[&[]]);
    assert!(cover_swkb(&empty_polygon, GEOGRAPHIC).is_ok());
    assert!(cover_swkb(&point(f64::NAN, 1.0), GEOGRAPHIC).is_err());
    assert!(cover_swkb(&point(1.0, 91.0), GEOGRAPHIC).is_err());
    assert!(cover_swkb(&polygon(&[&[(0.0, 0.0), (1.0, 1.0)]]), GEOGRAPHIC).is_err());
    assert!(cover_swkb(&point(1.0, 2.0)[..12], GEOGRAPHIC).is_err());
}

#[test]
fn distance_expansion_returns_leaf_ranges() {
    let config = CoveringConfig {
        kind: CoveringKind::Query,
        distance_radians: 0.001,
        ..GEOGRAPHIC
    };
    let covering = cover_swkb(&point(0.0, 0.0), config).unwrap();
    assert!(!covering.ranges.is_empty());
    for range in covering.ranges {
        assert!(CellId(range.start).is_leaf());
        assert!(CellId(range.end).is_leaf());
        assert!(range.start <= range.end);
    }
}

#[test]
fn mbr_filter_handles_antimeridian_and_planar_bounds() {
    let index = Mbr {
        x_min: 179.0,
        x_max: -179.0,
        y_min: -1.0,
        y_max: 1.0,
        is_point: false,
        geographic: true,
    };
    let intersecting = Mbr {
        x_min: 178.0,
        x_max: -178.0,
        y_min: -2.0,
        y_max: 2.0,
        ..index
    };
    assert!(!filter_mbr(index, intersecting, MbrRelation::Intersects).unwrap());
    let disjoint = Mbr {
        x_min: -10.0,
        x_max: 10.0,
        ..intersecting
    };
    assert!(filter_mbr(index, disjoint, MbrRelation::Intersects).unwrap());
    assert!(filter_mbr(index, index, MbrRelation::DFullyWithin).is_err());
}

#[test]
fn geographic_mbr_preserves_wide_and_full_longitude_intervals() {
    let origin = Mbr {
        x_min: 0.0,
        x_max: 0.0,
        y_min: 0.0,
        y_max: 0.0,
        is_point: true,
        geographic: true,
    };
    for (x_min, x_max) in [(-170.0, 170.0), (-180.0, 180.0)] {
        let enclosing = Mbr {
            x_min,
            x_max,
            y_min: -10.0,
            y_max: 10.0,
            is_point: false,
            ..origin
        };
        assert!(!filter_mbr(origin, enclosing, MbrRelation::Intersects).unwrap());
        assert!(!filter_mbr(origin, enclosing, MbrRelation::Covers).unwrap());
        assert!(!filter_mbr(enclosing, origin, MbrRelation::CoveredBy).unwrap());
    }
    let wkb = line(&[(-120.0, 0.0), (0.0, 0.0), (120.0, 0.0)]);
    for kind in [CoveringKind::Index, CoveringKind::Query] {
        let covering = cover_swkb(&wkb, with_kind(GEOGRAPHIC, kind)).unwrap();
        assert!(!filter_mbr(origin, covering.mbr, MbrRelation::Intersects).unwrap());
    }
}

#[test]
fn big_endian_and_nested_collections_are_supported() {
    let mut big_endian = vec![0, 0, 0, 0, 0];
    big_endian.extend_from_slice(&1u32.to_be_bytes());
    big_endian.extend_from_slice(&12.0f64.to_be_bytes());
    big_endian.extend_from_slice(&34.0f64.to_be_bytes());
    let little_endian = point(12.0, 34.0);
    assert_eq!(
        cover_swkb(&big_endian, GEOGRAPHIC).unwrap().cells,
        cover_swkb(&little_endian, GEOGRAPHIC).unwrap().cells
    );
    let nested = collection(&[
        collection(&[point(1.0, 2.0)]),
        line(&[(3.0, 4.0), (5.0, 6.0)]),
    ]);
    assert!(!cover_swkb(&nested, GEOGRAPHIC).unwrap().cells.is_empty());
}
