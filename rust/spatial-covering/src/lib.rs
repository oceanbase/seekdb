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

//! Native spatial covering, cell hierarchy, and MBR semantics backed by s2rst.
//! The CXX bridge is only a representation adapter around these Rust APIs.

mod bounds;
mod bridge;
mod covering;
mod geometry;

pub use bounds::{MbrRelation, filter as filter_mbr};
pub use covering::{
    CellRange, Covering, CoveringConfig, CoveringKind, OUT_OF_BOUNDS_CELL_ID, cover_geometry,
    cover_swkb,
};
pub use geometry::{Bounds, Coordinate, Envelope, Geometry, Primitive};
use s2rst::s2::{LatLng, Rect};
use std::panic::catch_unwind;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Mbr {
    pub x_min: f64,
    pub x_max: f64,
    pub y_min: f64,
    pub y_max: f64,
    pub is_point: bool,
    pub geographic: bool,
}

impl Mbr {
    pub fn empty() -> Self {
        Self {
            x_min: f64::NAN,
            x_max: f64::NAN,
            y_min: f64::NAN,
            y_max: f64::NAN,
            is_point: false,
            geographic: false,
        }
    }

    pub fn is_empty(self) -> bool {
        self.x_min.is_nan() && self.x_max.is_nan() && self.y_min.is_nan() && self.y_max.is_nan()
    }

    fn to_rect(self) -> Rect {
        let lo = LatLng::from_degrees(self.y_min, self.x_min);
        let hi = LatLng::from_degrees(self.y_max, self.x_max);
        Rect::new(
            s2rst::r1::Interval::new(lo.lat.radians(), hi.lat.radians()),
            s2rst::s1::Interval::new(lo.lng.radians(), hi.lng.radians()),
        )
    }

    fn from_envelope(envelope: Envelope, is_point: bool, geographic: bool) -> Self {
        Self {
            x_min: envelope.min_x,
            x_max: envelope.max_x,
            y_min: envelope.min_y,
            y_max: envelope.max_y,
            is_point,
            geographic,
        }
    }

    fn from_rect(rect: Rect, is_point: bool, geographic: bool) -> Self {
        if rect.is_empty() {
            return Self {
                is_point,
                geographic,
                ..Self::empty()
            };
        }
        Self {
            x_min: rect.lo().lng.degrees(),
            x_max: rect.hi().lng.degrees(),
            y_min: rect.lo().lat.degrees(),
            y_max: rect.hi().lat.degrees(),
            is_point,
            geographic,
        }
    }
}

