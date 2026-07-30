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

#ifndef OCEANBASE_SQL_OB_EXPR_UDF_H_
#define OCEANBASE_SQL_OB_EXPR_UDF_H_

#include "common/object/ob_object.h"
#include "lib/container/ob_2d_array.h"
#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/expr/ob_i_expr_extra_info.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/engine/expr/ob_expr_result_type_util.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
}
}
namespace sql
{
typedef common::ParamStore ParamStore;

// struct ObSqlCtx;
struct ObExprUDFInfo : public ObIExprExtraInfo
{
  OB_UNIS_VERSION(1);
public:
  ObExprUDFInfo(common::ObIAllocator &alloc, ObExprOperatorType type)
      : ObIExprExtraInfo(alloc, type),
      udf_id_(common::OB_INVALID_ID), udf_package_id_(common::OB_INVALID_ID),
      subprogram_path_(alloc), result_type_(), params_type_(alloc), params_desc_(alloc),
      reserved_udt_udf_(false), loc_(0), reserved_udt_cons_(false),
      is_called_in_sql_(false),
      is_deterministic_(false)
  {
  }

  virtual int deep_copy(common::ObIAllocator &allocator,
                        const ObExprOperatorType type,
                        ObIExprExtraInfo *&copied_info) const override;

  template <typename RE>
  int from_raw_expr(RE &expr);

  int64_t udf_id_;
  int64_t udf_package_id_;
  common::ObFixedArray<int64_t, common::ObIAllocator> subprogram_path_;
  ObExprResType result_type_;
  common::ObFixedArray<ObExprResType, common::ObIAllocator> params_type_;
  common::ObFixedArray<ObUDFParamDesc, common::ObIAllocator> params_desc_;
  bool reserved_udt_udf_;
  uint64_t loc_;
  bool reserved_udt_cons_;
  bool is_called_in_sql_;
  bool is_deterministic_;
};
class ObSqlCtx;
class ObUDFParamDesc;

class ObExprUDF : public ObFuncExprOperator
{
  class ObExprUDFCtx : public ObExprOperatorCtx
  {
    public:
    ObExprUDFCtx() :
    ObExprOperatorCtx(),
    param_store_buf_(nullptr),
    params_(nullptr),
    ctx_allocator_("UDFCtxAlloc", OB_MALLOC_NORMAL_BLOCK_SIZE) {}

    ~ObExprUDFCtx() {}

    int init_param_store(int param_num);
    void reuse()
    {
      if (OB_NOT_NULL(params_)) {
        params_->reuse();
      }
    }

    ParamStore* get_param_store() { return params_; }
    int64_t get_param_count() { return OB_ISNULL(params_) ? 0 : params_->count(); }

    private:
    void* param_store_buf_;
    ParamStore* params_;
    // ctx-level allocator: the ParamStore is built once per udf_ctx lifetime and
    // must NOT be allocated on exec_ctx.allocator_ (the outer SQL arena, freed only
    // at query end). Because PL's reset_expr_op() wipes the cached udf_ctx on every
    // PL call, build_udf_ctx() re-creates it and re-runs init_param_store() for each
    // call; allocating on the outer arena would leak ~per-call within one long SQL.
    // Using this member arena ties the ParamStore lifetime to the udf_ctx itself.
    common::ObArenaAllocator ctx_allocator_;
  };

  OB_UNIS_VERSION(1);
public:
  explicit ObExprUDF(common::ObIAllocator &alloc);
  virtual ~ObExprUDF();

  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_stack,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_udf(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);

  static int build_udf_ctx(int64_t udf_ctx_id,
                           int64_t param_num,
                           ObExecContext &exec_ctx,
                           ObExprUDFCtx *&udf_ctx);

  virtual inline void reset();
  virtual int assign(const ObExprOperator &other);
  inline void set_udf_id(int64_t udf_id) { udf_id_ = udf_id; }
  inline void set_udf_package_id(int64_t udf_package_id) { udf_package_id_ = udf_package_id; }
  inline int set_subprogram_path(const common::ObIArray<int64_t> &path)
  {
    return subprogram_path_.assign(path);
  }
  inline void set_result_type(const ObExprResType &result_type) { result_type_ = result_type; }
  inline int set_params_type(common::ObIArray<ObRawExprResType> &params_type)
  {
    return ObExprResultTypeUtil::assign_type_array(params_type, params_type_);
  }
  inline int set_params_desc(common::ObIArray<ObUDFParamDesc> &params_desc)
  {
    return params_desc_.assign(params_desc);
  }
  inline void set_loc(uint64_t loc) { loc_ = loc; }
  inline uint64_t get_loc() const { return loc_; }

