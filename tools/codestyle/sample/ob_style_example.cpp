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

#include "common/ob_style_example.h"

// c lib headers
#include <cstdlib>
#include <cstring>

// c++ lib headers
#include <string>

// other OceanBase headers
#include "common/errno.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace example
{

ObExampleClass::ObExampleClass()
    : inited_(false),
      int64_member_(0),
      pint64_member_(NULL)
{
}
// or this way
// ObExampleClass::ObExampleClass() : inited_(false), int64_member_(0), pint64_member_(NULL)
// {
// }

ObExampleClass::~ObExampleClass()
{
}

int ObExampleClass::outside_public_func()
{
  int ret = OB_SUCCESS;

  if (OB_SUCCESS != (ret = do_something())) {
    // print log
  } else if (OB_SUCCESS != (ret = do_another_thing())) {
    // print log
  } else if (OB_SUCCESS != (ret = do_another_thing2())) {
    // print log
  } else {
    // go through if all things work right
  }

  // There's mayhaps another group things should do.
  if (OB_SUCC(ret)) {
    // Do thus if and only if the uppers don't go awry.
    // Here's the switch statement outline.
    switch (var) {
      case SUCCESS: {
        // ...
        break;
      }
      case FAIL_BY_xxx: {
        // ...
        break;
      }
      default: {
        OB_ASSERT(false);
        break;
      }
    }
  }

  // The third group of works
  if (OB_SUCC(ret)) {
    // If there's no sufficient space to arguments in single line when invoking a function,
    a_function_have_some_long_arguments(the_first_parameter,
                                        the_second_parameter,
                                        the_third_parameter,
                                        the_fourth_parameter);
    // or maybe the first should put another line if the function name is toooo long,
    a_function_have_some_long_arguments(
        the_first_parameter,
        the_second_parameter,
        the_third_parameter,
        the_fourth_parameter);
    // or combination of multiple conditions for condition statement i.e. if, for, while, switch and so forth,
    if ((a_long_long_long_condition0 || a_long_long_long_condition1)
        && a_long_long_long_condition2
        && a_long_long_long_condition3) {
      // do something
    } else {
      // leave empty
    }
    // or maybe
  }

  return ret;
}

int ObExampleClass::outside_public_func()
{
#define MY_MAX(a, b) (((a) > (b)) ? (a) : (b))
  // do with MY_MAX macro
#undef MY_MAX
  int ret = OB_SUCCESS;
  return ret;
}

} // end of namespace example
} // end of namespace oceanbase
