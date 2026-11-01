/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */
#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/file_table/ob_file_table_utils.h"
#include <new>
#include "common/json_type/ob_json_base.h"

namespace oceanbase
{
namespace sql
{
using namespace common;
namespace
{
int file_scan_text_to_obj(ObObjParam &obj, ObObjType type, const char *data,
                          int64_t len, bool is_null)
{
  int ret = OB_SUCCESS;
  obj.set_null();
  if (is_null || OB_ISNULL(data)) {
  } else if (ObIntType == type) {
    char buf[32];
    if (len <= 0 || len >= static_cast<int64_t>(sizeof(buf))) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      MEMCPY(buf, data, len);
      buf[len] = 0;
      obj.set_int(strtoll(buf, NULL, 10));
    }
  } else if (ObDoubleType == type) {
    char buf[64];
    if (len <= 0 || len >= static_cast<int64_t>(sizeof(buf))) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      MEMCPY(buf, data, len);
      buf[len] = 0;
      obj.set_double(strtod(buf, NULL));
    }
  } else if (ObTinyIntType == type) {
    bool value = false;
    if (!ObFileTypeInferrer::parse_boolean(data, len, value)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      obj.set_bool(value);
    }
  } else if (ObDateType == type) {
    int32_t value = 0;
    if (OB_FAIL(ObTimeConverter::str_to_date(ObString(static_cast<int32_t>(len), data), value))) {
    } else {
      obj.set_date(value);
    }
  } else if (ObDateTimeType == type) {
    int64_t value = 0;
    ObTimeConvertCtx ctx(NULL, false);
    if (OB_FAIL(ObTimeConverter::str_to_datetime(ObString(static_cast<int32_t>(len), data),
                                                 ctx, value, NULL))) {
    } else {
      obj.set_datetime(ObDateTimeType, value);
    }
  } else {
    obj.set_varchar(ObString(static_cast<int32_t>(len), data));
    obj.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  }
  return ret;
}

int file_scan_json_to_obj(ObObjParam &obj, ObObjType type, ObIJsonBase *node)
{
  int ret = OB_SUCCESS;
  obj.set_null();
  if (OB_ISNULL(node) || ObJsonNodeType::J_NULL == node->json_type()) {
  } else if (ObIntType == type) {
    obj.set_int(node->get_int());
  } else if (ObDoubleType == type) {
    obj.set_double(ObJsonNodeType::J_DOUBLE == node->json_type()
                       ? node->get_double() : static_cast<double>(node->get_int()));
  } else if (ObTinyIntType == type) {
    obj.set_bool(node->get_boolean());
  } else {
    ret = file_scan_text_to_obj(obj, type, node->get_data(), node->get_data_length(), false);
  }
  return ret;
}
} // end anonymous namespace

struct ObFileScanReader::Impl
{
  Impl() : format_(ObFileTableFormat::MAX), schema_(NULL) {}
  ObFileTableFormat format_;
  const ObFileColumnSchema *schema_;
  ObCsvReader csv_;
  ObParquetStreamReader parquet_;
};

ObFileScanReader::ObFileScanReader() : impl_(NULL) {}
ObFileScanReader::~ObFileScanReader() { close(); }
void ObFileScanReader::close()
{
  if (OB_NOT_NULL(impl_)) {
    impl_->csv_.close();
    impl_->parquet_.close();
    delete impl_;
    impl_ = NULL;
  }
}

int ObFileScanReader::open(ObIAllocator &allocator, const char *path,
                           ObFileTableFormat format, bool header,
                           const ObFileColumnSchema &schema,
                           const ObIArray<int64_t> *projected_columns)
{
  int ret = OB_SUCCESS;
  close();
  if (OB_ISNULL(path) || format >= ObFileTableFormat::MAX) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(impl_ = new (std::nothrow) Impl())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    impl_->format_ = format;
    impl_->schema_ = &schema;
    if (ObFileTableFormat::PARQUET == format) {
      ObFileColumnSchema parquet_schema;
      if (OB_FAIL(impl_->parquet_.open(allocator, path, parquet_schema, projected_columns))) {
      }
    } else if (OB_FAIL(impl_->csv_.open(path, 1 << 20))) {
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "cannot open file");
    } else if (ObFileTableFormat::CSV == format && header) {
      ObCsvRecord record;
      ObArray<bool> nulls;
      bool has_more = false;
      if (OB_FAIL(impl_->csv_.read_row(allocator, record, nulls, has_more)) || !has_more) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "csv file is empty");
      }
    }
  }
  if (OB_FAIL(ret)) {
    close();
  }
  return ret;
}

int ObFileScanReader::next_row(ObIAllocator &allocator, ObIArray<ObObjParam> &row,
                               bool &has_row)
{
  int ret = OB_SUCCESS;
  has_row = false;
  row.reset();
  if (OB_ISNULL(impl_) || OB_ISNULL(impl_->schema_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (ObFileTableFormat::PARQUET == impl_->format_) {
    ret = impl_->parquet_.next_row(allocator, row, has_row);
  } else if (ObFileTableFormat::CSV == impl_->format_) {
    ObCsvRecord record;
    ObArray<bool> nulls;
    if (OB_FAIL(impl_->csv_.read_row(allocator, record, nulls, has_row))) {
    } else if (has_row) {
      if (record.field_lens_.count() > impl_->schema_->column_cnt()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "csv field count changed during scan");
      }
      for (int64_t c = 0; OB_SUCC(ret) && c < impl_->schema_->column_cnt(); ++c) {
        ObObjParam cell;
        if (c >= record.field_lens_.count()) {
          cell.set_null();
        } else if (OB_FAIL(file_scan_text_to_obj(cell, impl_->schema_->types_.at(c),
                                                 record.field_ptrs_.at(c), record.field_lens_.at(c),
                                                 nulls.at(c)))) {
        }
        if (OB_SUCC(ret) && OB_FAIL(row.push_back(cell))) {
        }
      }
    }
  } else {
    char *line_buf = NULL;
    int64_t line_len = 0;
    while (OB_SUCC(ret) && !has_row) {
      if (OB_FAIL(impl_->csv_.read_line(allocator, line_buf, line_len, has_row))) {
      } else if (!has_row) {
        break;
      } else if (0 == line_len) {
        continue;
      } else {
        ObIJsonBase *base = NULL;
        ObString line(static_cast<int32_t>(line_len), line_buf);
        if (OB_FAIL(ObJsonBaseFactory::get_json_base(&allocator, line, ObJsonInType::JSON_TREE,
                                                     ObJsonInType::JSON_TREE, base))) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "invalid json line in file");
        } else if (OB_ISNULL(base) || ObJsonNodeType::J_OBJECT != base->json_type()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "jsonl line is not a flat object");
        } else {
          ObJsonObject *object = static_cast<ObJsonObject *>(base);
          for (int64_t c = 0; OB_SUCC(ret) && c < impl_->schema_->column_cnt(); ++c) {
            ObObjParam cell;
            if (OB_FAIL(file_scan_json_to_obj(cell, impl_->schema_->types_.at(c),
                                              object->get_value(impl_->schema_->names_.at(c))))) {
            } else if (OB_FAIL(row.push_back(cell))) {
            }
          }
        }
      }
    }
  }
  return ret;
}
} // namespace sql
} // namespace oceanbase
