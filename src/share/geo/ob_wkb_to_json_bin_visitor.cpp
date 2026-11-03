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

#include "lib/ob_errno.h"
#include <cstdint>
#define USING_LOG_PREFIX LIB
#include "ob_wkb_to_json_bin_visitor.h"
#include "share/geo/ob_geo_func_register.h"
#include "share/geo/ob_wkb_to_json_visitor.h"
#include "rust/json.h"

namespace oceanbase {
namespace common {

enum TypeNameMap
{
  Point = 0,
  MultiPoint = 1,
  LineString = 2,
  MultiLineString = 3,
  Polygon = 4,
  MultiPolygon = 5,
  GeometryCollection = 6
};
const ObString type_name_table[]
{
  "Point",
  "MultiPoint",
  "LineString",
  "MultiLineString",
  "Polygon",
  "MultiPolygon",
  "GeometryCollection"
};
enum KeyNameMap
{
  crs = 0,
  bbox = 1,
  type = 2,
  geometries = 3,
  coordinates = 4,
  properties = 5,
  name = 6
};
const ObString key_name_table[]
{
  "crs",
  "bbox",
  "type",
  "geometries",
  "coordinates",
  "properties",
  "name"
};
namespace {
int binary_writer_status(int status)
{
  return status == 0 ? OB_SUCCESS : status == 1 ? OB_ALLOCATE_MEMORY_FAILED : OB_ERR_UNEXPECTED;
}
}

struct ObWkbToJsonBinVisitor::WriterState
{
  WriterState() : native_(json::new_binary_writer()) {}
  rust::Box<json::BinaryWriter> native_;
};

ObWkbToJsonBinVisitor::ObWkbToJsonBinVisitor(
    ObIAllocator *allocator,
    uint32_t max_dec_digits,
    uint8_t flag,
    const ObGeoSrid srid)
    : writer_(nullptr),
      flag_(flag),
      srid_(srid),
      allocator_(allocator),
      max_dec_digits_(max_dec_digits),
      is_appendCrs(false)
{
}

int ObWkbToJsonBinVisitor::visit(ObIWkbGeogPoint *geo)
{
  INIT_SUCC(ret);
  if (OB_FAIL(appendPointObj<ObIWkbGeogPoint>(geo, type_name_table[TypeNameMap::Point]))) {
  }
  return ret;
}

int ObWkbToJsonBinVisitor::visit(ObIWkbGeomPoint *geo)
{
  INIT_SUCC(ret);
  ObString type_name(type_name_table[0]); //"Point"
  if (OB_FAIL(appendPointObj<ObIWkbGeomPoint>(geo, type_name))) {
  }
  return ret;
}

int ObWkbToJsonBinVisitor::visit(ObIWkbGeogMultiPoint *geo)
{
  INIT_SUCC(ret);
  if (OB_FAIL((appendLineObj<ObIWkbGeogMultiPoint, ObWkbGeogMultiPoint>(geo, type_name_table[TypeNameMap::MultiPoint])))) {
  }
  return ret;
}

int ObWkbToJsonBinVisitor::visit(ObIWkbGeomMultiPoint *geo)
{
  INIT_SUCC(ret);
  if (OB_FAIL((appendLineObj<ObIWkbGeomMultiPoint, ObWkbGeomMultiPoint>(geo, type_name_table[TypeNameMap::MultiPoint])))) {
  }
  return ret;
}

int ObWkbToJsonBinVisitor::visit(ObIWkbGeogLineString *geo)
{
  INIT_SUCC(ret);
  // ObString type_name(type_name_table[2]); //"LineString"
  if (OB_FAIL((appendLineObj<ObIWkbGeogLineString, ObWkbGeogLineString>(geo, type_name_table[TypeNameMap::LineString])))) {
  }
  return ret;
}

int ObWkbToJsonBinVisitor::visit(ObIWkbGeomLineString *geo)
{
  INIT_SUCC(ret);
  if (OB_FAIL((appendLineObj<ObIWkbGeomLineString, ObWkbGeomLineString>(geo, type_name_table[TypeNameMap::LineString])))) {
  }
  return ret;
}

int ObWkbToJsonBinVisitor::visit(ObIWkbGeogMultiLineString *geo)
{
  INIT_SUCC(ret);
  if (OB_FAIL((appendMultiLineObj<
                          ObIWkbGeogMultiLineString,
                          ObWkbGeogMultiLineString,
                          ObWkbGeogLineString>(geo, type_name_table[TypeNameMap::MultiLineString])))) {
  }
  return ret;
}

int ObWkbToJsonBinVisitor::visit(ObIWkbGeomMultiLineString *geo)
{
  INIT_SUCC(ret);
  if (OB_FAIL((appendMultiLineObj<
                          ObIWkbGeomMultiLineString,
                          ObWkbGeomMultiLineString,
                          ObWkbGeomLineString>(geo, type_name_table[TypeNameMap::MultiLineString])))) {
  }
  return ret;
}

int ObWkbToJsonBinVisitor::visit(ObIWkbGeogPolygon *geo)
{
  INIT_SUCC(ret);
  if (OB_FAIL((appendPolygonObj<
                        ObIWkbGeogPolygon,
                        ObWkbGeogPolygon,
                        ObWkbGeogLinearRing,
                        ObWkbGeogPolygonInnerRings>(geo, type_name_table[TypeNameMap::Polygon])))) {
  }
  return ret;
}

int ObWkbToJsonBinVisitor::visit(ObIWkbGeomPolygon *geo)
{
  INIT_SUCC(ret);
  if (OB_FAIL((appendPolygonObj<
                        ObIWkbGeomPolygon,
                        ObWkbGeomPolygon,
                        ObWkbGeomLinearRing,
                        ObWkbGeomPolygonInnerRings>(geo, type_name_table[TypeNameMap::Polygon])))) {
  }
  return ret;
}

int ObWkbToJsonBinVisitor::visit(ObIWkbGeogMultiPolygon *geo)
{
  INIT_SUCC(ret);
  if (OB_FAIL((appendMultiPolygonObj<
                          ObIWkbGeogMultiPolygon,
                          ObWkbGeogMultiPolygon,
                          ObWkbGeogPolygon,
                          ObWkbGeogLinearRing,
                          ObWkbGeogPolygonInnerRings>(geo, type_name_table[TypeNameMap::MultiPolygon])))) {
  }
  return ret;
}

int ObWkbToJsonBinVisitor::visit(ObIWkbGeomMultiPolygon *geo)
{
  INIT_SUCC(ret);
  if (OB_FAIL((appendMultiPolygonObj<
                          ObIWkbGeomMultiPolygon,
                          ObWkbGeomMultiPolygon,
                          ObWkbGeomPolygon,
                          ObWkbGeomLinearRing,
                          ObWkbGeomPolygonInnerRings>(geo, type_name_table[TypeNameMap::MultiPolygon])))) {
  }
  return ret;
}

int ObWkbToJsonBinVisitor::visit(ObIWkbGeogCollection *geo)
{
  INIT_SUCC(ret);
  if (OB_FAIL((appendCollectionObj<
                  ObIWkbGeogCollection,
                  ObWkbGeogCollection>(geo, type_name_table[TypeNameMap::GeometryCollection])))) {
  }
  return ret;
}

int ObWkbToJsonBinVisitor::visit(ObIWkbGeomCollection *geo)
{
  INIT_SUCC(ret);
  if (OB_FAIL((appendCollectionObj<
                  ObIWkbGeomCollection,
                  ObWkbGeomCollection>(geo, type_name_table[TypeNameMap::GeometryCollection])))) {
  }
  return ret;
}

int ObWkbToJsonBinVisitor::to_jsonbin(ObGeometry *geo, ObString &geojsonbin)
{
  int ret = OB_SUCCESS;
  WriterState state;
  writer_ = &state;
  if (OB_FAIL(geo->do_visit(*this))) {
  } else {
    const json::WriterFinishResult result = json::writer_finish(std::move(state.native_));
    if (OB_FAIL(binary_writer_status(result.status))) {
    } else {
      ret = ob_write_string(*allocator_, ObString(result.binary.size(),
          reinterpret_cast<const char *>(result.binary.data())), geojsonbin);
    }
  }
  writer_ = nullptr;
  return ret;
}

int ObWkbToJsonBinVisitor::appendCoordinatePoint(double x, double y)
{
  int ret = OB_SUCCESS;
  uint64_t array = 0;
  if (OB_FAIL(beginContainer(array, 1, 2, 0))) {
  } else if (OB_FAIL(appendDouble(x))) {
  } else if (OB_FAIL(appendDouble(y))) {
  } else {
    ret = endContainer(array);
  }
  return ret;
}

template<typename T_IBIN>
int ObWkbToJsonBinVisitor::appendPointObj(T_IBIN *geo, const ObString &type_name)
{
  int ret = OB_SUCCESS;
  uint64_t object = 0;
  if (OB_FAIL(appendJsonCommon(type_name, geo, object))) {
  } else if (OB_FAIL(appendCoordinatePoint(geo->x(), geo->y()))) {
  } else {
    ret = endContainer(object);
  }
  return ret;
}

template<typename T_BIN>
int ObWkbToJsonBinVisitor::appendLine(const T_BIN *line)
{
  int ret = OB_SUCCESS;
  uint64_t array = 0;
  if (OB_FAIL(beginContainer(array, 1, line->iter_idx_max()))) {
  } else {
    for (auto point = line->begin(); OB_SUCC(ret) && point != line->end(); ++point) {
      ret = appendCoordinatePoint(point->template get<0>(), point->template get<1>());
    }
    if (OB_SUCC(ret)) { ret = endContainer(array); }
  }
  return ret;
}

template<typename T_IBIN, typename T_BIN>
int ObWkbToJsonBinVisitor::appendLineObj(T_IBIN *geo, const ObString &type_name)
{
  int ret = OB_SUCCESS;
  uint64_t object = 0;
  if (OB_FAIL(appendJsonCommon(type_name, geo, object))) {
  } else if (OB_FAIL(appendLine(reinterpret_cast<const T_BIN *>(geo->val())))) {
  } else {
    ret = endContainer(object);
  }
  return ret;
}

template<typename T_IBIN, typename T_BIN, typename T_BIN_LINE>
int ObWkbToJsonBinVisitor::appendMultiLineObj(T_IBIN *geo, const ObString &type_name)
{
  int ret = OB_SUCCESS;
  uint64_t object = 0;
  uint64_t array = 0;
  const T_BIN *lines = reinterpret_cast<const T_BIN *>(geo->val());
  if (OB_FAIL(appendJsonCommon(type_name, geo, object))) {
  } else if (OB_FAIL(beginContainer(array, 1, lines->iter_idx_max()))) {
  } else {
    for (auto line = lines->begin(); OB_SUCC(ret) && line != lines->end(); ++line) {
      ret = appendLine<T_BIN_LINE>(&(*line));
    }
    if (OB_SUCC(ret) && OB_SUCC(ret = endContainer(array))) { ret = endContainer(object); }
  }
  return ret;
}

template<typename T_BIN, typename T_BIN_RING, typename T_BIN_INNER_RING>
int ObWkbToJsonBinVisitor::appendPolygon(const T_BIN *poly)
{
  int ret = OB_SUCCESS;
  uint64_t array = 0;
  if (OB_FAIL(beginContainer(array, 1, poly->size()))) {
  } else {
    if (poly->size() > 0) { ret = appendLine<T_BIN_RING>(&poly->exterior_ring()); }
    const T_BIN_INNER_RING &rings = poly->inner_rings();
    for (auto ring = rings.begin(); OB_SUCC(ret) && ring != rings.end(); ++ring) {
      ret = appendLine<T_BIN_RING>(&(*ring));
    }
    if (OB_SUCC(ret)) { ret = endContainer(array); }
  }
  return ret;
}

template<typename T_IBIN, typename T_BIN, typename T_BIN_RING, typename T_BIN_INNER_RING>
int ObWkbToJsonBinVisitor::appendPolygonObj(T_IBIN *geo, const ObString &type_name)
{
  int ret = OB_SUCCESS;
  uint64_t object = 0;
  if (OB_FAIL(appendJsonCommon(type_name, geo, object))) {
  } else if (OB_FAIL((appendPolygon<T_BIN, T_BIN_RING, T_BIN_INNER_RING>(
      reinterpret_cast<const T_BIN *>(geo->val()))))) {
  } else {
    ret = endContainer(object);
  }
  return ret;
}

template<typename T_IBIN, typename T_BIN, typename T_BIN_POLY, typename T_BIN_RING, typename T_BIN_INNER_RING>
int ObWkbToJsonBinVisitor::appendMultiPolygonObj(T_IBIN *geo, const ObString &type_name)
{
  int ret = OB_SUCCESS;
  uint64_t object = 0;
  uint64_t array = 0;
  const T_BIN *polys = reinterpret_cast<const T_BIN *>(geo->val());
  if (OB_FAIL(appendJsonCommon(type_name, geo, object))) {
  } else if (OB_FAIL(beginContainer(array, 1, polys->iter_idx_max()))) {
  } else {
    for (auto poly = polys->begin(); OB_SUCC(ret) && poly != polys->end(); ++poly) {
      ret = appendPolygon<T_BIN_POLY, T_BIN_RING, T_BIN_INNER_RING>(&(*poly));
    }
    if (OB_SUCC(ret) && OB_SUCC(ret = endContainer(array))) { ret = endContainer(object); }
  }
  return ret;
}

template<typename T_IPOINT,
          typename T_IMULTIPOINT,
          typename T_ILINE,
          typename T_IMULTILINE,
          typename T_IPOLY,
          typename T_IMULTIPOLY, 
          typename T_ICOLLC,
          typename T_POINT,
          typename T_MULTIPOINT,
          typename T_LINE,
          typename T_MULTILINE,
          typename T_POLY,
          typename T_LINEARRING,
          typename T_INNERRING,
          typename T_MULTIPOLY,
          typename T_COLLC>
int ObWkbToJsonBinVisitor::appendCollectionSub(
    typename T_COLLC::const_pointer sub_ptr, const T_COLLC *collection)
{
  int ret = OB_SUCCESS;
  ObGeoType sub_type = collection->get_sub_type(sub_ptr);
    switch (sub_type) {
      case ObGeoType::POINT : {
        const T_POINT* geo = reinterpret_cast<const T_POINT*>(sub_ptr);
        T_IPOINT igeo;
        ObString data(sizeof(T_POINT), reinterpret_cast<const char *>(geo));
        igeo.set_data(data);
        if (OB_FAIL(appendPointObj<T_IPOINT>(&igeo, type_name_table[TypeNameMap::Point]))) {
        }
        break;
      }
      case ObGeoType::MULTIPOINT : {
        const T_MULTIPOINT* geo = reinterpret_cast<const T_MULTIPOINT*>(sub_ptr);
        T_IMULTIPOINT igeo;
        ObString data(sizeof(T_MULTIPOINT), reinterpret_cast<const char *>(geo));
        igeo.set_data(data);
        if (OB_FAIL((appendLineObj<T_IMULTIPOINT, T_MULTIPOINT>(&igeo, type_name_table[TypeNameMap::MultiPoint])))) {
        }
        break;
      }
      case ObGeoType::LINESTRING : {
        const T_LINE* geo = reinterpret_cast<const T_LINE*>(sub_ptr);
        T_ILINE igeo;
        ObString data(sizeof(T_LINE), reinterpret_cast<const char *>(geo));
        igeo.set_data(data);
        if (OB_FAIL((appendLineObj<T_ILINE, T_LINE>(&igeo, type_name_table[TypeNameMap::LineString])))) {
        }
        break;
      }
      case ObGeoType::MULTILINESTRING : {
        const T_MULTILINE* geo = reinterpret_cast<const T_MULTILINE*>(sub_ptr);
        T_IMULTILINE igeo;
        ObString data(sizeof(T_MULTILINE), reinterpret_cast<const char *>(geo));
        igeo.set_data(data);
        if (OB_FAIL((appendMultiLineObj<
                        T_IMULTILINE,
                        T_MULTILINE,
                        T_LINE>(&igeo, type_name_table[TypeNameMap::MultiLineString])))) {
        }
        break;
      }
      case ObGeoType::POLYGON : {
        const T_POLY* geo = reinterpret_cast<const T_POLY*>(sub_ptr);
        T_IPOLY igeo;
        ObString data(sizeof(T_POLY), reinterpret_cast<const char *>(geo));
        igeo.set_data(data);
        if (OB_FAIL((appendPolygonObj<
                        T_IPOLY,
                        T_POLY,
                        T_LINEARRING,
                        T_INNERRING>(&igeo, type_name_table[TypeNameMap::Polygon])))) {
        }
        break;
      }
      case ObGeoType::MULTIPOLYGON : {
        const T_MULTIPOLY* geo = reinterpret_cast<const T_MULTIPOLY*>(sub_ptr);
        T_IMULTIPOLY igeo;
        ObString data(sizeof(T_MULTIPOLY), reinterpret_cast<const char *>(geo));
        igeo.set_data(data);
        if (OB_FAIL((appendMultiPolygonObj<
                        T_IMULTIPOLY,
                        T_MULTIPOLY,
                        T_POLY,
                        T_LINEARRING,
                        T_INNERRING>(&igeo, type_name_table[TypeNameMap::MultiPolygon])))) {
        }
        break;
      }
      case ObGeoType::GEOMETRYCOLLECTION : {
        const T_COLLC* geo = reinterpret_cast<const T_COLLC*>(sub_ptr);
        T_ICOLLC igeo;
        ObString data(sizeof(T_COLLC), reinterpret_cast<const char *>(geo));
        igeo.set_data(data);
        if (OB_FAIL((appendCollectionObj<T_ICOLLC, T_COLLC>(&igeo, type_name_table[TypeNameMap::GeometryCollection])))) {
        }
        break;
      }
      default : {
        ret = OB_INVALID_ARGUMENT;
        break;
      }
    }
  return ret;
}

int ObWkbToJsonBinVisitor::appendCollectionSubWrapper(
    ObWkbGeogCollection::const_pointer sub_ptr, const ObWkbGeogCollection *collection)
{
  return appendCollectionSub<
      ObIWkbGeogPoint,
      ObIWkbGeogMultiPoint,
      ObIWkbGeogLineString,
      ObIWkbGeogMultiLineString,
      ObIWkbGeogPolygon,
      ObIWkbGeogMultiPolygon,
      ObIWkbGeogCollection,
      ObWkbGeogPoint,
      ObWkbGeogMultiPoint,
      ObWkbGeogLineString,
      ObWkbGeogMultiLineString,
      ObWkbGeogPolygon,
      ObWkbGeogLinearRing,
      ObWkbGeogPolygonInnerRings,
      ObWkbGeogMultiPolygon,
      ObWkbGeogCollection>(sub_ptr, collection);
}

int ObWkbToJsonBinVisitor::appendCollectionSubWrapper(
    ObWkbGeomCollection::const_pointer sub_ptr, const ObWkbGeomCollection *collection)
{
  return appendCollectionSub<
      ObIWkbGeomPoint,
      ObIWkbGeomMultiPoint,
      ObIWkbGeomLineString,
      ObIWkbGeomMultiLineString,
      ObIWkbGeomPolygon,
      ObIWkbGeomMultiPolygon,
      ObIWkbGeomCollection,
      ObWkbGeomPoint,
      ObWkbGeomMultiPoint,
      ObWkbGeomLineString,
      ObWkbGeomMultiLineString,
      ObWkbGeomPolygon,
      ObWkbGeomLinearRing,
      ObWkbGeomPolygonInnerRings,
      ObWkbGeomMultiPolygon,
      ObWkbGeomCollection>(sub_ptr, collection);
}

template<typename T_IBIN, typename T_BIN>
int ObWkbToJsonBinVisitor::appendCollectionObj(T_IBIN *geo, const ObString &type_name)
{
  int ret = OB_SUCCESS;
  uint64_t object = 0;
  uint64_t array = 0;
  const T_BIN *collection = reinterpret_cast<const T_BIN *>(geo->val());
  if (OB_FAIL(appendJsonCommon(type_name, geo, object))) {
  } else if (OB_FAIL(beginContainer(array, 1, collection->iter_idx_max()))) {
  } else {
    for (auto child = collection->begin(); OB_SUCC(ret) && child != collection->end(); ++child) {
      ret = appendCollectionSubWrapper(child.operator->(), collection);
    }
    if (OB_SUCC(ret) && OB_SUCC(ret = endContainer(array))) { ret = endContainer(object); }
  }
  return ret;
}

int ObWkbToJsonBinVisitor::appendJsonCommon(const ObString type_name, ObGeometry *geo, uint64_t &object)
{
  int ret = OB_SUCCESS;
  const bool crs = !is_appendCrs && srid_ != 0
      && ((flag_ & ObGeoJsonFormat::SHORT_SRID) || (flag_ & ObGeoJsonFormat::LONG_SRID));
  const bool bbox = (flag_ & ObGeoJsonFormat::BBOX) && !geo->is_empty();
  if (geo->type() <= ObGeoType::GEOMETRY || geo->type() >= ObGeoType::GEOTYPEMAX) {
    ret = OB_ERR_GIS_INVALID_DATA;
  } else if (OB_FAIL(beginContainer(object, 0, 2 + crs + bbox))) {
  } else if (crs && OB_FAIL(appendObjKey(key_name_table[KeyNameMap::crs]))) {
  } else if (bbox && OB_FAIL(appendObjKey(key_name_table[KeyNameMap::bbox]))) {
  } else if (OB_FAIL(appendObjKey(key_name_table[KeyNameMap::type]))) {
  } else if (OB_FAIL(appendObjKey(key_name_table[geo->type() == ObGeoType::GEOMETRYCOLLECTION
      ? KeyNameMap::geometries : KeyNameMap::coordinates]))) {
  } else if (OB_FAIL(appendCrs())) {
  } else if (OB_FAIL(appendBbox(geo))) {
  } else {
    ret = appendString(type_name);
  }
  return ret;
}

int ObWkbToJsonBinVisitor::beginContainer(uint64_t &handle, int type_flag, uint64_t count, uint8_t width)
{
  const json::WriterBeginResult result = writer_->native_->writer_begin(type_flag == 0, count, width);
  handle = result.handle;
  return binary_writer_status(result.status);
}

int ObWkbToJsonBinVisitor::appendObjKey(const ObString key)
{
  return binary_writer_status(writer_->native_->writer_key(rust::Slice<const uint8_t>(
      reinterpret_cast<const uint8_t *>(key.ptr()), key.length())));
}

int ObWkbToJsonBinVisitor::endContainer(uint64_t handle)
{
  return binary_writer_status(writer_->native_->writer_end(handle));
}

int ObWkbToJsonBinVisitor::appendCrs()
{
  int ret = OB_SUCCESS;
  if (!is_appendCrs && srid_ != 0
      && ((flag_ & ObGeoJsonFormat::SHORT_SRID) || (flag_ & ObGeoJsonFormat::LONG_SRID))) {
    uint64_t object = 0;
    if (OB_FAIL(beginContainer(object, 0, 2, 0))) {
    } else if (OB_FAIL(appendObjKey(key_name_table[KeyNameMap::type]))) {
    } else if (OB_FAIL(appendObjKey(key_name_table[KeyNameMap::properties]))) {
    } else if (OB_FAIL(appendString(key_name_table[KeyNameMap::name]))) {
    } else if (OB_FAIL(appendCrsProp())) {
    } else if (OB_FAIL(endContainer(object))) {
    } else { is_appendCrs = true; }
  }
  return ret;
}

int ObWkbToJsonBinVisitor::appendCrsProp()
{
  int ret = OB_SUCCESS;
  uint64_t object = 0;
  char srid_buf[ObFastFormatInt::MAX_DIGITS10_STR_SIZE] = {0};
  const uint64_t srid_len = ObFastFormatInt::format_signed(srid_, srid_buf);
  ObStringBuffer value(allocator_);
  if (OB_FAIL(value.append((flag_ & ObGeoJsonFormat::LONG_SRID) ? "urn:ogc:def:crs:EPSG::" : "EPSG:"))) {
  } else if (OB_FAIL(value.append(srid_buf, srid_len, 0))) {
  } else if (OB_FAIL(beginContainer(object, 0, 1, 0))) {
  } else if (OB_FAIL(appendObjKey(key_name_table[KeyNameMap::name]))) {
  } else if (OB_FAIL(appendString(value.string()))) {
  } else { ret = endContainer(object); }
  return ret;
}

int ObWkbToJsonBinVisitor::appendBbox(ObGeometry *geo)
{
  int ret = OB_SUCCESS;
  if ((flag_ & ObGeoJsonFormat::BBOX) && !geo->is_empty()) {
    ObGeogBox *box = nullptr;
    ObArenaAllocator tmp_allocator;
    ObGeometry *box_geo = nullptr;
    if (geo->crs() == ObGeoCRS::Geographic) {
      if (OB_FAIL(ObGeoTypeUtil::create_geo_by_type(tmp_allocator, geo->type(), false, true, box_geo, geo->get_srid()))) {
      } else { box_geo->set_data(geo->val()); }
    } else { box_geo = geo; }
    CREATE_WITH_TEMP_CONTEXT(lib::ContextParam().set_mem_attr("GISModule", ObCtxIds::DEFAULT_CTX_ID)) {
      ObGeoEvalCtx geo_ctx(CURRENT_CONTEXT);
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(geo_ctx.append_geo_arg(box_geo))) {
      } else if (OB_FAIL(ObGeoFunc<ObGeoFuncType::Box>::geo_func::eval(geo_ctx, box))) {
      } else {
        uint64_t array = 0;
        if (OB_FAIL(beginContainer(array, 1, 4, 0))) {
        } else if (OB_FAIL(appendDouble(box->xmin))) {
        } else if (OB_FAIL(appendDouble(box->ymin))) {
        } else if (OB_FAIL(appendDouble(box->xmax))) {
        } else if (OB_FAIL(appendDouble(box->ymax))) {
        } else { ret = endContainer(array); }
      }
    }
  }
  return ret;
}

int ObWkbToJsonBinVisitor::appendString(const ObString &value)
{
  return binary_writer_status(writer_->native_->writer_string(rust::Slice<const uint8_t>(
      reinterpret_cast<const uint8_t *>(value.ptr()), value.length())));
}

int ObWkbToJsonBinVisitor::appendDouble(double value)
{
  const double rounded = max_dec_digits_ < INT_MAX32
      ? ObGeoTypeUtil::round_double(value, max_dec_digits_, false) : value;
  return binary_writer_status(writer_->native_->writer_double(rounded));
}

} // namespace common
} // namespace oceanbase
