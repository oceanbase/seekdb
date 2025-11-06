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

#include "oceanbase/ob_plugin_charset_info.h"
#include "oceanbase/ob_plugin_errno.h"
#include "lib/charset/ob_ctype.h"

#ifdef __cplusplus
extern "C" {
#endif

static ObCharsetInfo *get_charset_info(ObPluginDatum cs)
{
  return (ObCharsetInfo*)cs;
}

OBP_PUBLIC_API int obp_charset_ctype(ObPluginCharsetInfoPtr cs, int *ctype, const unsigned char *s, const unsigned char *e)
{
  return get_charset_info(cs)->cset->ctype(get_charset_info(cs), ctype, s, e);
}

OBP_PUBLIC_API const char *obp_charset_csname(ObPluginCharsetInfoPtr cs)
{
  return get_charset_info(cs)->csname;
}

OBP_PUBLIC_API int obp_charset_is_utf8mb4(ObPluginCharsetInfoPtr cs)
{
  bool result = 0 == strcasecmp(get_charset_info(cs)->csname, OB_UTF8MB4);
  return result ? OBP_SUCCESS : OBP_PLUGIN_ERROR;
}

OBP_PUBLIC_API size_t obp_charset_numchars(ObPluginCharsetInfoPtr cs, const char *pos, const char *end)
{
  return ob_numchars_mb(get_charset_info(cs), pos, end);
}

#ifdef __cplusplus
} // extern "C"
#endif
