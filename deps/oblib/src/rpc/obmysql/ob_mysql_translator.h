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

#ifndef _OB_MYSQL_TRANSLATOR_H_
#define _OB_MYSQL_TRANSLATOR_H_

#include "rpc/frame/ob_req_translator.h"

// used when initializing processor table
namespace oceanbase
{
namespace obmysql
{

using rpc::frame::ObReqProcessor;

class ObMySQLTranslator
    : public rpc::frame::ObReqTranslator
{
public:
  ObMySQLTranslator() {}
  virtual ~ObMySQLTranslator() {}
}; // end of class ObMySQLTranslator

} // end of namespace obmysql
} // end of namespace oceanbase

#endif /* _OB_MYSQL_TRANSLATOR_H_ */
