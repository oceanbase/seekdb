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

#ifndef OCEANBASE_LIB_GEO_OB_GEO_TO_TREE_VISITOR_
#define OCEANBASE_LIB_GEO_OB_GEO_TO_TREE_VISITOR_
#include "lib/geo/ob_geo_visitor.h"

namespace oceanbase
{
namespace common
{

class ObGeoToTreeVisitor : public ObEmptyGeoVisitor
{
public:
  ObGeoToTreeVisitor(ObIAllocator *allocator)
    : allocator_(allocator), root_(NULL),
      parent_(GEOM_PAGE_SIZE_GEO, ModulePageAllocator(*allocator, "GISModule")) {}
  ObGeometry *get_geometry() const { return root_; }
  template<typename T>
  int alloc_geo_tree_obj(T *&obj);
  template<typename T, typename T_IBIN>
  int create_geo_tree_collection(T_IBIN *i_geo);
  template<typename T_POINT, typename T_TREE, typename T_IBIN, typename T_BIN>
  int create_geo_multi_point(T_TREE *&geo, T_IBIN *geo_ibin);
  template<typename T, typename T_BIN>
  int point_visit(T_BIN *geo);

  template<typename P_TYPE, typename P_BIN_TYPE, typename L_TYPE, typename L_BIN_TYPE, 
           typename POINT_TYPE, typename RINGS_TYPE, typename p_ibin_type>
  int polygon_visit(p_ibin_type *geo);

  template<typename T>
  int update_root_and_parent(T *geo);

  bool prepare(ObGeometry *geo) override { UNUSED(geo); return true; }
  // wkb
  int visit(ObIWkbGeogPoint *geo) override;
  int visit(ObIWkbGeomPoint *geo) override;
  int visit(ObIWkbGeogLineString *geo) override;
  int visit(ObIWkbGeomLineString *geo) override;  
  int visit(ObIWkbGeogMultiPoint *geo) override;  
  int visit(ObIWkbGeomMultiPoint *geo) override;
  int visit(ObIWkbGeogMultiLineString *geo) override;
  int visit(ObIWkbGeomMultiLineString *geo) override;
  int visit(ObIWkbGeogPolygon *geo) override;
  int visit(ObIWkbGeomPolygon *geo) override;
  int visit(ObIWkbGeogMultiPolygon *geo) override;
  int visit(ObIWkbGeomMultiPolygon *geo) override;
  int visit(ObIWkbGeogCollection *geo) override;
  int visit(ObIWkbGeomCollection *geo) override;
 
  bool is_end(ObIWkbGeogLineString *geo) override { UNUSED(geo); return true; }
  bool is_end(ObIWkbGeomLineString *geo) override { UNUSED(geo); return true; }
  bool is_end(ObIWkbGeogMultiPoint *geo) override { UNUSED(geo); return true; }
  bool is_end(ObIWkbGeomMultiPoint *geo) override { UNUSED(geo); return true; }
  bool is_end(ObIWkbGeogLinearRing *geo) override { UNUSED(geo); return true; }
  bool is_end(ObIWkbGeomLinearRing *geo) override { UNUSED(geo); return true; }
  bool is_end(ObIWkbGeogPolygon *geo) override { UNUSED(geo); return true; }
  bool is_end(ObIWkbGeomPolygon *geo) override { UNUSED(geo); return true; }

  int finish(ObIWkbGeogMultiLineString *geo) override { UNUSED(geo); parent_.pop_back(); return OB_SUCCESS; }
  int finish(ObIWkbGeomMultiLineString *geo) override { UNUSED(geo); parent_.pop_back(); return OB_SUCCESS; }
  int finish(ObIWkbGeogMultiPolygon *geo) override { UNUSED(geo); parent_.pop_back(); return OB_SUCCESS; }
  int finish(ObIWkbGeomMultiPolygon *geo) override { UNUSED(geo); parent_.pop_back(); return OB_SUCCESS; }
  int finish(ObIWkbGeogCollection *geo) override { UNUSED(geo); parent_.pop_back(); return OB_SUCCESS; }
  int finish(ObIWkbGeomCollection *geo) override { UNUSED(geo); parent_.pop_back(); return OB_SUCCESS; }  


private:
  ObIAllocator *allocator_;
  ObGeometry *root_;
  ObArray<ObGeometrycollection *> parent_;
  DISALLOW_COPY_AND_ASSIGN(ObGeoToTreeVisitor);
};

} // namespace common
} // namespace oceanbase

#endif
