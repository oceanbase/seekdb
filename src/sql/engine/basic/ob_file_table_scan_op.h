/**
 * (c) 2025 OceanBase
 * SPDX-License-Identifier: Apache-2.0
 *
 * Physical operator for the file-as-database table functions.
 *
 * The operator streams records out of a local file.  It never materializes
 * the whole file: only the record being converted is kept in memory and the
 * output datums are allocated from a per-batch arena.
 */

#pragma once

#include "sql/engine/ob_operator.h"
#include "sql/engine/basic/ob_file_csv_reader.h"
#include "sql/engine/basic/ob_file_scan_utils.h"
#include "sql/engine/cmd/ob_load_data_parser.h"

namespace oceanbase
{
namespace sql
{

class ObFileTableScanSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObFileTableScanSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type);
  INHERIT_TO_STRING_KV("op_spec", ObOpSpec, K_(file_path), K_(format),
                       K_(column_count), K_(row_count), K_(file_size),
                       K_(file_mtime));
  // output column expressions and their field index in the file
  common::ObFixedArray<ObExpr *, common::ObIAllocator> column_exprs_;
  common::ObFixedArray<int64_t, common::ObIAllocator> column_indexs_;
  // absolute path of the file, resolved at resolve time
  common::ObString file_path_;
  // ObFileScanFormat
  int32_t format_;
  // total number of columns described by the file schema
  int64_t column_count_;
  // number of data rows estimated at resolve time
  int64_t row_count_;
  // file signature captured at resolve time, used to detect file changes
  int64_t file_size_;
  int64_t file_mtime_;
  // csv format options
  common::ObString field_term_;
  int64_t field_enclosed_char_;
  int64_t field_escaped_char_;
  common::ObString null_str_;
  int32_t cs_type_;
  int64_t skip_header_lines_;
  bool parse_header_;
  bool skip_blank_lines_;
  bool trim_space_;
  bool empty_field_as_null_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObFileTableScanSpec);
};

class ObFileTableScanOp : public ObOperator
{
public:
  ObFileTableScanOp(ObExecContext &exec_ctx,
                    const ObOpSpec &spec,
                    ObOpInput *input);
  virtual ~ObFileTableScanOp();
  virtual int inner_open() override;
  virtual int inner_rescan() override;
  virtual int inner_get_next_row() override;
  virtual int inner_close() override;
  virtual void destroy() override;
private:
  int build_csv_format(common::ObIAllocator &allocator);
  int check_file_unchanged(const int64_t file_size, const int64_t file_mtime);
  int convert_field(const ObFileCsvField &field,
                    const ObObjType type,
                    ObDatum &datum);
  int fill_row();
private:
  ObFileCsvReader reader_;
  ObCSVGeneralFormat csv_format_;
  common::ObSEArray<ObFileCsvField, 16> fields_;
  // storage of the string datums of the current batch
  common::ObArenaAllocator str_alloc_;
  DISALLOW_COPY_AND_ASSIGN(ObFileTableScanOp);
};

} // namespace sql
} // namespace oceanbase
