/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SQL_CREATE_EXTENSION_RESOLVER_H_
#define SEEKDB_SQL_CREATE_EXTENSION_RESOLVER_H_

#include "sql/resolver/cmd/ob_cmd_resolver.h"

namespace oceanbase { namespace sql {
class CreateExtensionResolver final : public ObCMDResolver
{
public:
  explicit CreateExtensionResolver(ObResolverParams &params) : ObCMDResolver(params) {}
  int resolve(const ParseNode &tree) override;
};
} }
#endif
