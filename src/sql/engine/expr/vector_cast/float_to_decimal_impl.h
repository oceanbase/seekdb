/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OCEANBASE_SQL_ENGINE_EXPR_VECTOR_CAST_FLOAT_TO_DECIMAL_
#define OCEANBASE_SQL_ENGINE_EXPR_VECTOR_CAST_FLOAT_TO_DECIMAL_
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class ObFloatToDecimal final
{
public:
constexpr static uint16_t MAX_DIGITS_FLOAT = 9;
constexpr static uint16_t MAX_DIGITS_DOUBLE = 17;
constexpr static uint16_t FLOAT80_PRECISION = 18;
constexpr static uint16_t INT64_PRECISION = 18;
constexpr static uint16_t DOUBlE_MANTISSA_BITS = 52;
constexpr static uint16_t DOUBLE_EXPONENT_BITS = 11;
constexpr static uint16_t DOUBLE_EXPONENT_BIAS = 1023;
constexpr static uint16_t LONG_DOUBLE_MANTISSA_BITS = 63;
constexpr static uint64_t DOUBLE_EXPONENT_MASK = 0x7FF;
constexpr static uint64_t DOUBLE_MANTISSA_MASK = 0xFFFFFFFFFFFFFull;
constexpr static double LOG10_2 = 0.30103;
constexpr static double EPSILON = 1e-6;

typedef union
{
  bool is_negative() const { return (integer_value >> (DOUBlE_MANTISSA_BITS + DOUBLE_EXPONENT_BITS)) != 0; }
  uint32_t get_exponent() const { return (integer_value >> DOUBlE_MANTISSA_BITS) & DOUBLE_EXPONENT_MASK; }
  uint64_t get_mantissa() const { return integer_value & DOUBLE_MANTISSA_MASK; }
  double double_value;
  uint64_t integer_value;
} DoubleUnion;

typedef union{
  long double double_val;
  uint32_t uint32_val[4];
} LongDoubleUnion;

static int float2decimal(double x, const bool is_oracle_mode, ob_gcvt_arg_type arg_type,
                         const ObPrecision target_precision, const ObScale target_scale,
                         const ObCastMode cast_mode, ObDecimalIntBuilder &dec_builder,
                         const ObUserLoggingCtx *user_logging_ctx, ObDecimalInt *&decint);
};

}
}
#endif /* OCEANBASE_SQL_ENGINE_EXPR_VECTOR_CAST_FLOAT_TO_DECIMAL_ */
