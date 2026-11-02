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

#ifndef OCEANBASE_SRC_SQL_ENGINE_EXPR_OB_EXPR_PL_GET_ASSOCIATIVE_INDEX_H_
#define OCEANBASE_SRC_SQL_ENGINE_EXPR_OB_EXPR_PL_GET_ASSOCIATIVE_INDEX_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "pl/ob_pl_user_type.h"

namespace oceanbase
{
namespace sql
{
class ObExprPLAssocIndex : public ObExprOperator
{
  OB_UNIS_VERSION(1);

public:
  explicit ObExprPLAssocIndex(common::ObIAllocator &alloc);
  virtual ~ObExprPLAssocIndex();

  virtual void reset();
  int assign(const ObExprOperator &other);
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const;

  inline void set_write(bool for_write) { info_.for_write_ = for_write; }
  inline void set_out_of_range_set_err(bool v) { info_.out_of_range_set_err_ = v; }
  inline void set_parent_expr_type(pl::parent_expr_type type) { info_.parent_expr_type_ = type; }
  inline void set_is_index_by_varchar(bool v) { info_.is_index_by_varchar_ = v; }


  VIRTUAL_TO_STRING_KV(N_EXPR_TYPE, get_type_name(type_),
                       N_EXPR_NAME, name_,
                       N_PARAM_NUM, param_num_,
                       N_DIM, row_dimension_,
                       N_REAL_PARAM_NUM, real_param_num_,
                       K_(info_.for_write),
                       K_(info_.out_of_range_set_err),
                       K_(info_.parent_expr_type),
                       K_(info_.is_index_by_varchar));

  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr, ObExpr &rt_expr) const override;
  static int eval_assoc_idx(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);

  struct Info {
    Info()
        : for_write_(false),
        out_of_range_set_err_(true),
        parent_expr_type_(pl::parent_expr_type::EXPR_UNKNOWN),
        is_index_by_varchar_(false)
    {
    }

    TO_STRING_KV(K(for_write_), K(out_of_range_set_err_), K(parent_expr_type_), K(is_index_by_varchar_));

    union {
      struct {
        bool for_write_;
        bool out_of_range_set_err_;
        pl::parent_expr_type parent_expr_type_;
        bool is_index_by_varchar_;
      } __attribute__((packed));
      uint64_t v_;
    };
  };

  static_assert(sizeof(Info) == 8, "unexpected size");

  DISALLOW_COPY_AND_ASSIGN(ObExprPLAssocIndex);

  Info info_;
};

}
}

#endif /* OCEANBASE_SRC_SQL_ENGINE_EXPR_OB_EXPR_PL_ASSOCIATIVE_INDEX_H_ */
