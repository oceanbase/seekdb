/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */
#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/basic/ob_file_scan_op.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
OB_SERIALIZE_MEMBER((ObFileScanSpec, ObOpSpec), kind_, file_path_, secure_file_priv_, file_format_,
                    device_, inode_, file_size_, modified_time_ns_,
                    file_column_names_, file_column_types_, source_column_names_,
                    source_original_names_, source_type_names_, source_column_types_, source_column_nullable_,
                    output_column_idxs_, column_exprs_);

int ObFileScanOp::open_scan()
{
  int ret = OB_SUCCESS;
  std::vector<ObFileColumnSchema> columns;
  std::vector<int64_t> projected_column_idxs;
  if (file_spec().file_column_names_.count() != file_spec().file_column_types_.count()) {
    ret = OB_ERR_UNEXPECTED;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < file_spec().file_column_names_.count(); ++i) {
    ObFileColumnSchema column;
    const ObString &name = file_spec().file_column_names_.at(i);
    column.source_name_.assign(name.ptr(), name.length());
    column.column_name_ = column.source_name_;
    column.type_ = static_cast<ObFileColumnType>(file_spec().file_column_types_.at(i));
    columns.push_back(column);
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < file_spec().output_column_idxs_.count(); ++i) {
    projected_column_idxs.push_back(file_spec().output_column_idxs_.at(i));
  }
  if (OB_SUCC(ret) && OB_FAIL(reader_.open(
        std::string(file_spec().file_path_.ptr(), file_spec().file_path_.length()),
        static_cast<ObFileFormat>(file_spec().file_format_), columns,
        file_spec().device_, file_spec().inode_, file_spec().file_size_,
        file_spec().modified_time_ns_, projected_column_idxs))) {
    LOG_WARN("failed to open file scan reader", K(ret), K(file_spec().file_path_));
  }
  return ret;
}

int ObFileScanOp::open_directory()
{
  int ret = OB_SUCCESS;
  std::string canonical_path;
  uint64_t device = 0;
  uint64_t inode = 0;
  int64_t modified_time_ns = 0;
  const std::string path(file_spec().file_path_.ptr(), file_spec().file_path_.length());
  if (OB_FAIL(ObFileScanUtils::get_directory_fingerprint(
        path, canonical_path, device, inode, modified_time_ns))) {
    LOG_WARN("failed to verify file list directory", K(ret));
  } else if (canonical_path != path || device != file_spec().device_
             || inode != file_spec().inode_) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("file list directory changed", K(ret));
  } else {
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat directory_stat;
    if (fd < 0) {
      ret = OB_FILE_NOT_OPENED;
    } else if (0 != fstat(fd, &directory_stat)
               || static_cast<uint64_t>(directory_stat.st_dev) != file_spec().device_
               || static_cast<uint64_t>(directory_stat.st_ino) != file_spec().inode_
               || (static_cast<int64_t>(directory_stat.st_mtim.tv_sec) * 1000000000L
                   + static_cast<int64_t>(directory_stat.st_mtim.tv_nsec))
                  != file_spec().modified_time_ns_) {
      ::close(fd);
      ret = OB_SCHEMA_EAGAIN;
      LOG_WARN("opened directory does not match resolved fingerprint", K(ret));
    } else if (OB_ISNULL(directory_ = fdopendir(fd))) {
      ::close(fd);
      ret = OB_FILE_NOT_OPENED;
    }
  }
  return ret;
}

int ObFileScanOp::verify_file_fingerprint()
{
  int ret = OB_SUCCESS;
  std::string canonical_path;
  uint64_t device = 0;
  uint64_t inode = 0;
  int64_t file_size = 0;
  int64_t modified_time_ns = 0;
  const std::string path(file_spec().file_path_.ptr(), file_spec().file_path_.length());
  if (OB_FAIL(ObFileScanUtils::get_file_fingerprint(
        path, canonical_path, device, inode, file_size, modified_time_ns))) {
    LOG_WARN("failed to verify file schema fingerprint", K(ret));
  } else if (canonical_path != path || device != file_spec().device_
             || inode != file_spec().inode_ || file_size != file_spec().file_size_
             || modified_time_ns != file_spec().modified_time_ns_) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("file changed after schema resolution", K(ret));
  }
  return ret;
}

