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
 
#ifndef OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_QUERY_REWRITE_H_
#define OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_QUERY_REWRITE_H_
#include "sql/engine/ob_exec_context.h"
#include "share/object/ob_obj_cast.h"
#include "sql/udr/ob_udr_struct.h"

namespace oceanbase
{
namespace pl
{

#define DEF_UDR_PROCESSOR(name)                                                                   \
  class name##Processor : public ObUDRProcessor                                                   \
  {                                                                                               \
  public:                                                                                         \
    name##Processor(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result)      \
    : ObUDRProcessor(ctx, params, result)                                                         \
    {}                                                                                            \
    virtual ~name##Processor() {}                                                                 \
    virtual int parse_request_param();                                                            \
    virtual int generate_exec_arg();                                                              \
    virtual int execute();                                                                        \
  private:                                                                                        \
    DISALLOW_COPY_AND_ASSIGN(name##Processor);                                                    \
  };

class ObUDRProcessor
{
public:
  ObUDRProcessor(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result)
    : is_inited_(false),
      ctx_(ctx),
      params_(params),
      result_(result),
      arg_()
  {}
  virtual ~ObUDRProcessor() {}
  int process();
  virtual int init();
  virtual int parse_request_param() = 0;
  virtual int generate_exec_arg() = 0;
  virtual int execute() = 0;

protected:
  int pre_execution_check();
  int sync_rule_from_inner_table();

protected:
  bool is_inited_;
  sql::ObExecContext &ctx_;
  sql::ParamStore &params_;
  common::ObObj &result_;
  sql::ObUDRInfo arg_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObUDRProcessor);
};

DEF_UDR_PROCESSOR(ObCreateRule);
DEF_UDR_PROCESSOR(ObRemoveRule);
DEF_UDR_PROCESSOR(ObEnableRule);
DEF_UDR_PROCESSOR(ObDisableRule);

class ObDBMSUserDefineRule
{
public:
  ObDBMSUserDefineRule() {}
  virtual ~ObDBMSUserDefineRule() {}
#define DEF_UDR_FUNC(name)  \
  static int name(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);
  DEF_UDR_FUNC(create_rule);
  DEF_UDR_FUNC(remove_rule);
  DEF_UDR_FUNC(enable_rule);
  DEF_UDR_FUNC(disable_rule);
};

} // end of pl
} // end of oceanbase
#endif /* OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_QUERY_REWRITE_H_ */
