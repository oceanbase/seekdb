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

#ifndef OCEANBASE_SQL_OB_JSON_PATH
#define OCEANBASE_SQL_OB_JSON_PATH

#include "lib/string/ob_string.h"
#include "lib/container/ob_array.h"
#include "lib/container/ob_vector.h"
#include "common/json_type/ob_json_common.h"

namespace oceanbase {
namespace common {

enum ObJsonPathNodeType {
  JPN_ERROR = 0,
  JPN_ROOT = 1,

  JPN_BEGIN_BASIC_FLAG,
  JPN_MEMBER,              // member, example：.key                 -> mysql & SQL/JSON
  JPN_MEMBER_WILDCARD,     // member wildcard, example：.*          -> mysql & SQL/JSON
  JPN_ARRAY_CELL,          // single array ele, example：[index]    -> mysql
  JPN_ARRAY_RANGE,         // array range, example：[8 to 16]       -> mysql
  JPN_ARRAY_CELL_WILDCARD, // array ele wildcatd, example：[*]      -> mysql & SQL/JSON
  JPN_WILDCARD_ELLIPSIS,   // ellipsis for mysql, example：**       -> mysql
  JPN_MULTIPLE_ARRAY,      // SQL/JSON array list, example:[3, 4 to 6]
  JPN_DOT_ELLIPSIS,        // SQL/JSON ellipsis, example：..
  JPN_END_BASIC_FLAG,

  // SQL/JSON item methods
  JPN_BEGIN_FUNC_FLAG,
  JPN_ABS, // abs()
  JPN_BOOLEAN, // boolean()
  JPN_BOOL_ONLY, // booleanOnly()
  JPN_CEILING,
  JPN_DATE,
  JPN_DOUBLE,
  JPN_FLOOR,
  JPN_LENGTH,
  JPN_LOWER,
  JPN_NUMBER,
  JPN_NUM_ONLY,
  JPN_SIZE,
  JPN_STRING,
  JPN_STR_ONLY,
  JPN_TIMESTAMP,
  JPN_TYPE,
  JPN_UPPER,
  JPN_END_FUNC_FLAG,

  // filter 25
  JPN_BEGIN_FILTER_FLAG,
  JPN_EQUAL, // predicate ==
  JPN_UNEQUAL, // !=
  JPN_LARGGER, // > 
  JPN_LARGGER_EQUAL, // >= 
  JPN_SMALLER, // < 
  JPN_SMALLER_EQUAL, // <= 
  JPN_SUBSTRING, // has substring
  JPN_STARTS_WITH, // starts with
  JPN_LIKE, // like
  JPN_LIKE_REGEX, // like_regex
  JPN_EQ_REGEX, // eq_regex
  JPN_NOT_EXISTS,
  JPN_EXISTS, // exist
  JPN_AND_COND, // cond1 && cond2
  JPN_OR_COND, // cond1 || cond2
  JPN_NOT_COND, // !(cond)
  JPN_END_FILTER_FLAG,

  JPN_BEGIN_FILTER_CONTENT_FLAG,
  JPN_SQL_VAR,	// sql identifier
  JPN_BOOL_TRUE,
  JPN_BOOL_FALSE,
  JPN_NULL,
  JPN_SCALAR,	// quoted 
  JPN_SUB_PATH,
  JPN_END_FILTER_CONTENT_FLAG,

