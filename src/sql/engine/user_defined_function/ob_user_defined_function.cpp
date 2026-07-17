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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_user_defined_function.h"
#include "sql/engine/user_defined_function/ob_udf_util.h"
#include "share/ob_i_sql_expression.h"


namespace oceanbase
{

using namespace common;

namespace sql
{

ObUdfFunction::~ObUdfFunction()
{
  IGNORE_RETURN ObUdfUtil::unload_so(dlhandle_);
}

/*
 * try to load the so file to ob.
 * try to load origin function and all helper function to ob.
 *
 * */
int ObUdfFunction::init(const share::schema::ObUDFMeta &udf_meta)
{
  int ret = OB_SUCCESS;
  if (udf_meta.dl_.empty() || udf_meta.name_.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the udf meta is invalid", K(ret));
  } else if (OB_FAIL(ObUdfUtil::load_so(udf_meta.dl_, dlhandle_))) {
    LOG_WARN("load so error", K(ret));
  } else if (OB_FAIL(ObUdfUtil::load_function(udf_meta.name_,
                                              dlhandle_,
                                              ObString::make_string(""),
                                              false, /* can't ignore error */
                                              func_origin_))) {
    // change the error code
    ret = OB_CANT_FIND_DL_ENTRY;
    LOG_WARN("load origin function failed", K(ret));
    LOG_USER_ERROR(OB_CANT_FIND_DL_ENTRY, udf_meta.name_.length(), udf_meta.name_.ptr());
  } else if (OB_FAIL(ObUdfUtil::load_function(udf_meta.name_,
                                              dlhandle_,
                                              ObString::make_string("_init"),
                                              false, /* can't ignore error */
                                              func_init_))) {
    LOG_WARN("load init function failed", K(ret));
  } else if (OB_FAIL(ObUdfUtil::load_function(udf_meta.name_,
                                              dlhandle_,
                                              ObString::make_string("_deinit"),
                                              true, /* ignore error */
                                              func_deinit_))) {
    LOG_WARN("load deinit function failed", K(ret));
  } else if (udf_meta.type_ == share::schema::ObUDF::UDFType::FUNCTION) {
    // do nothing
  } else if (OB_FAIL(ObUdfUtil::load_function(udf_meta.name_,
                                              dlhandle_,
                                              ObString::make_string("_clear"),
                                              false, /* ignore error */
                                              func_clear_))) {
    LOG_WARN("load clear function error", K(ret));
  } else if (OB_FAIL(ObUdfUtil::load_function(udf_meta.name_,
                                              dlhandle_,
                                              ObString::make_string("_add"),
                                              false, /* ignore error */
                                              func_add_))) {
    LOG_WARN("load add function error", K(ret));
  }
  if (OB_SUCC(ret)) {
    IGNORE_RETURN udf_meta_.assign(udf_meta);
  }
  return ret;
}

int ObUdfFunction::process_init_func(ObUdfFunction::ObUdfCtx &udf_ctx) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(func_init_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the init function is null", K(ret));
  } else {
    char init_msg_buff[OB_MYSQL_ERRMSG_SIZE];
    MEMSET(init_msg_buff, 0, OB_MYSQL_ERRMSG_SIZE);
    bool error = func_init_(&udf_ctx.udf_init_, &udf_ctx.udf_args_, init_msg_buff);
    if (error) {
      ret = OB_CANT_INITIALIZE_UDF;
      LOG_WARN("do init func failed", K(init_msg_buff), K(ret), K(error));
      LOG_USER_ERROR(OB_CANT_INITIALIZE_UDF, udf_meta_.name_.length(), udf_meta_.name_.ptr());
    }
  }
  IGNORE_RETURN ObUdfUtil::print_udf_args_to_log(udf_ctx.udf_args_);
  return ret;
}

void ObUdfFunction::process_deinit_func(ObUdfFunction::ObUdfCtx &udf_ctx) const
{
  if (OB_ISNULL(func_deinit_)) {
    LOG_DEBUG("the deinit function is null");
  } else {
    IGNORE_RETURN func_deinit_(&udf_ctx.udf_init_);
  }
}


int ObAggUdfFunction::process_origin_func(ObIAllocator &allocator,
                                          ObObj &agg_result,
                                          ObUdfCtx &udf_ctx) const
{
  int ret = OB_SUCCESS;
  ObUdfCtx *tmp_ctx = &udf_ctx;
  IGNORE_RETURN ObUdfUtil::print_udf_args_to_log(udf_ctx.udf_args_);
  //COPY_UDF_CTX_TO_STACK(udf_ctx, tmp_ctx);
  if (OB_FAIL(ObUdfUtil::process_udf_func(udf_meta_.ret_,
                                          allocator,
                                          udf_ctx.udf_init_,
                                          tmp_ctx->udf_args_,
                                          func_origin_,
                                          agg_result))) {
    LOG_WARN("failed to process udf function", K(ret));
  }
  return ret;
}



int ObAggUdfFunction::process_add_func(common::ObIAllocator &allocator,
                                       const common::ObDatum *datums,
                                       const common::ObIArray<ObExpr *> &exprs,
                                       ObUdfCtx &udf_ctx) const
{
  int ret = OB_SUCCESS;
  CK(NULL != func_add_ && NULL != datums && exprs.count() == udf_ctx.udf_args_.arg_count);
  for (int64_t i = 0; OB_SUCC(ret) && i < exprs.count(); i++) {
    OZ(ObUdfUtil::set_udf_arg(allocator, datums[i], *exprs.at(i), udf_ctx.udf_args_, i));
  }
  OZ(ObUdfUtil::process_add_func(udf_ctx.udf_init_, udf_ctx.udf_args_, func_add_));
  return ret;
}

int ObAggUdfFunction::process_clear_func(ObUdfCtx &udf_ctx) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObUdfUtil::process_clear_func(udf_ctx.udf_init_, func_clear_))) {
    LOG_WARN("failed to process add row", K(ret));
  }
  return ret;
}


OB_SERIALIZE_MEMBER(ObAggUdfMeta,
                    udf_meta_,
                    udf_attributes_,
                    udf_attributes_types_,
                    calculable_results_);

// we will des/ser the sql_calc_ at the operator group
OB_SERIALIZE_MEMBER(ObUdfConstArgs,
                    idx_in_udf_arg_);

}
}
