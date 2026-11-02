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

#pragma once

#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/basic/ob_file_scan_utils.h"
#include <fstream>

namespace oceanbase
{
namespace sql
{

class ObExprReadCSV : public ObFuncExprOperator
{
public:
  explicit ObExprReadCSV(common::ObIAllocator &alloc);
  virtual ~ObExprReadCSV();
  
  virtual int calc_result_typeN(ObExprResType &type,
                                 ObExprResType *types_array,
                                 int64_t param_num,
                                 common::ObExprTypeCtx &type_ctx) const override;
  
  static int eval_read_csv(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  
  virtual bool need_rt_ctx() const override { return true; }

private:
  static constexpr int FILE_PATH_IDX = 0;
  static constexpr int HAS_HEADER_IDX = 1;
  static constexpr int DELIMITER_IDX = 2;
  
  DISALLOW_COPY_AND_ASSIGN(ObExprReadCSV);
};

class ObCSVFileScanner
{
public:
  ObCSVFileScanner();
  ~ObCSVFileScanner();
  
  int init(const common::ObString &file_path, const FileScanOption &option);
  int get_next_row(common::ObIArray<common::ObObj> &row);
  int get_schema(common::ObIArray<ColumnMeta> &schema);
  void reset();
  void close();
  
private:
  int parse_line(const std::string &line, common::ObIArray<common::ObString> &fields);
  int infer_type(const common::ObString &value, common::ObObjType &type);
  
  std::ifstream file_;
  FileScanOption option_;
  common::ObSEArray<ColumnMeta, 16> schema_;
  bool has_initialized_;
  int64_t row_count_;
};

} // namespace sql
} // namespace oceanbase