int ObFileScanOp::inner_open()
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = ctx_.get_my_session();
  ObString current_secure_file_priv;
  bool enable_file_sql = false;
  schema_row_idx_ = 0;
  const ObFileTableKind kind = static_cast<ObFileTableKind>(file_spec().kind_);
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(session->get_sys_variable(share::SYS_VAR_ENABLE_FILE_SQL,
                                               enable_file_sql))) {
    LOG_WARN("failed to get enable_file_sql at execution", K(ret));
  } else if (!enable_file_sql) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("file sql was disabled after resolution", K(ret));
  } else if (OB_FAIL(session->get_secure_file_priv(current_secure_file_priv))) {
    LOG_WARN("failed to get current secure_file_priv", K(ret));
  } else if (current_secure_file_priv != file_spec().secure_file_priv_) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("secure_file_priv changed after file sql resolution", K(ret));
  } else if (ObFileTableKind::SCAN == kind) {
    ret = open_scan();
  } else if (ObFileTableKind::LIST == kind) {
    ret = open_directory();
  } else if (ObFileTableKind::SCHEMA == kind) {
    ret = verify_file_fingerprint();
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObFileScanOp::get_next_scan_row()
{
  return reader_.get_next_row(cells_);
}

int ObFileScanOp::get_next_schema_row()
{
  int ret = OB_SUCCESS;
  if (schema_row_idx_ >= file_spec().source_column_names_.count()) {
    if (OB_FAIL(verify_file_fingerprint())) {
    } else {
      ret = OB_ITER_END;
    }
  } else if (file_spec().source_column_names_.count()
             != file_spec().source_column_types_.count()
             || file_spec().source_column_names_.count()
                != file_spec().source_original_names_.count()
             || file_spec().source_column_names_.count()
                != file_spec().source_type_names_.count()
             || file_spec().source_column_names_.count()
                != file_spec().source_column_nullable_.count()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    cells_.assign(6, ObFileCell());
    cells_[0].is_null_ = false;
    cells_[0].int_value_ = schema_row_idx_ + 1;
    cells_[1].is_null_ = false;
    cells_[1].string_value_.assign(file_spec().source_column_names_.at(schema_row_idx_).ptr(),
                                   file_spec().source_column_names_.at(schema_row_idx_).length());
    cells_[2].is_null_ = false;
    cells_[2].string_value_.assign(file_spec().source_original_names_.at(schema_row_idx_).ptr(),
                                   file_spec().source_original_names_.at(schema_row_idx_).length());
    const ObFileColumnType type = static_cast<ObFileColumnType>(
      file_spec().source_column_types_.at(schema_row_idx_));
    cells_[3].is_null_ = false;
    cells_[3].string_value_ = ObFileScanUtils::column_type_name(type);
    cells_[4].is_null_ = false;
    cells_[4].string_value_.assign(file_spec().source_type_names_.at(schema_row_idx_).ptr(),
                                   file_spec().source_type_names_.at(schema_row_idx_).length());
    cells_[5].is_null_ = false;
    cells_[5].bool_value_ = 0 != file_spec().source_column_nullable_.at(schema_row_idx_);
    ++schema_row_idx_;
  }
  return ret;
}

int ObFileScanOp::get_next_list_row()
{
  int ret = OB_SUCCESS;
  struct dirent *entry = nullptr;
  struct stat entry_stat;
  while (OB_SUCC(ret) && OB_NOT_NULL(entry = readdir(directory_))) {
    if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
      continue;
    }
    if (0 != fstatat(dirfd(directory_), entry->d_name, &entry_stat, AT_SYMLINK_NOFOLLOW)) {
      continue;
    }
    const std::string directory_path(file_spec().file_path_.ptr(), file_spec().file_path_.length());
    const std::string full_path = directory_path + "/" + entry->d_name;
    ObFileFormat format = ObFileFormat::INVALID;
    const bool queryable = S_ISREG(entry_stat.st_mode)
                           && OB_SUCCESS == ObFileScanUtils::detect_format(full_path, format);
    cells_.assign(6, ObFileCell());
    cells_[0].is_null_ = false;
    cells_[0].string_value_ = entry->d_name;
    cells_[1].is_null_ = false;
    cells_[1].string_value_ = full_path;
    cells_[2].is_null_ = false;
    cells_[2].string_value_ = queryable ? ObFileScanUtils::format_name(format) : "unknown";
    cells_[3].is_null_ = false;
    cells_[3].int_value_ = static_cast<int64_t>(entry_stat.st_size);
    cells_[4].is_null_ = false;
    cells_[4].datetime_value_ = static_cast<int64_t>(entry_stat.st_mtime) * 1000000L;
    cells_[5].is_null_ = false;
    cells_[5].bool_value_ = queryable;
    break;
  }
  if (OB_SUCC(ret) && OB_ISNULL(entry)) {
    struct stat directory_stat;
    if (0 != fstat(dirfd(directory_), &directory_stat)
        || static_cast<uint64_t>(directory_stat.st_dev) != file_spec().device_
        || static_cast<uint64_t>(directory_stat.st_ino) != file_spec().inode_
        || (static_cast<int64_t>(directory_stat.st_mtim.tv_sec) * 1000000000L
            + static_cast<int64_t>(directory_stat.st_mtim.tv_nsec))
           != file_spec().modified_time_ns_) {
      ret = OB_SCHEMA_EAGAIN;
      LOG_WARN("directory changed while listing", K(ret));
    } else {
      ret = OB_ITER_END;
    }
  }
  return ret;
}

