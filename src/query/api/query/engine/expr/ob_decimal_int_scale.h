/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_ENGINE_EXPR_OB_DECIMAL_INT_SCALE_H_
#define OCEANBASE_QUERY_API_ENGINE_EXPR_OB_DECIMAL_INT_SCALE_H_

#include "common/object/ob_obj_type.h"
#include "common/wide_integer/ob_wide_integer.h"
#include "share/object/ob_obj_cast.h"

namespace oceanbase
{
namespace query
{

class ObDecimalIntScale
{
public:
  static bool is_needed(
      common::ObScale input_scale,
      int32_t input_bytes,
      common::ObScale output_scale,
      int32_t output_bytes);

  static int scale(
      const common::ObDecimalInt *value,
      int32_t value_bytes,
      common::ObScale input_scale,
      common::ObScale output_scale,
      common::ObPrecision output_precision,
      common::ObCastMode cast_mode,
      common::ObDecimalIntBuilder &result);
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_ENGINE_EXPR_OB_DECIMAL_INT_SCALE_H_
