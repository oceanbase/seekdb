/**
 * (c) 2025 OceanBase
 * This file contains utilities for reading local files as tables via SQL.
 * It implements option parsing, deterministic type inference, and change detection.
 */

#pragma once

#include "sql/engine/cmd/ob_load_data_parser.h"
#include "sql/parser/ob_item_type.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/page_arena.h"
#include "lib/charset/ob_charset.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace sql
{

/**
 * The file formats a table function can read.
 */
enum class ObFileScanFormat
{
  CSV = 0,
  JSON = 1,
  PARQUET = 2,
  GLOB = 3,
};

/**
 * A single argument of a file table function, either the positional file path
 * or a named option such as ``header = true``.
 */
struct ObFileSqlArg
{
  enum Kind
  {
    STRING = 0,
    INT,
    BOOL
  };

  ObFileSqlArg() : kind_(STRING), int_value_(0) {}

  Kind kind_;
  ObString name_;    // empty for positional args
  ObString str_value_;
  int64_t int_value_;
};

/**
 * The inferred schema of one output column.
 */
struct ObFileScanColMeta
{
  ObFileScanColMeta() : type_(ObNullType), length_(0), nullable_(false) {}

  ObString name_;
  ObObjType type_;
  int32_t length_;    // max length in characters, only meaningful for strings
  bool nullable_;
};

/**
 * Helpers shared by the file table functions (read_csv / read_json / read_parquet / glob).
 */
class ObFileSqlUtils
{
public:
  /**
   * map function name (case-insensitive) to its format.
   * Returns OB_NOT_SUPPORTED if the function name is unknown.
   */
  static int get_format_type(const ObString &func_name, ObFileScanFormat &type);

  /**
   * build an ObCSVGeneralFormat from named arguments (DuckDB-style).
   * supported options: header, delimiter, quote, escape, null, compression, encoding, skip_blank_lines, trim_space.
   * returns OB_NOT_SUPPORTED for unsupported options.
   */
  static int build_csv_format(const common::ObIArray<ObFileSqlArg> &args,
                              common::ObIAllocator &allocator,
                              ObCSVGeneralFormat &csv_format);

  /**
   * build an ObCSVGeneralFormat for JSONL:
   *   sample_size := X (-1 accepted only), compression, encoding
   * For JSONL, we reuse csv_format fields for line_term_str_, cs_type_, compression_algorithm_.
   * returns OB_NOT_SUPPORTED for unsupported options.
   */
  static int build_json_format(const common::ObIArray<ObFileSqlArg> &args,
                               common::ObIAllocator &allocator,
                               ObCSVGeneralFormat &json_format);

  /**
   * column-level type inference state machine.
   * feed a single cell value (as UTF-8 string) repeatedly; the state converges to the best-fit type.
   * Supported types: NULL, BOOLEAN, BIGINT, DOUBLE, VARCHAR, DATE, DATETIME.
   * Widening rules:
   *   BOOLEAN + any numeric -> VarChar
   *   DOUBLE + INT -> DOUBLE
   *   INT overflow int64 -> DOUBLE
   *   mixed VARCHAR + anything -> VARCHAR
   *   DATE + DATETIME -> DATETIME
   *   any conflict -> VARCHAR
   *
   * Call after feeding all non-empty rows: finish(), which populates the inferred data type and max varchar length.
   */
  struct ColInfer
  {
    enum State
    {
      ST_NULL,
      ST_BOOLEAN,
      ST_INT,
      ST_DOUBLE,
      ST_DATE,
      ST_DATETIME,
      ST_VARCHAR
    };

    ColInfer()
        : state_(ST_NULL),
          bigint_min_(INT64_MIN),
          bigint_max_(INT64_MAX),
          max_varchar_len_(0),
          datetime_first_(false)
    {}

    /// feed one field value (empty means NULL); returns true if state changed
    bool feed_field(const ObString &value, const ObCharsetType cs_type);

    /// called after all cells: populate the final type and max varchar length
    void finish(ObObjType &data_type, int32_t &max_varchar_len, ObCollationType &collation_type);

    State state_;
    int64_t bigint_min_;  // tracks min/max if in INT state
    int64_t bigint_max_;
    int32_t max_varchar_len_;
    bool datetime_first_;
  };

  /**
   * infer all columns for CSV/JSONL by scanning the file line-by-line once.
   * The column names are taken from the CSV header (or, for JSONL, from the union of
   * the keys of all records, in first-seen order).
   */
  static int infer_column_schemas(const ObString &file_url,
                                  ObFileScanFormat format,
                                  const ObCSVGeneralFormat &format_opt,
                                  common::ObIAllocator &allocator,
                                  common::ObIArray<ObFileScanColMeta> &col_metas,
                                  int64_t *row_cnt = NULL);

  /**
   * compute a change signature (path, size, mtime) for a file URL
   */
};

} // namespace sql
} // namespace oceanbase
