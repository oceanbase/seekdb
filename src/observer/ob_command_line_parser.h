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

#ifndef OCEANBASE_OBSERVER_OB_COMMAND_LINE_PARSER_H_
#define OCEANBASE_OBSERVER_OB_COMMAND_LINE_PARSER_H_

#include "observer/ob_server_options.h"

namespace oceanbase {
namespace observer {

/**
 * 使用C标准库getopt_long实现的命令行参数解析器
 * 符合OceanBase编码规范
 */
class ObCommandLineParser final {
public:
  ObCommandLineParser() = default;
  ~ObCommandLineParser() = default;

  /**
   * 解析命令行参数并直接设置到ObServerOptions中
   * @param argc 参数个数
   * @param argv 参数数组
   * @param opts 服务器选项，解析结果会直接设置到这里
   * @return 解析结果，OB_SUCCESS表示成功
   */
  int parse_args(int argc, char* argv[], ObServerOptions& opts);

  /**
   * 打印帮助信息
   */
  void print_help() const;

  /**
   * 打印版本信息
   */
  static void print_version();


private:
  // 处理选项值
  int handle_option(int option, const char* value, ObServerOptions& opts);
  // 设置字符串到ObString
  int set_ob_string(ObIAllocator& allocator, const char* value, ObString& target);

private:
  // 解析结果状态
  bool help_requested_    = false;
  bool version_requested_ = false;
};

} // namespace observer
} // namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_COMMAND_LINE_PARSER_H_