  JPN_MAX
};

enum {
  OPT_JSON_VALUE,
  OPT_JSON_QUERY,
};
// SQL expression metadata is a snapshot of the native parser result.
class ObJsonPath
{
public:
  explicit ObJsonPath(common::ObIAllocator *allocator);
  explicit ObJsonPath(const ObString &path, common::ObIAllocator *allocator);
  int parse_path();
  ObJsonPathNodeType get_last_node_type() const { return last_node_; }
  bool is_last_func() const { return last_node_ > JPN_BEGIN_FUNC_FLAG && last_node_ < JPN_END_FUNC_FLAG; }
  bool path_not_str() const;
  bool can_match_many() const { return can_match_many_; }
  ObString &get_path_string() { return expression_; }
private:
  ObString expression_;
  ObJsonBuffer heap_expr_;
  ObJsonPathNodeType last_node_;
  bool can_match_many_;
};

// ObJsonPathCache
// used in json expression
class ObJsonPathCache {
public:
  enum ObPathParseStat{
    UNINITIALIZED,
    OK_NOT_NULL,
    OK_NULL,
    ERROR,
  };

  struct ObPathCacheStat{
    ObPathParseStat state_;
    int index_;
    ObPathCacheStat() : state_(UNINITIALIZED), index_(-1) {}
    ObPathCacheStat(ObPathParseStat state, int idx) : state_(state), index_(idx) {};
    ObPathCacheStat(const ObPathCacheStat& stat) : state_(stat.state_), index_(stat.index_) {}
  };
  typedef PageArena<ObJsonPath*, ModulePageAllocator> JsonPathArena;
  typedef PageArena<ObPathCacheStat, ModulePageAllocator> PathCacheStatArena;
  typedef ObVector<ObJsonPath*, JsonPathArena> ObJsonPathPointers;
  typedef ObVector<ObPathCacheStat, PathCacheStatArena> ObPathCacheStatArr;
  static const int64_t DEFAULT_PAGE_SIZE = (1LL << 10); // 1kb
 
public:
  ObJsonPathCache(common::ObIAllocator *allocator) :
        allocator_(allocator),
        page_allocator_(*allocator, common::ObModIds::OB_MODULE_PAGE_ALLOCATOR),
        path_cache_arena_(DEFAULT_PAGE_SIZE, page_allocator_),
        path_arena_(DEFAULT_PAGE_SIZE, page_allocator_),
        path_arr_ptr_(&path_arena_, common::ObModIds::OB_MODULE_PAGE_ALLOCATOR),
        stat_arr_(&path_cache_arena_, common::ObModIds::OB_MODULE_PAGE_ALLOCATOR) {}
  ~ObJsonPathCache() {};

  ObJsonPath* path_at(size_t idx);

  ObPathParseStat path_stat_at(size_t idx);
  
  size_t size();
  void reset();

  int find_and_add_cache(ObJsonPath*& parse_path, ObString& path_str, int arg_idx, bool is_const = false);
  common::ObIAllocator* get_allocator();
private:
  int set_path(ObJsonPath* path, ObPathParseStat stat, int arg_idx, int index);
  bool is_match(ObString& path_str, size_t idx); 

  int fill_empty(size_t reserve_size);

private:
  common::ObIAllocator *allocator_;
  ModulePageAllocator page_allocator_;
  PathCacheStatArena path_cache_arena_;
  JsonPathArena      path_arena_;
  ObJsonPathPointers path_arr_ptr_;
  ObPathCacheStatArr stat_arr_;
};

using ObPathCacheStat = ObJsonPathCache::ObPathCacheStat;
using ObPathParseStat = ObJsonPathCache::ObPathParseStat;

class ObJsonPathUtil
{
public:
  static bool is_whitespace(char ch);
  static void skip_whitespace(const ObString& path, uint64_t& idx);
  static int double_quote(ObString &name, ObJsonBuffer* tmp_name);
  static int string_cmp_skip_charactor(ObString& lhs, ObString& rhs, char ch);
  static bool is_letter(unsigned codepoint, const char* ori, uint64_t start, uint64_t end);
  static bool is_connector_punctuation(unsigned codepoint);
  static bool unicode_combining_mark(unsigned codepoint);
  static bool is_utf8_unicode_charator(const char* ori, uint64_t& start, int64_t len);
};

} // namespace common
} // namespace oceanbase
#endif  // OCEANBASE_SQL_OB_JSON_PATH
