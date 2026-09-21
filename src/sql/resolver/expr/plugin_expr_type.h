// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_SQL_PLUGIN_EXPR_TYPE_H_
#define SEEKDB_SQL_PLUGIN_EXPR_TYPE_H_
#include "common/object/ob_obj_type.h"
#include "lib/string/ob_string.h"

namespace oceanbase { namespace sql {
// Optional compilation metadata, allocated only for plugin-typed expressions.
// Never a field of ObDatum/ObObj or a pointer copied into an execution plan.
// Stored values need a codec before they are usable as plugin runtime values.
struct PluginExprType {
  common::ObString logical_id_;
  common::ObObjType physical_type_ = common::ObMaxType;
  bool stored_ = false;
  common::ObString sql_name_, owner_, format_;
  uint32_t format_version_ = 0;
  // Query binding version, never durable column metadata. Zero is allowed
  // only for a stored schema identity whose runtime codec is not bound yet.
  uint64_t catalog_epoch_ = 0;

  bool operator==(const PluginExprType &other) const {
    return logical_id_ == other.logical_id_ && physical_type_ == other.physical_type_ &&
        stored_ == other.stored_ && sql_name_ == other.sql_name_ && owner_ == other.owner_ &&
        format_ == other.format_ && format_version_ == other.format_version_ &&
        catalog_epoch_ == other.catalog_epoch_;
  }
};
} }
#endif
