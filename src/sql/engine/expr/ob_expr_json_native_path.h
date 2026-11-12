/*
 * Copyright (c) 2026 OceanBase.
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

#pragma once

#include "common/json_type/ob_json_base.h"
#include "lib/string/ob_string.h"
#include "rust/json.h"
#include <optional>
#include <string>
#include <vector>

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class MultimodeAlloctor;
class ObJsonParamCacheCtx;

struct JsonPathMatches
{
  uint64_t count_ = 0;
  uint64_t single_length_ = 0;
};

class JsonPathCache
{
public:
  int find_and_add(common::ObString &path_text, int64_t arg_idx, bool is_const,
                   const common::json::Path *&path);
private:
  struct Entry
  {
    Entry(std::string &&text, rust::Box<common::json::PathParseResult> &&parsed)
      : text_(std::move(text)), parsed_(std::move(parsed)) {}
    std::string text_;
    rust::Box<common::json::PathParseResult> parsed_;
  };
  std::vector<std::optional<Entry>> entries_;
};

int query_native_json_path(const common::ObString &data,
                           common::ObJsonInType input_type,
                           const common::json::Path &path,
                           bool auto_wrap,
                           bool first_only,
                           uint32_t max_depth_config,
                           JsonPathMatches &matches);
int get_native_json_query(const ObExpr &expr,
                          ObEvalCtx &ctx,
                          MultimodeAlloctor &allocator,
                          uint16_t index,
                          std::optional<rust::Box<common::json::Query>> &query,
                          bool &is_null,
                          bool *is_cover_by_error = nullptr);
int get_native_json_path(const ObExpr &expr,
                         ObEvalCtx &ctx,
                         MultimodeAlloctor &allocator,
                         ObJsonParamCacheCtx *param_ctx,
                         uint16_t index,
                         const common::json::Path *&path,
                         bool &is_null,
                         bool &is_cover_by_error);
JsonPathCache *get_native_path_cache_ctx(const uint64_t &id, ObExecContext *exec_ctx);
int find_and_add_native_path_cache(JsonPathCache *path_cache,
                                   const common::json::Path *&res_path,
                                   common::ObString &path_text,
                                   int64_t arg_idx,
                                   bool enable_wildcard,
                                   bool is_const = false);

} // namespace sql
} // namespace oceanbase
