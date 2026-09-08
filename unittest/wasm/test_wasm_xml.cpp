// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "common/xml/ob_libxml2_sax_handler.h"
#include "common/xml/ob_xml_tree.h"
#include "lib/allocator/page_arena.h"
#include "lib/resource/achunk_mgr.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <pthread.h>

using namespace oceanbase;
using namespace oceanbase::common;

static void *check_parser(void *)
{
  for (unsigned round = 0; round < 8; ++round) {
    ObArenaAllocator allocator;
    ObMulModeMemCtx ctx;
    ctx.allocator_ = &allocator;
    {
      ObXmlParser parser(&ctx);
      assert(parser.parse_document(ObString::make_string(
        "<root id=\"42\"><value>中文 &amp; 😀</value></root>")) == OB_SUCCESS);
      ObXmlDocument *doc = parser.document();
      assert(doc != nullptr && doc->count() == 1);
      ObXmlNode *root = doc->at(0);
      assert(root != nullptr && root->count() == 1 && root->attribute_count() == 1);
      ObXmlNode *value = root->at(0);
      assert(value != nullptr && value->count() == 1);
      ObString text;
      assert(value->at(0)->get_value(text) == OB_SUCCESS);
      assert(text == ObString::make_string("中文 & 😀"));
    }
    for (const char *bad : {"<root><x></root>", "<root>", "<root a=\"1\" a=\"2\"/>", ""}) {
      ObXmlParser parser(&ctx);
      parser.set_only_syntax_check();
      assert(parser.parse_document(ObString::make_string(bad)) != OB_SUCCESS);
    }
    // An earlier parser error must not poison the next document or thread.
    ObXmlParser next(&ctx);
    next.set_only_syntax_check();
    assert(next.parse_document(ObString::make_string("<next/>")) == OB_SUCCESS);
  }
  return nullptr;
}

int main()
{
  static_assert(LIBXML_VERSION == 21004);
  assert(strcmp(xmlParserVersion, "21004") == 0);
  assert(xmlHasFeature(XML_WITH_THREAD) != 0);
  assert(xmlHasFeature(XML_WITH_ICONV) != 0);
  lib::AChunkMgr::instance().set_limit(64 * 1024 * 1024);
  lib::AChunkMgr::instance().set_hard_limit(64 * 1024 * 1024);
  ObLibXml2SaxHandler::init();
  pthread_t workers[2];
  for (auto &worker : workers) assert(pthread_create(&worker, nullptr, check_parser, nullptr) == 0);
  check_parser(nullptr);
  for (auto &worker : workers) assert(pthread_join(worker, nullptr) == 0);
  ObLibXml2SaxHandler::destroy();
  puts("PASS: seekdb XML tree, Unicode/entities, syntax errors and concurrent parser contexts");
}
