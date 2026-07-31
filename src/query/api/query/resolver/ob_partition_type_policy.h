/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_RESOLVER_OB_PARTITION_TYPE_POLICY_H_
#define OCEANBASE_QUERY_API_RESOLVER_OB_PARTITION_TYPE_POLICY_H_

#include "common/object/ob_object.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace query
{

// Query-language rules for deciding which SQL data types can participate in
// each partitioning form.  The rules are public; resolver implementation is
// not.
class ObPartitionTypePolicy
{
public:
  static bool requires_range_columns(const common::ObObjType type)
  {
    return common::ob_is_float_tc(type)
        || common::ob_is_double_tc(type)
        || common::ob_is_decimal_int_tc(type)
        || common::ob_is_datetime_or_mysql_datetime_tc(type)
        || common::ob_is_string_tc(type)
        || common::ob_is_date_or_mysql_date(type)
        || common::ob_is_time_tc(type)
        || common::ob_is_number_tc(type);
  }

  static bool is_valid_partition_column(
      const common::ObObjType type,
      const share::schema::ObPartitionFuncType part_type,
      const bool is_check_value,
      const bool is_string_lob = false)
  {
    bool valid = false;
    UNUSED(is_check_value);
    if (share::schema::is_key_part(part_type)) {
      valid = (!common::ob_is_text_tc(type)
          && !common::ob_is_json_tc(type)
          && !common::ob_is_collection_sql_type(type))
          || is_string_lob;
    } else if (share::schema::PARTITION_FUNC_TYPE_HASH == part_type
        || share::schema::PARTITION_FUNC_TYPE_RANGE == part_type
        || share::schema::PARTITION_FUNC_TYPE_LIST == part_type) {
      valid = common::ob_is_integer_type(type)
          || common::ObYearType == type
          || common::ObBitType == type;
    } else if (share::schema::PARTITION_FUNC_TYPE_RANGE_COLUMNS == part_type
        || share::schema::PARTITION_FUNC_TYPE_LIST_COLUMNS == part_type) {
      const common::ObObjTypeClass type_class = common::ob_obj_type_class(type);
      valid = is_string_lob
          || common::ObIntTC == type_class
          || common::ObUIntTC == type_class
          || (common::ObDateTimeTC == type_class && common::ObTimestampType != type)
          || common::ObDateTC == type_class
          || common::ObStringTC == type_class
          || common::ObYearTC == type_class
          || common::ObTimeTC == type_class
          || common::ObMySQLDateTimeTC == type_class
          || common::ObMySQLDateTC == type_class
          || (share::schema::PARTITION_FUNC_TYPE_RANGE_COLUMNS == part_type
              && requires_range_columns(type));
    }
    return valid;
  }
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_RESOLVER_OB_PARTITION_TYPE_POLICY_H_
