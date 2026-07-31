/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_RESOLVER_OB_RAW_EXPR_TRAITS_H_
#define OCEANBASE_QUERY_API_RESOLVER_OB_RAW_EXPR_TRAITS_H_

namespace oceanbase
{
namespace sql
{
class ObRawExpr;
}
namespace query
{

bool is_topn_filter(const sql::ObRawExpr *expr);

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_RESOLVER_OB_RAW_EXPR_TRAITS_H_
