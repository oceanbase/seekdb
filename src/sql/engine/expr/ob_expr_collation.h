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

#ifndef OCEANBASE_SQL_OB_EXPR_COLLATION_H_
#define OCEANBASE_SQL_OB_EXPR_COLLATION_H_

#include "sql/engine/expr/ob_expr_operator.h"
namespace oceanbase
{
namespace sql
{
/// Returns the character set of the string argument.
class ObExprCharset: public ObStringExprOperator
{
public:
  //ObExprCharset();
  explicit  ObExprCharset(common::ObIAllocator &alloc);
  virtual ~ObExprCharset();

  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, 
                       ObExpr &rt_expr) const;
private:
  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprCharset);
  // function members
private:
  // data members
};


/// Returns the collation of the string argument.
class ObExprCollation: public ObStringExprOperator
{
public:
  //ObExprCollation();
  explicit  ObExprCollation(common::ObIAllocator &alloc);
  virtual ~ObExprCollation();

  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, 
                       ObExpr &rt_expr) const;
private:
  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprCollation);
  // function members
private:
  // data members
};

/// Returns the collation coercibility value of the string argument.
/// @see ObCollationLevel
class ObExprCoercibility: public ObExprOperator
{
public:
  //ObExprCoercibility();
  explicit  ObExprCoercibility(common::ObIAllocator &alloc);
  virtual ~ObExprCoercibility();

  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, 
                       ObExpr &rt_expr) const;
private:
  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprCoercibility);
  // function members
private:
  // data members
};

/// change collation of the input argument
/// used to implement COLLATE clause, e.g. C1 collate utf8_general_ci, 'abc' collate utf8_bin
/// format: SET_COLLATION(expr, utf8_general_ci)
class ObExprSetCollation: public ObExprOperator
{
public:
  //ObExprSetCollation();
  explicit  ObExprSetCollation(common::ObIAllocator &alloc);
  virtual ~ObExprSetCollation();

  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, 
                       ObExpr &rt_expr) const;
private:
  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprSetCollation);
  // function members
private:
  // data members
};

/// Returns the meta used for comparison
/// @note for debug purpose
class ObExprCmpMeta: public ObStringExprOperator
{
public:
  //ObExprCmpMeta();
  explicit  ObExprCmpMeta(common::ObIAllocator &alloc);
  virtual ~ObExprCmpMeta();

  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, 
                       ObExpr &rt_expr) const;

private:
  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprCmpMeta);
  // function members
private:
  // data members
};

} // end namespace sql
} // end namespace oceanbase

#endif //OCEANBASE_SQL_OB_EXPR_COLLATION_H_
