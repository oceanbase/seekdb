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

#ifndef OCEANBASE_LIB_OB_GEO_EVAL_CTX_H_
#define OCEANBASE_LIB_OB_GEO_EVAL_CTX_H_

#include <cstdint>

#include "lib/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace lib
{
class MemoryContext;
}

namespace common
{

class ObGeometry;
class ObIAllocator;
class ObString;
struct ObGeoBufferStrategy;
class ObSrsItem;

// Non-geometry arguments, e.g. distance_sphere.
union ObGeoNormalVal
{
  int64_t int64_;
  double double_;
  const ObString *string_;
  ObGeoBufferStrategy *strategy_; // todo@dazhi
};

// Evaluation context shared by geometry function interfaces.
class ObGeoEvalCtx
{
public:
  ObGeoEvalCtx(lib::MemoryContext &mem_ctx);
  ObGeoEvalCtx(lib::MemoryContext &mem_ctx, const ObSrsItem *srs_item);

  ~ObGeoEvalCtx() = default;

  inline int append_geo_arg(const ObGeometry *g)
  {
    INIT_SUCC(ret);
    if (g_arg_c_ < MAX_ARG_COUNT) {
      gis_args_[g_arg_c_++] = g;
    } else {
      ret = OB_ERR_ARGUMENT_OUT_OF_RANGE;
    }
    return ret;
  }

  inline int append_val_arg(ObGeoNormalVal &value)
  {
    INIT_SUCC(ret);
    if (v_arg_c_ < MAX_ARG_COUNT) {
      val_args_[v_arg_c_++] = value;
    } else {
      ret = OB_ERR_ARGUMENT_OUT_OF_RANGE;
    }
    return ret;
  }

  inline int append_val_arg(ObGeoBufferStrategy *value)
  {
    INIT_SUCC(ret);
    if (v_arg_c_ < MAX_ARG_COUNT) {
      ObGeoNormalVal n_val; // todo@dazhi: remove stack variable
      n_val.strategy_ = value;
      val_args_[v_arg_c_++] = n_val;
    } else {
      ret = OB_ERR_ARGUMENT_OUT_OF_RANGE;
    }
    return ret;
  }

  inline int append_val_arg(int64_t value)
  {
    INIT_SUCC(ret);
    if (v_arg_c_ < MAX_ARG_COUNT) {
      ObGeoNormalVal n_val; // todo@dazhi: remove stack variable
      n_val.int64_ = value;
      val_args_[v_arg_c_++] = n_val;
    } else {
      ret = OB_ERR_ARGUMENT_OUT_OF_RANGE;
    }
    return ret;
  }

  inline int append_val_arg(double value)
  {
    INIT_SUCC(ret);
    if (v_arg_c_ < MAX_ARG_COUNT) {
      ObGeoNormalVal n_val;
      n_val.double_ = value;
      val_args_[v_arg_c_++] = n_val;
    } else {
      ret = OB_ERR_ARGUMENT_OUT_OF_RANGE;
    }
    return ret;
  }

  inline int append_val_arg(const ObString *value)
  {
    INIT_SUCC(ret);
    if (v_arg_c_ < MAX_ARG_COUNT) {
      ObGeoNormalVal n_val;
      n_val.string_ = value;
      val_args_[v_arg_c_++] = n_val;
    } else {
      ret = OB_ERR_ARGUMENT_OUT_OF_RANGE;
    }
    return ret;
  }

  inline int get_geo_count() const { return g_arg_c_; }
  inline int get_val_count() const { return v_arg_c_; }
  inline const ObGeometry *get_geo_arg(int idx) const
  {
    const ObGeometry *geo_ret = nullptr;
    if (idx >= 0 && idx < g_arg_c_) {
      geo_ret = gis_args_[idx];
    }
    return geo_ret;
  }

  inline const ObGeoNormalVal *get_val_arg(int idx) const
  {
    const ObGeoNormalVal *val_ret = nullptr;
    if (idx >= 0 && idx < v_arg_c_) {
      val_ret = &val_args_[idx];
    }
    return val_ret;
  }

  inline ObIAllocator *get_allocator() const { return allocator_; }
  inline const ObSrsItem *get_srs() const { return srs_; }

  inline void set_is_called_in_pg_expr(bool in) { is_called_in_pg_expr_ = in; }
  inline bool get_is_called_in_pg_expr() const { return is_called_in_pg_expr_; }
  inline lib::MemoryContext &get_mem_ctx() const { return mem_ctx_; }

  // Interfaces for unittest only.
  inline void ut_set_geo_count(int count)
  {
    g_arg_c_ = (count >= MAX_ARG_COUNT ? MAX_ARG_COUNT - 1 : count);
  }

  inline void ut_set_geo_arg(int index, ObGeometry *g)
  {
    index = (index >= MAX_ARG_COUNT ? MAX_ARG_COUNT - 1 : index);
    gis_args_[index] = g;
  }

private:
  static const int MAX_ARG_COUNT = 3;

  ObIAllocator *allocator_; // reserved for allocator
  const ObSrsItem *srs_; // get parsed srs or boost context
  int g_arg_c_; // num of geometry arguments
  int v_arg_c_; // num of other arguments, e.g. distance_sphere
  const ObGeometry *gis_args_[MAX_ARG_COUNT];
  ObGeoNormalVal val_args_[MAX_ARG_COUNT];
  bool is_called_in_pg_expr_; // distinguish pg/mysql expr call
  lib::MemoryContext &mem_ctx_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObGeoEvalCtx);
};

struct ObGeoFuncResWithNull
{
  bool bret = false;
  bool is_null = false;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_OB_GEO_EVAL_CTX_H_
