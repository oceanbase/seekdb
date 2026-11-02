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

use crate::geometry::{Bounds, Coordinate, Envelope, Geometry, Primitive};
use crate::{Error, Mbr, Result};
use s2rst::r3::Vector;
use s2rst::s1::Angle;
use s2rst::s2::latlng_rect_bounder::LatLngRectBounder;
use s2rst::s2::polyline::Polyline;
use s2rst::s2::region_coverer::RegionCoverer;
use s2rst::s2::{
    Cap, Cell, CellId, CellUnion, LatLng, Level, LevelMod, Loop, Point, Polygon, Rect, Region,
};
use std::collections::BTreeSet;

pub const OUT_OF_BOUNDS_CELL_ID: u64 = u64::MAX;
const MAX_LEVEL: u8 = 30;
const INDEX_MAX_CELLS: usize = 4;
const QUERY_MAX_CELLS: usize = 50;
const EXPANSION_MAX_LEVEL_DIFF: u8 = 2;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CoveringKind {
    Index,
    Query,
    CoveredBy,
}

#[derive(Clone, Copy, Debug)]
pub struct CoveringConfig {
    pub kind: CoveringKind,
    pub geographic: bool,
    pub projected_bounds: Option<Bounds>,
    pub distance_radians: f64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct CellRange {
    pub start: u64,
    pub end: u64,
}

#[derive(Debug, Default)]
pub struct Covering {
    pub cells: Vec<u64>,
    pub ancestors: Vec<u64>,
    pub ranges: Vec<CellRange>,
    pub mbr: Mbr,
}

pub fn cover_swkb(input: &[u8], config: CoveringConfig) -> Result<Covering> {
    let geometry = Geometry::from_swkb(input)?;
    cover_geometry(geometry, config)
}

pub fn cover_geometry(geometry: Geometry, config: CoveringConfig) -> Result<Covering> {
    if !config.distance_radians.is_finite() || config.distance_radians < 0.0 {
        return Err(Error::InvalidArgument(
            "distance must be finite and nonnegative",
        ));
    }
    if config.kind != CoveringKind::Query && config.distance_radians != 0.0 {
        return Err(Error::InvalidArgument(
            "distance expansion is only valid for queries",
        ));
    }
    if config.geographic && config.projected_bounds.is_some() {
        return Err(Error::InvalidArgument(
            "geographic geometry cannot have projected bounds",
        ));
    }
    if !config.geographic && config.projected_bounds.is_none() {
        return Err(Error::InvalidArgument(
            "projected geometry requires SRS bounds",
        ));
    }
    let projection = config.projected_bounds.map(Projection::new).transpose()?;
    let exceeds_bounds = projection.is_some_and(|projection| {
        let outside = |point: &Coordinate| !projection.valid.contains(*point);
        geometry.primitives.iter().any(|primitive| match primitive {
            Primitive::Point(point) => outside(point),
            Primitive::Line(points) => points.iter().any(outside),
            Primitive::Polygon(rings) => rings.iter().flatten().any(outside),
        })
    });
    let raw_envelope = geometry.envelope;
    let is_point = geometry.is_point;
    let prepared = if let Some(projection) = projection.filter(|_| exceeds_bounds) {
        match raw_envelope.intersection(projection.valid) {
            Some(envelope) => prepare_envelope(envelope, Some(projection), config.kind),
            None => Prepared::empty(),
        }
    } else {
        prepare_primitives(
            geometry.primitives,
            config.geographic,
            projection,
            config.kind,
        )?
    };

    let mut output = Covering {
        mbr: if config.geographic {
            Mbr::from_rect(prepared.rect, is_point, true)
        } else {
            Mbr::from_envelope(raw_envelope, is_point, false)
        },
        ..Covering::default()
    };

    if config.kind == CoveringKind::CoveredBy {
        output.cells = normalized_ids(prepared.vertex_cells);
        if exceeds_bounds {
            output.cells.push(OUT_OF_BOUNDS_CELL_ID);
        }
        output.ancestors = unique_ancestors(&output.cells);
        return Ok(output);
    }

    let max_cells = match config.kind {
        CoveringKind::Index => INDEX_MAX_CELLS,
        CoveringKind::Query => QUERY_MAX_CELLS,
        CoveringKind::CoveredBy => unreachable!(),
    };
    let coverer = region_coverer(max_cells);
    let mut covering = cover_regions(&coverer, &prepared.regions);
    if is_full_range(&covering, config.geographic) {
        let margin = LatLng::from_degrees(0.00001, 0.00001);
        let rect = prepared.bounder.get_bound().expanded(margin);
        covering = CellUnion::from_cell_ids(coverer.covering(&rect).cell_ids().to_vec());
        if config.geographic {
            output.mbr = Mbr::from_rect(rect, is_point, true);
        }
    }
    if config.distance_radians > 0.0 {
        covering.expand_by_radius(
            Angle::from_radians(config.distance_radians),
            EXPANSION_MAX_LEVEL_DIFF,
        );
    }
    if config.geographic && !output.mbr.is_empty() {
        let mut rect = output.mbr.to_rect();
        if config.distance_radians > 0.0 {
            rect = rect.expanded_by_distance(Angle::from_radians(config.distance_radians));
        }
        rect = rect.expanded_by_distance(Angle::from_degrees(f64::EPSILON));
        output.mbr = Mbr::from_rect(rect, is_point, true);
    }

    output.cells = covering.cell_ids().iter().map(|cell| cell.id()).collect();
    if exceeds_bounds {
        output.cells.push(OUT_OF_BOUNDS_CELL_ID);
    }
    if config.kind == CoveringKind::Query {
        output.ancestors = unique_ancestors(&output.cells);
        if !is_point || config.distance_radians != 0.0 {
            output.ranges = output
                .cells
                .iter()
                .filter_map(|&id| {
                    let cell = CellId(id);
                    if cell.is_valid() {
                        Some(CellRange {
                            start: cell.range_min().id(),
                            end: cell.range_max().id(),
                        })
                    } else {
                        (id == OUT_OF_BOUNDS_CELL_ID).then_some(CellRange { start: id, end: id })
                    }
                })
                .collect();
        }
    }
    Ok(output)
}

#[derive(Clone, Copy)]
struct Projection {
    original: Bounds,
    valid: Bounds,
}

impl Projection {
    fn new(original: Bounds) -> Result<Self> {
        Ok(Self {
            original,
            valid: original.shrunk()?,
        })
    }
}

enum PreparedRegion {
    Point(Cell),
    Line(Polyline),
    GeographicLine(BoundedPolyline),
    Polygon(Polygon),
}

impl PreparedRegion {
    fn region(&self) -> &dyn Region {
        match self {
            Self::Point(cell) => cell,
            Self::Line(line) => line,
            Self::GeographicLine(line) => line,
            Self::Polygon(polygon) => polygon,
        }
    }
}

#[derive(Debug)]
struct BoundedPolyline {
    line: Polyline,
    rect: Rect,
}

impl Region for BoundedPolyline {
    fn cap_bound(&self) -> Cap {
        self.rect.cap_bound()
    }