int ObFileScanOp::project_cells()
{
  int ret = OB_SUCCESS;
  clear_evaluated_flag();
  if (file_spec().column_exprs_.count() != file_spec().output_column_idxs_.count()) {
    ret = OB_ERR_UNEXPECTED;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < file_spec().column_exprs_.count(); ++i) {
    const int64_t column_idx = file_spec().output_column_idxs_.at(i);
    ObExpr *expr = file_spec().column_exprs_.at(i);
    if (OB_ISNULL(expr) || column_idx < 0
        || column_idx >= static_cast<int64_t>(cells_.size())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
      const ObFileCell &cell = cells_.at(column_idx);
      const ObFileColumnType type = static_cast<ObFileColumnType>(
        file_spec().file_column_types_.at(column_idx));
      OZ (write_cell(cell, type, datum));
      OX (expr->set_evaluated_projected(eval_ctx_));
    }
  }
  return ret;
}

int ObFileScanOp::write_cell(const ObFileCell &cell,
                             const ObFileColumnType type,
                             ObDatum &datum)
{
  int ret = OB_SUCCESS;
  if (cell.is_null_ || ObFileColumnType::NULL_TYPE == type) {
    datum.set_null();
  } else if (ObFileColumnType::BOOLEAN == type) {
    datum.set_int(cell.bool_value_ ? 1 : 0);
  } else if (ObFileColumnType::BIGINT == type) {
    datum.set_int(cell.int_value_);
  } else if (ObFileColumnType::DOUBLE == type) {
    datum.set_double(cell.double_value_);
  } else if (ObFileColumnType::DATE == type) {
    datum.set_date(cell.date_value_);
  } else if (ObFileColumnType::DATETIME == type) {
    datum.set_datetime(cell.datetime_value_);
  } else if (ObFileColumnType::VARCHAR == type) {
    datum.set_string(cell.string_value_.data(), cell.string_value_.length());
  } else {
    ret = OB_NOT_SUPPORTED;
  }
  return ret;
}

int ObFileScanOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ctx_.check_status())) {
    LOG_WARN("file scan was interrupted", K(ret));
  } else {
    const ObFileTableKind kind = static_cast<ObFileTableKind>(file_spec().kind_);
    if (ObFileTableKind::SCAN == kind) {
      ret = get_next_scan_row();
    } else if (ObFileTableKind::SCHEMA == kind) {
      ret = get_next_schema_row();
    } else if (ObFileTableKind::LIST == kind) {
      ret = get_next_list_row();
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
    if (OB_SUCC(ret)) {
      ret = project_cells();
    }
  }
  return ret;
}

int ObFileScanOp::inner_get_next_batch(const int64_t max_row_cnt)
{
  int ret = OB_SUCCESS;
  bool is_end = false;
  const int64_t batch_limit = std::min(max_row_cnt, file_spec().max_batch_size_);
  batch_cells_.clear();
  clear_evaluated_flag();
  clear_datum_eval_flag();
  while (OB_SUCC(ret) && !is_end
         && static_cast<int64_t>(batch_cells_.size()) < batch_limit) {
    if (OB_FAIL(ctx_.check_status())) {
      LOG_WARN("file batch scan was interrupted", K(ret));
    } else {
      const ObFileTableKind kind = static_cast<ObFileTableKind>(file_spec().kind_);
      if (ObFileTableKind::SCAN == kind) {
        ret = get_next_scan_row();
      } else if (ObFileTableKind::SCHEMA == kind) {
        ret = get_next_schema_row();
      } else if (ObFileTableKind::LIST == kind) {
        ret = get_next_list_row();
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        is_end = true;
      } else if (OB_SUCC(ret)) {
        batch_cells_.push_back(cells_);
      }
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < file_spec().column_exprs_.count(); ++i) {
    const int64_t column_idx = file_spec().output_column_idxs_.at(i);
    ObExpr *expr = file_spec().column_exprs_.at(i);
    if (OB_ISNULL(expr) || column_idx < 0
        || column_idx >= file_spec().file_column_types_.count()) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObDatum *datums = expr->locate_batch_datums(eval_ctx_);
      const int64_t datum_count = expr->is_batch_result()
                                ? static_cast<int64_t>(batch_cells_.size())
                                : std::min<int64_t>(1, batch_cells_.size());
      for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < datum_count; ++row_idx) {
        if (column_idx >= static_cast<int64_t>(batch_cells_.at(row_idx).size())) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(write_cell(batch_cells_.at(row_idx).at(column_idx),
                                     static_cast<ObFileColumnType>(
                                       file_spec().file_column_types_.at(column_idx)),
                                     datums[row_idx]))) {
          LOG_WARN("failed to write file batch cell", K(ret), K(row_idx), K(column_idx));
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    brs_.size_ = batch_cells_.size();
    brs_.end_ = is_end;
  }
  return ret;
}

int ObFileScanOp::inner_rescan()
{
  int ret = inner_close();
  if (OB_SUCC(ret)) {
    ret = inner_open();
  }
  return ret;
}

int ObFileScanOp::inner_close()
{
  reader_.close();
  cells_.clear();
  batch_cells_.clear();
  schema_row_idx_ = 0;
  if (OB_NOT_NULL(directory_)) {
    closedir(directory_);
    directory_ = nullptr;
  }
  return OB_SUCCESS;
}
} // namespace sql
} // namespace oceanbase
