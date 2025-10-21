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

#ifndef OCEANBASE_LIB_OBMYSQL_OB_DTOA_
#define OCEANBASE_LIB_OBMYSQL_OB_DTOA_

#include "lib/charset/ob_mysql_global.h"

#ifdef	__cplusplus
extern "C" {
#endif

//================= from m_string.h ================

/* Conversion routines */
typedef enum
{
  OB_GCVT_ARG_FLOAT,
  OB_GCVT_ARG_DOUBLE
} ob_gcvt_arg_type;

//==================================================

double ob_strtod(const char *str, char **end, int *error);
size_t ob_fcvt(double x, int precision, int width, char *to, bool *error);
size_t ob_gcvt(double x, ob_gcvt_arg_type type, int width, char *to, bool *error);
// If is_binary_double is false, the behavior at this time is consistent
// with mysql, and mysql mode needs to be used
size_t ob_gcvt_opt(double x, ob_gcvt_arg_type type, int width, char *to, bool *error,
                   bool is_binary_double);
size_t ob_gcvt_strict(double x, ob_gcvt_arg_type type, int width, char *to, bool *error,
                      bool is_binary_double, bool use_force_e_format);

#ifdef	__cplusplus
}
#endif

#endif /* OCEANBASE_LIB_OBMYSQL_OB_DTOA_ */