    fn rect_bound(&self) -> Rect {
        self.rect
    }

    fn cell_union_bound(&self) -> Vec<CellId> {
        self.cap_bound().cell_union_bound()
    }

    fn contains_cell(&self, cell: &Cell) -> bool {
        self.line.contains_cell(cell)
    }

    fn intersects_cell(&self, cell: &Cell) -> bool {
        self.rect.intersects(cell.rect_bound()) && self.line.intersects_cell(cell)
    }

    fn contains_point(&self, point: &Point) -> bool {
        self.line.contains_point(point)
    }
}

struct Prepared {
    regions: Vec<PreparedRegion>,
    vertex_cells: Vec<CellId>,
    bounder: LatLngRectBounder,
    rect: Rect,
}

impl Prepared {
    fn empty() -> Self {
        Self {
            regions: Vec::new(),
            vertex_cells: Vec::new(),
            bounder: LatLngRectBounder::new(),
            rect: Rect::empty(),
        }
    }

    fn add_point(&mut self, point: Point, kind: CoveringKind) {
        if kind == CoveringKind::CoveredBy {
            self.vertex_cells
                .push(CellId::from_point(&point).parent_at_level(Level::new(MAX_LEVEL)));
        } else {
            self.bounder.add_point(point);
        }
    }

    fn add_rect(&mut self, rect: Rect) {
        self.rect = if self.rect.is_empty() {
            rect
        } else {
            self.rect.union(rect)
        };
    }
}

fn prepare_primitives(
    primitives: Vec<Primitive>,
    geographic: bool,
    projection: Option<Projection>,
    kind: CoveringKind,
) -> Result<Prepared> {
    let mut prepared = Prepared::empty();
    for primitive in primitives {
        match primitive {
            Primitive::Point(coordinate) => {
                let point = project_point(coordinate, geographic, projection)?;
                prepared.add_point(point, kind);
                prepared.add_rect(if geographic {
                    Rect::from_lat_lng(
                        LatLng::from_degrees(coordinate.y, coordinate.x).normalized(),
                    )
                } else {
                    Rect::empty()
                });
                prepared
                    .regions
                    .push(PreparedRegion::Point(Cell::from_point(point)));
            }
            Primitive::Line(coordinates) => {
                let points = project_points(coordinates, geographic, projection)?;
                for &point in &points {
                    prepared.add_point(point, kind);
                }
                let line = Polyline::new(points);
                if geographic {
                    let rect = line.rect_bound();
                    prepared.add_rect(rect);
                    prepared
                        .regions
                        .push(PreparedRegion::GeographicLine(BoundedPolyline {
                            line,
                            rect,
                        }));
                } else {
                    prepared.regions.push(PreparedRegion::Line(line));
                }
            }
            Primitive::Polygon(rings) => {
                let mut loops = Vec::with_capacity(rings.len());
                for coordinates in rings {
                    if coordinates.is_empty() {
                        continue;
                    }
                    let points = project_points(coordinates, geographic, projection)?;
                    for &point in &points {
                        prepared.add_point(point, kind);
                    }
                    let mut loop_ = Loop::new(points);
                    loop_.normalize();
                    loops.push(loop_);
                }
                let polygon = Polygon::from_loops(loops);
                if geographic {
                    prepared.add_rect(polygon.rect_bound());
                }
                prepared.regions.push(PreparedRegion::Polygon(polygon));
            }
        }
    }
    Ok(prepared)
}

fn prepare_envelope(
    envelope: Envelope,
    projection: Option<Projection>,
    kind: CoveringKind,
) -> Prepared {
    let primitives = if envelope.min_x == envelope.max_x && envelope.min_y == envelope.max_y {
        vec![Primitive::Point(Coordinate {
            x: envelope.min_x,
            y: envelope.min_y,
        })]
    } else if envelope.min_x == envelope.max_x || envelope.min_y == envelope.max_y {
        vec![Primitive::Line(vec![
            Coordinate {
                x: envelope.min_x,
                y: envelope.min_y,
            },
            Coordinate {
                x: envelope.max_x,
                y: envelope.max_y,
            },
        ])]
    } else {
        vec![Primitive::Polygon(vec![vec![
            Coordinate {
                x: envelope.min_x,
                y: envelope.min_y,
            },
            Coordinate {
                x: envelope.max_x,
                y: envelope.min_y,
            },
            Coordinate {
                x: envelope.max_x,
                y: envelope.max_y,
            },
            Coordinate {
                x: envelope.min_x,
                y: envelope.max_y,
            },
            Coordinate {
                x: envelope.min_x,
                y: envelope.min_y,
            },
        ]])]
    };
    prepare_primitives(primitives, false, projection, kind)
        .expect("clipped coordinates are in bounds")
}

fn project_points(
    coordinates: Vec<Coordinate>,
    geographic: bool,
    projection: Option<Projection>,
) -> Result<Vec<Point>> {
    coordinates
        .into_iter()
        .map(|coordinate| project_point(coordinate, geographic, projection))
        .collect()
}

fn project_point(
    coordinate: Coordinate,
    geographic: bool,
    projection: Option<Projection>,
) -> Result<Point> {
    if geographic {
        let latlng = LatLng::from_degrees(coordinate.y, coordinate.x);
        if !latlng.is_valid() {
            return Err(Error::InvalidWkb("invalid geographic coordinate"));
        }
        Ok(latlng.normalized().to_point())
    } else {
        let projection = projection.expect("projected bounds were validated");
        if !projection.valid.contains(coordinate) {
            return Err(Error::InvalidArgument(
                "coordinate exceeds projected SRS bounds",
            ));
        }
        let bounds = projection.original;
        let s = (coordinate.x - bounds.min_x) / (bounds.max_x - bounds.min_x);
        let t = (coordinate.y - bounds.min_y) / (bounds.max_y - bounds.min_y);
        Ok(Point::new(Vector {
            x: 1.0,
            y: st_to_uv(s),
            z: st_to_uv(t),
        }))
    }
}

fn st_to_uv(value: f64) -> f64 {
    if value >= 0.5 {
        (4.0 * value * value - 1.0) / 3.0
    } else {
        (1.0 - 4.0 * (1.0 - value) * (1.0 - value)) / 3.0
    }
}

fn region_coverer(max_cells: usize) -> RegionCoverer {
    RegionCoverer::new()
        .max_cells(max_cells)
        .max_level(Level::new(MAX_LEVEL))
        .level_mod(LevelMod::new(1))
}

fn cover_regions(coverer: &RegionCoverer, regions: &[PreparedRegion]) -> CellUnion {
    let mut cells = Vec::new();
    for region in regions {
        cells.extend_from_slice(coverer.covering(region.region()).cell_ids());
    }
    CellUnion::from_cell_ids(cells)
}

fn is_full_range(covering: &CellUnion, geographic: bool) -> bool {
    if geographic {
        covering.len() == 6
            && covering
                .cell_ids()
                .iter()
                .all(|cell| cell.level() == Level::new(0))
    } else {
        covering
            .cell_ids()
            .iter()
            .any(|cell| cell.face().as_u8() != 0)
    }
}

fn normalized_ids(cells: Vec<CellId>) -> Vec<u64> {
    CellUnion::from_cell_ids(cells)
        .cell_ids()
        .iter()
        .map(|cell| cell.id())
        .collect()
}

fn unique_ancestors(cells: &[u64]) -> Vec<u64> {
    let base: BTreeSet<u64> = cells.iter().copied().collect();
    let mut ancestors = BTreeSet::new();
    for &id in cells {
        let cell = CellId(id);
        if !cell.is_valid() {
            continue;
        }
        for level in (0..cell.level().as_u8()).rev() {
            let ancestor = cell.parent_at_level(Level::new(level)).id();
            if !base.contains(&ancestor) {
                ancestors.insert(ancestor);
            }
        }
    }
    ancestors.into_iter().collect()
}
