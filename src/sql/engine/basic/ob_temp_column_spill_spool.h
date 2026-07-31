/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_SQL_ENGINE_BASIC_OB_TEMP_COLUMN_SPILL_SPOOL_H_
#define OCEANBASE_SQL_ENGINE_BASIC_OB_TEMP_COLUMN_SPILL_SPOOL_H_

#include "query/engine/basic/ob_spill_batch_spool.h"

namespace oceanbase
{
namespace sql
{

// SQL owns the production implementation.  Callers explicitly inject this
// factory through the stable query API; storage never looks it up itself.
query::ObISpillBatchSpoolFactory &get_temp_column_spill_spool_factory();

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_BASIC_OB_TEMP_COLUMN_SPILL_SPOOL_H_