impl Default for Mbr {
    fn default() -> Self {
        Self::empty()
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    InvalidWkb(&'static str),
    InvalidArgument(&'static str),
    Unsupported(&'static str),
    Internal(&'static str),
}

impl Error {
    pub fn message(self) -> &'static str {
        match self {
            Self::InvalidWkb(message)
            | Self::InvalidArgument(message)
            | Self::Unsupported(message)
            | Self::Internal(message) => message,
        }
    }
}

pub type Result<T> = std::result::Result<T, Error>;

fn spatial_cover(
    wkb: &[u8],
    geographic: bool,
    has_projected_bounds: bool,
    projected_bounds: bridge::ffi::Bounds,
    kind: bridge::ffi::CoveringKind,
    distance_radians: f64,
) -> bridge::ffi::CoveringResult {
    let result = catch_unwind(|| {
        let kind = match kind {
            bridge::ffi::CoveringKind::Index => CoveringKind::Index,
            bridge::ffi::CoveringKind::Query => CoveringKind::Query,
            bridge::ffi::CoveringKind::CoveredBy => CoveringKind::CoveredBy,
            _ => return Err(Error::InvalidArgument("unknown covering kind")),
        };
        let projected_bounds = has_projected_bounds.then_some(Bounds {
            min_x: projected_bounds.min_x,
            max_x: projected_bounds.max_x,
            min_y: projected_bounds.min_y,
            max_y: projected_bounds.max_y,
        });
        cover_swkb(
            wkb,
            CoveringConfig {
                kind,
                geographic,
                projected_bounds,
                distance_radians,
            },
        )
    });
    match result {
        Ok(Ok(covering)) => bridge::ffi::CoveringResult {
            code: bridge::ffi::ErrorCode::None,
            message: String::new(),
            cells: covering.cells,
            ancestors: covering.ancestors,
            ranges: covering
                .ranges
                .into_iter()
                .map(|range| bridge::ffi::CellRange {
                    start: range.start,
                    end: range.end,
                })
                .collect(),
            mbr: bridge_mbr(covering.mbr),
        },
        Ok(Err(error)) => bridge_covering_error(error),
        Err(_) => bridge_covering_error(Error::Internal("spatial covering panicked")),
    }
}

fn spatial_filter_mbr(
    index: bridge::ffi::Mbr,
    query: bridge::ffi::Mbr,
    relation: bridge::ffi::MbrRelation,
) -> bridge::ffi::MbrFilterResult {
    let result = catch_unwind(|| {
        let relation = match relation {
            bridge::ffi::MbrRelation::Covers => MbrRelation::Covers,
            bridge::ffi::MbrRelation::Intersects => MbrRelation::Intersects,
            bridge::ffi::MbrRelation::CoveredBy => MbrRelation::CoveredBy,
            bridge::ffi::MbrRelation::DWithin => MbrRelation::DWithin,
            bridge::ffi::MbrRelation::DFullyWithin => MbrRelation::DFullyWithin,
            _ => return Err(Error::InvalidArgument("unknown MBR relation")),
        };
        filter_mbr(native_mbr(index), native_mbr(query), relation)
    });
    match result {
        Ok(Ok(pass_through)) => bridge::ffi::MbrFilterResult {
            code: bridge::ffi::ErrorCode::None,
            message: String::new(),
            pass_through,
        },
        Ok(Err(error)) => bridge_filter_error(error),
        Err(_) => bridge_filter_error(Error::Internal("MBR filtering panicked")),
    }
}

fn native_mbr(mbr: bridge::ffi::Mbr) -> Mbr {
    Mbr {
        x_min: mbr.x_min,
        x_max: mbr.x_max,
        y_min: mbr.y_min,
        y_max: mbr.y_max,
        is_point: mbr.is_point,
        geographic: mbr.geographic,
    }
}

fn bridge_mbr(mbr: Mbr) -> bridge::ffi::Mbr {
    bridge::ffi::Mbr {
        x_min: mbr.x_min,
        x_max: mbr.x_max,
        y_min: mbr.y_min,
        y_max: mbr.y_max,
        is_point: mbr.is_point,
        geographic: mbr.geographic,
    }
}

fn error_code(error: Error) -> bridge::ffi::ErrorCode {
    match error {
        Error::InvalidWkb(_) => bridge::ffi::ErrorCode::InvalidWkb,
        Error::InvalidArgument(_) => bridge::ffi::ErrorCode::InvalidArgument,
        Error::Unsupported(_) => bridge::ffi::ErrorCode::Unsupported,
        Error::Internal(_) => bridge::ffi::ErrorCode::Internal,
    }
}

fn bridge_covering_error(error: Error) -> bridge::ffi::CoveringResult {
    bridge::ffi::CoveringResult {
        code: error_code(error),
        message: error.message().to_string(),
        cells: Vec::new(),
        ancestors: Vec::new(),
        ranges: Vec::new(),
        mbr: bridge_mbr(Mbr::empty()),
    }
}

fn bridge_filter_error(error: Error) -> bridge::ffi::MbrFilterResult {
    bridge::ffi::MbrFilterResult {
        code: error_code(error),
        message: error.message().to_string(),
        pass_through: true,
    }
}

#[cfg(test)]
mod tests;
