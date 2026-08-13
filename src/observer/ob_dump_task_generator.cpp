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

#define USING_LOG_PREFIX SERVER

#include "observer/ob_dump_task_generator.h"
#ifdef _WIN32
#include <fcntl.h>
#endif
#include "lib/alloc/memory_dump.h"
#include "sql/parser/ob_parser.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
namespace observer
{
int ObDumpTaskGenerator::read_cmd(char *buf, int64_t len, int64_t &real_size)
{
  int ret = OB_SUCCESS;
  FILE *fp = fopen("etc/dump.config", "r");
  if (nullptr == fp) {
    ret = OB_ERR_SYS;
    LOG_WARN("open config file failed", K(ret), K(strerror(errno)));
  } else {
    fseek(fp, 0, SEEK_END);
    int64_t size = ftell(fp);
    rewind(fp);
    if (size > len) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("cmd too long", K(ret), K(size), K(len));
    } else {
      fread(buf, 1, size, fp);
      real_size = size;
    }
    fclose(fp);
  }
  return ret;
}

int ObDumpTaskGenerator::generate_task_from_file()
{
  int ret = OB_SUCCESS;
  auto &mem_dump = ObMemoryDump::get_instance();
  ObArenaAllocator allocator;
  ObMemAttr attr("dumpParser", ObCtxIds::DEFAULT_CTX_ID);
  allocator.set_attr(attr);
  ObParser parser(allocator, SMO_DEFAULT);
  ParseResult parse_result;
  ParseNode *stmt_node = nullptr;
  ParseNode *node = nullptr;
  const int64_t len = 128;
  char buf[len];
  int64_t real_size = 0;
  ObString cmd;
  if (!mem_dump.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(read_cmd(buf, len, real_size))) {
  } else if(FALSE_IT(cmd.assign_ptr(buf, static_cast<int32_t>(real_size)))) {
  } else if (OB_FAIL(parser.parse(cmd, parse_result))) {
  } else if(nullptr == parse_result.result_tree_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("nullptr", K(cmd), K(ret));
  } else if (OB_ISNULL(stmt_node = parse_result.result_tree_->children_[0])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("nullptr", K(cmd), K(ret));
  } else if (stmt_node->type_ != T_DUMP_MEMORY) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not support", K(cmd), K(stmt_node->type_));
  } else if (OB_ISNULL(node = stmt_node->children_[0])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("nullptr", K(cmd), K(ret));
  } else {
    LOG_INFO("read command", K(cmd));
    {
      ObMemoryDumpTask task;
      task.type_ = node->value_ <= 1 ? DUMP_CONTEXT : DUMP_CHUNK;
      task.dump_all_ = 0 == node->value_ || 2 == node->value_;
      char atoi_buf[32];
      if (CONTEXT_ALL == node->value_) {
        // do-nothing
      } else if (CONTEXT == node->value_) {
        snprintf(atoi_buf, sizeof(atoi_buf), "%.*s",
                 (int32_t)node->children_[0]->str_len_, node->children_[0]->str_value_);
        task.p_context_ = (void*)std::stoll(atoi_buf, nullptr, 0);
        task.slot_idx_ = node->children_[1]->value_;
      } else if (CHUNK_ALL == node->value_) {
        // do-nothing
      } else if (CHUNK == node->value_) {
        snprintf(atoi_buf, sizeof(atoi_buf), "%.*s",
                 (int32_t)node->children_[0]->str_len_, node->children_[0]->str_value_);
        task.p_chunk_ = (void*)std::stoll(atoi_buf, nullptr, 0);
      }
      LOG_INFO("task info", K(task));
      if (OB_FAIL(mem_dump.request_dump(task))) {
      }
    }
  }
  return ret;
}

}
}
