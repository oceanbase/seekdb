/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_OB_SQL_NAME_SERVICE_H_
#define OCEANBASE_QUERY_API_OB_SQL_NAME_SERVICE_H_

#include "lib/charset/ob_charset.h"
#include "lib/ob_define.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace query
{

// Public SQL identifier policy used by command implementations outside the
// query module.  Parser/resolver utilities remain private to SQL.
class ObSQLNameService
{
public:
  static int check_and_convert_database_name(
      common::ObCollationType cs_type,
      bool preserve_lettercase,
      common::ObString &name);

  static int check_and_convert_table_name(
      common::ObCollationType cs_type,
      bool preserve_lettercase,
      common::ObString &name);

  static int resolve_table_name(
      common::ObCollationType cs_type,
      common::ObNameCaseMode case_mode,
      const common::ObString &name,
      common::ObString &database_name,
      common::ObString &table_name);

};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_OB_SQL_NAME_SERVICE_H_