  static int process_in_params(const common::ObObj *objs_stack,
                               int64_t param_num,
                               const common::ObIArray<ObUDFParamDesc> &params_desc,
                               const common::ObIArray<ObExprResType> &params_type,
                               common::ParamStore& iparams,
                               common::ObIAllocator &allocator,
                               ObIArray<ObObj> *deep_in_objs = NULL);
  static int process_out_params(const common::ObObj *objs_stack,
                                int64_t param_num,
                                common::ParamStore& iparams,
                                common::ObIAllocator &alloc,
                                ObExecContext &exec_ctx,
                                const common::ObIArray<ObUDFParamDesc> &params_desc,
                                const common::ObIArray<ObExprResType> &params_type);

  static int is_child_of(const ObObj &parent, const ObObj &child, bool &is_child);
  static int process_singal_out_param(int64_t i,
                                      ObIArray<bool> &dones,
                                      const ObObj *objs_stack,
                                      int64_t param_num,
                                      ParamStore& iparams,
                                      ObIAllocator &alloc,
                                      ObExecContext &exec_ctx,
                                      const ObIArray<ObUDFParamDesc> &params_desc,
                                      const ObIArray<ObExprResType> &params_type);

  static int process_package_out_param(int64_t idx,
                                       ObIArray<bool> &dones,
                                       const ObObj *objs_stack,
                                       int64_t param_num,
                                       ParamStore& iparams,
                                       ObIAllocator &alloc,
                                       ObExecContext &exec_ctx,
                                       const ObIArray<ObUDFParamDesc> &params_desc,
                                       const ObIArray<ObExprResType> &params_type);
  static int before_calc_result(share::schema::ObSchemaGetterGuard &schema_guard,
                                ObSqlCtx &sql_ctx,
                                ObExecContext &exec_ctx);
  static int after_calc_result(share::schema::ObSchemaGetterGuard &schema_guard,
                               ObSqlCtx &sql_ctx, ObExecContext &exec_ctx);
  static int need_deep_copy_in_parameter(const ObObj *objs_stack,
                                          int64_t param_num,
                                          const ObIArray<ObUDFParamDesc> &params_desc,
                                          const ObIArray<ObExprResType> &params_type,
                                          const ObObj &element,
                                          bool &need_deep_copy);
  static int extract_allocator_and_restore_obj(const ObObj &obj, ObObj &new_obj, ObIAllocator *&composite_alloc);
  int64_t get_udf_id() const { return udf_id_;}
  int64_t get_udf_package_id() const { return udf_package_id_;}
  const common::ObIArray<int64_t> &get_subprogram_path() const { return subprogram_path_;}
  const ObExprResType &get_result_type() const { return result_type_;}
  const common::ObIArray<ObExprResType> &get_params_type() const { return params_type_;}
  const common::ObIArray<ObUDFParamDesc> &get_params_desc() const { return params_desc_; }
  virtual bool need_rt_ctx() const override { return true; }

private:
  static int fill_obj_stack(const ObExpr &expr, ObEvalCtx &ctx, common::ObObj *objs);
  static int check_types(const ObExpr &expr, const ObExprUDFInfo &info);
  int64_t udf_id_;
  int64_t udf_package_id_;
  common::ObSEArray<int64_t, 8> subprogram_path_;
  ObExprResType result_type_;
  common::ObSEArray<ObExprResType, 5> params_type_;
  common::ObSEArray<ObUDFParamDesc, 5> params_desc_;
  bool reserved_udt_udf_;
  uint64_t loc_; // this is col and line number combination,
  bool reserved_udt_cons_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprUDF);
};

} //sql
} //oceanbase
#endif //OCEANBASE_SQL_OB_EXPR_USER_DEFINED_FUNC_H_
