/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#pragma once

#include "common/object/ob_object.h"

namespace oceanbase
{
namespace common { struct ObLobReadOptions; }
namespace pl { class ObPLINS; }
namespace sql
{
class ObExecContext;
class ObResultSet;
class ObBasicSessionInfo;

// Only the final result writer calls this adapter. In particular, a PL datum in
// an operator or an internal SQL result must retain its declared composite type.
class PlCompositeClientOutput final
{
public:
  static bool needs_conversion(const common::ObObjMeta &meta);
  static int convert(common::ObObj &value, ObResultSet &result);
  static int convert(common::ObObj &value, common::ObIAllocator &allocator,
                     ObExecContext &exec_ctx);
  static int convert(common::ObObj &value, common::ObIAllocator &allocator,
                     const pl::ObPLINS *type_namespace, ObBasicSessionInfo &session,
                     const common::ObLobReadOptions *lob_options);
};
} // namespace sql
} // namespace oceanbase
