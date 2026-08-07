/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef OCEANBASE_SQL_ENGINE_BASIC_OB_FILE_SCAN_OP_H_
#define OCEANBASE_SQL_ENGINE_BASIC_OB_FILE_SCAN_OP_H_

#include <dirent.h>
#include <vector>

#include "sql/engine/ob_operator.h"
#include "sql/engine/basic/ob_file_scan_utils.h"

namespace oceanbase
{
namespace sql
{
class ObFileScanSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObFileScanSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type), kind_(static_cast<int32_t>(ObFileTableKind::INVALID)),
      file_path_(), secure_file_priv_(), file_format_(static_cast<int32_t>(ObFileFormat::INVALID)),
      device_(0), inode_(0), file_size_(0), modified_time_ns_(0),
      file_column_names_(alloc), file_column_types_(alloc),
      source_column_names_(alloc), source_original_names_(alloc),
      source_type_names_(alloc), source_column_types_(alloc), source_column_nullable_(alloc),
      output_column_idxs_(alloc), column_exprs_(alloc)
  {}

  int32_t kind_;
  common::ObString file_path_;
  common::ObString secure_file_priv_;
  int32_t file_format_;
  uint64_t device_;
  uint64_t inode_;
  int64_t file_size_;
  int64_t modified_time_ns_;
  common::ObFixedArray<common::ObString, common::ObIAllocator> file_column_names_;
  common::ObFixedArray<int32_t, common::ObIAllocator> file_column_types_;
  common::ObFixedArray<common::ObString, common::ObIAllocator> source_column_names_;
  common::ObFixedArray<common::ObString, common::ObIAllocator> source_original_names_;
  common::ObFixedArray<common::ObString, common::ObIAllocator> source_type_names_;
  common::ObFixedArray<int32_t, common::ObIAllocator> source_column_types_;
  common::ObFixedArray<int8_t, common::ObIAllocator> source_column_nullable_;
  common::ObFixedArray<int64_t, common::ObIAllocator> output_column_idxs_;
  common::ObFixedArray<ObExpr *, common::ObIAllocator> column_exprs_;
};

class ObFileScanOp : public ObOperator
{
public:
  ObFileScanOp(ObExecContext &ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObOperator(ctx, spec, input), reader_(), cells_(), batch_cells_(), directory_(nullptr),
      schema_row_idx_(0)
  {}
  virtual ~ObFileScanOp() {}

  virtual int inner_open() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_batch(const int64_t max_row_cnt) override;
  virtual int inner_rescan() override;
  virtual int inner_close() override;
  virtual void destroy() override { ObOperator::destroy(); }

private:
  int open_scan();
  int open_directory();
  int verify_file_fingerprint();
  int get_next_scan_row();
  int get_next_schema_row();
  int get_next_list_row();
  int project_cells();
  int write_cell(const ObFileCell &cell, ObFileColumnType type, ObDatum &datum);
  const ObFileScanSpec &file_spec() const
  { return static_cast<const ObFileScanSpec &>(spec_); }

private:
  ObFileScanReader reader_;
  std::vector<ObFileCell> cells_;
  std::vector<std::vector<ObFileCell> > batch_cells_;
  DIR *directory_;
  int64_t schema_row_idx_;
};
} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_BASIC_OB_FILE_SCAN_OP_H_
