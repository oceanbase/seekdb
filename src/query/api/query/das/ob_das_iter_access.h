/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_DAS_OB_DAS_ITER_ACCESS_H_
#define OCEANBASE_QUERY_API_DAS_OB_DAS_ITER_ACCESS_H_

#include <stdint.h>

namespace oceanbase
{
namespace sql
{
class ObDASScanIter;
}
namespace storage
{
class ObTableScanParam;
}
namespace query
{

int das_scan_next_row(sql::ObDASScanIter *iterator);
int das_scan_next_rows(
    sql::ObDASScanIter *iterator, int64_t &count, int64_t capacity);
int das_scan_reuse(sql::ObDASScanIter *iterator);
int das_scan_rescan(sql::ObDASScanIter *iterator);
int das_scan_advance(sql::ObDASScanIter *iterator);
void das_scan_reset(sql::ObDASScanIter *iterator);
void das_scan_set_param(
    sql::ObDASScanIter *iterator, storage::ObTableScanParam &scan_param);

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_DAS_OB_DAS_ITER_ACCESS_H_
