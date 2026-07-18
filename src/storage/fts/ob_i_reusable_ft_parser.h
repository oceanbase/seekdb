/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_STORAGE_FTS_OB_I_REUSABLE_FT_PARSER_H_
#define OCEANBASE_STORAGE_FTS_OB_I_REUSABLE_FT_PARSER_H_

#include "plugin/interface/ob_plugin_ftparser_intf.h"

namespace oceanbase
{
namespace storage
{

// Internal extension for built-in parsers.  Keeping reuse out of the exported
// plugin iterator preserves the external plugin ABI.
class ObIReusableFTParser : public plugin::ObITokenIterator
{
public:
  ObIReusableFTParser() = default;
  virtual ~ObIReusableFTParser() = default;
  virtual int reuse_parser(const char *fulltext, const int64_t fulltext_len) = 0;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_FTS_OB_I_REUSABLE_FT_PARSER_H_
