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

use crate::Error;

const SRID_BYTES: usize = 4;
const VERSION_BYTE: u8 = 0x40;
const VERSION_MASK: u8 = 0x3f;
const SUPPORTED_VERSION: u8 = 1;
const MAX_NESTING_DEPTH: usize = 64;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Coordinate {
    pub x: f64,
    pub y: f64,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Bounds {
    pub min_x: f64,
    pub max_x: f64,
    pub min_y: f64,
    pub max_y: f64,
}

impl Bounds {
    pub fn shrunk(self) -> Result<Self, Error> {
        if !self.min_x.is_finite()
            || !self.max_x.is_finite()
            || !self.min_y.is_finite()
            || !self.max_y.is_finite()
            || self.min_x >= self.max_x
            || self.min_y >= self.max_y
        {
            return Err(Error::InvalidArgument("invalid projected SRS bounds"));
        }
        let delta_x = 0.01 * (self.max_x - self.min_x);
        let delta_y = 0.01 * (self.max_y - self.min_y);
        let shrunk = Self {
            min_x: self.min_x + delta_x,
            max_x: self.max_x - delta_x,
            min_y: self.min_y + delta_y,
            max_y: self.max_y - delta_y,
        };
        if shrunk.min_x > shrunk.max_x || shrunk.min_y > shrunk.max_y {
            return Err(Error::InvalidArgument("empty projected SRS bounds"));
        }
        Ok(shrunk)
    }

    pub fn contains(self, point: Coordinate) -> bool {
        point.x >= self.min_x
            && point.x <= self.max_x
            && point.y >= self.min_y
            && point.y <= self.max_y
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Envelope {
    pub min_x: f64,
    pub max_x: f64,
    pub min_y: f64,
    pub max_y: f64,
}

impl Envelope {
    pub fn empty() -> Self {
        Self {
            min_x: f64::NAN,
            max_x: f64::NAN,
            min_y: f64::NAN,
            max_y: f64::NAN,
        }
    }

    pub fn is_empty(self) -> bool {
        self.min_x.is_nan()
    }

    pub fn add(&mut self, point: Coordinate) {
        if self.is_empty() {
            *self = Self {
                min_x: point.x,
                max_x: point.x,
                min_y: point.y,
                max_y: point.y,
            };
        } else {
            self.min_x = self.min_x.min(point.x);
            self.max_x = self.max_x.max(point.x);
            self.min_y = self.min_y.min(point.y);
            self.max_y = self.max_y.max(point.y);
        }
    }

    pub fn intersection(self, bounds: Bounds) -> Option<Self> {
        if self.is_empty() {
            return None;
        }
        let intersection = Self {
            min_x: self.min_x.max(bounds.min_x),
            max_x: self.max_x.min(bounds.max_x),
            min_y: self.min_y.max(bounds.min_y),
            max_y: self.max_y.min(bounds.max_y),
        };
        (intersection.min_x <= intersection.max_x && intersection.min_y <= intersection.max_y)
            .then_some(intersection)
    }
}

#[derive(Debug)]
pub enum Primitive {
    Point(Coordinate),
    Line(Vec<Coordinate>),
    Polygon(Vec<Vec<Coordinate>>),
}

#[derive(Debug)]
pub struct Geometry {
    pub is_point: bool,
    pub envelope: Envelope,
    pub primitives: Vec<Primitive>,
}

impl Geometry {
    pub fn from_swkb(input: &[u8]) -> Result<Self, Error> {
        let wkb = strip_spatial_header(input)?;
        let mut parser = Parser {
            input: wkb,
            position: 0,
            envelope: Envelope::empty(),
        };
        let (base_type, primitives) = parser.parse_geometry(0)?;
        if parser.position != wkb.len() {
            return Err(Error::InvalidWkb("trailing bytes after geometry"));
        }
        Ok(Self {
            is_point: base_type == 1,
            envelope: parser.envelope,
            primitives,
        })
    }
}

fn strip_spatial_header(input: &[u8]) -> Result<&[u8], Error> {
    if input.len() < SRID_BYTES + 5 {
        return Err(Error::InvalidWkb("spatial WKB header is truncated"));
    }
    let marker = input[SRID_BYTES];
    let offset = if marker & VERSION_BYTE != 0 {
        if marker & VERSION_MASK != SUPPORTED_VERSION {
            return Err(Error::InvalidWkb("unsupported spatial WKB version"));
        }
        SRID_BYTES + 1
    } else {
        SRID_BYTES
    };
    if input.len() < offset + 5 {
        return Err(Error::InvalidWkb("WKB geometry header is truncated"));
    }
    Ok(&input[offset..])
}

struct Parser<'a> {
    input: &'a [u8],
    position: usize,
    envelope: Envelope,
}

#[derive(Clone, Copy)]
enum ByteOrder {
    Little,
    Big,
}

impl Parser<'_> {
    fn parse_geometry(&mut self, depth: usize) -> Result<(u32, Vec<Primitive>), Error> {
        if depth > MAX_NESTING_DEPTH {
            return Err(Error::InvalidWkb("geometry nesting is too deep"));
        }
        let order = match self.read_u8()? {
            0 => ByteOrder::Big,
            1 => ByteOrder::Little,
            _ => return Err(Error::InvalidWkb("invalid WKB byte order")),
        };
        let raw_type = self.read_u32(order)?;
        let (base_type, dimensions) = match raw_type {
            1..=7 => (raw_type, 2),
            1001..=1007 => (raw_type - 1000, 3),
            _ => return Err(Error::InvalidWkb("unsupported WKB geometry type")),
        };
        let mut primitives = Vec::new();
        match base_type {
            1 => {
                let coordinate = self.read_coordinate(order, dimensions)?;
                if coordinate.x.is_nan() && coordinate.y.is_nan() {
                    return Ok((base_type, primitives));
                }
                self.validate_coordinate(coordinate)?;
                self.envelope.add(coordinate);
                primitives.push(Primitive::Point(coordinate));
            }
            2 => {
                let count = self.read_count(order, 8 * dimensions)?;
                let mut points = Vec::with_capacity(count);
                for _ in 0..count {
                    let point = self.read_coordinate(order, dimensions)?;
                    self.validate_coordinate(point)?;
                    self.envelope.add(point);
                    points.push(point);
                }
                primitives.push(Primitive::Line(points));
            }
            3 => {
                let ring_count = self.read_count(order, 4)?;
                let mut rings = Vec::with_capacity(ring_count);
                for _ in 0..ring_count {
                    let point_count = self.read_count(order, 8 * dimensions)?;
                    let mut ring = Vec::with_capacity(point_count);
                    for _ in 0..point_count {
                        let point = self.read_coordinate(order, dimensions)?;
                        self.validate_coordinate(point)?;
                        self.envelope.add(point);
                        ring.push(point);
                    }
                    if !ring.is_empty() && (ring.len() < 4 || ring.first() != ring.last()) {
                        return Err(Error::InvalidWkb("polygon ring is not closed"));
                    }
                    rings.push(ring);
                }
                primitives.push(Primitive::Polygon(rings));
            }
            4..=7 => {
                let count = self.read_count(order, 5)?;
                for _ in 0..count {
                    let (child_type, mut child) = self.parse_geometry(depth + 1)?;
                    let expected = match base_type {
                        4 => 1,
                        5 => 2,
                        6 => 3,
                        _ => child_type,
                    };
                    if child_type != expected {
                        return Err(Error::InvalidWkb("invalid geometry collection member"));
                    }
                    primitives.append(&mut child);
                }
            }
            _ => unreachable!(),
        }
        Ok((base_type, primitives))
    }

    fn validate_coordinate(&self, point: Coordinate) -> Result<(), Error> {
        if !point.x.is_finite() || !point.y.is_finite() {
            return Err(Error::InvalidWkb("coordinate is not finite"));
        }
        Ok(())
    }

    fn read_count(
        &mut self,
        order: ByteOrder,
        minimum_element_bytes: usize,
    ) -> Result<usize, Error> {
        let count = self.read_u32(order)? as usize;
        let remaining = self.input.len() - self.position;
        if count > remaining / minimum_element_bytes {
            return Err(Error::InvalidWkb("geometry element count exceeds input"));
        }
        Ok(count)
    }

    fn read_coordinate(
        &mut self,
        order: ByteOrder,
        dimensions: usize,
    ) -> Result<Coordinate, Error> {
        let x = self.read_f64(order)?;
        let y = self.read_f64(order)?;
        if dimensions == 3 {
            let _z = self.read_f64(order)?;
        }
        Ok(Coordinate { x, y })
    }

    fn read_u8(&mut self) -> Result<u8, Error> {
        let value = *self
            .input
            .get(self.position)
            .ok_or(Error::InvalidWkb("unexpected end of WKB"))?;
        self.position += 1;
        Ok(value)
    }

    fn read_u32(&mut self, order: ByteOrder) -> Result<u32, Error> {
        let bytes = self.read_array::<4>()?;
        Ok(match order {
            ByteOrder::Little => u32::from_le_bytes(bytes),
            ByteOrder::Big => u32::from_be_bytes(bytes),
        })
    }

    fn read_f64(&mut self, order: ByteOrder) -> Result<f64, Error> {
        let bytes = self.read_array::<8>()?;
        Ok(f64::from_bits(match order {
            ByteOrder::Little => u64::from_le_bytes(bytes),
            ByteOrder::Big => u64::from_be_bytes(bytes),
        }))
    }

    fn read_array<const N: usize>(&mut self) -> Result<[u8; N], Error> {
        let end = self
            .position
            .checked_add(N)
            .ok_or(Error::InvalidWkb("WKB position overflow"))?;
        let bytes = self
            .input
            .get(self.position..end)
            .ok_or(Error::InvalidWkb("unexpected end of WKB"))?;
        self.position = end;
        Ok(bytes.try_into().expect("fixed-size WKB slice"))
    }
}
