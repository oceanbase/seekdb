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
#ifndef OCEANBASE_LIBXML2_SAX_HANDLER_H_
#define OCEANBASE_LIBXML2_SAX_HANDLER_H_
#include "lib/xml/ob_xml_parser.h"
#include "libxml2/libxml/parser.h"
namespace oceanbase {
namespace common {
struct ObLibXml2SaxHandler
{
public:
  // libxml2 sax callback start
  static void start_document(void* ctx);
  static void end_document(void* ctx);
  // used for sax1
  static void start_element(void* ctx, const xmlChar* name, const xmlChar** p);
  static void end_element(void* ctx, const xmlChar* name);
  static void characters(void* ctx, const xmlChar* ch, int len);
  static void cdata_block(void* ctx, const xmlChar* value, int len);
  static void comment(void* ctx, const xmlChar* value);
  static void processing_instruction(void *ctx, const xmlChar *target, const xmlChar *data);
  static void internal_subset(void *ctx,
    const xmlChar *name,
    const xmlChar *external_id,
    const xmlChar *system_id);
  static void entity_reference(void *ctx, const xmlChar *name);
  // for error msg
  static void structured_error(void *ctx, const xmlError *error);
  // libxml2 sax callback end
  // helper method
  static int get_parser(void* ctx, ObLibXml2SaxParser*& parser);
  static void init();
  static void destroy();
  static void reset_libxml_last_error();
};
} // namespace common
} // namespace oceanbase
#endif  //OCEANBASE_LIBXML2_SAX_HANDLER_H_
