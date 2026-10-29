/**
 * Copyright (c) 2026 OceanBase
 *
 * OceanBase is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * OceanBase is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with OceanBase. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "sql/engine/file_source/file_source_def.h"

#include <string>

namespace oceanbase {
namespace sql {
namespace file_source {

/// Deterministic type inference for a single column.
///
/// The accumulator starts as `NULL_TYPE` and is widened by every observed
/// value. The widening lattice is:
///
///   NULL_TYPE < BOOLEAN < BIGINT < DOUBLE
///   NULL_TYPE < DATE < DATETIME
///   anything < VARCHAR
///
/// A column that only contains NULL values stays `NULL_TYPE`.
class ColumnTypeAccumulator
{
public:
  ColumnTypeAccumulator() : type_(ColumnType::NULL_TYPE), has_null_(false) {}

public:
  /// Feed a NULL value into the accumulator.
  void feed_null();
  /// Feed a non NULL textual value into the accumulator.
  void feed_value(const std::string &text);
  /// Merge another accumulator into this one.
  void merge(const ColumnTypeAccumulator &other);

  ColumnType type() const { return type_; }
  bool has_null() const { return has_null_; }
  bool is_all_null() const { return ColumnType::NULL_TYPE == type_; }

  /// Infer the type of a single literal value.
  static ColumnType infer_literal_type(const std::string &text);
  /// Widen two already inferred types.
  static ColumnType merge_types(ColumnType lhs, ColumnType rhs);

  /// Check whether the text is a valid `YYYY-MM-DD` date.
  static bool is_date_literal(const std::string &text);
  /// Check whether the text is a valid `YYYY-MM-DD[ T]HH:MM:SS[.ffffff]` value.
  static bool is_datetime_literal(const std::string &text);

private:
  ColumnType type_;
  bool has_null_;
};

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
