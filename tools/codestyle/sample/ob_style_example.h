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

namespace oceanbase
{
namespace example
{

// sample of interface class
class ObIAction
{
  // ...
}; // end of class ObIAction


// This class is a example class to demonstrate the new OceanBase
// style for OceanBase 1.0+.
//
// TODO(fufeng): should do something in 30 October 2014
// FIXME(fufeng): should fixed something in someday
class ObExampleClass : public ObIAction
{
public:
  typedef OtherClass NewType;
  static const int64_t PUBLIC_NUMBER = 32;

public:
  ObExampleClass();
  virtual ~ObExampleClass();

  inline bool is_inited() const { return inited_; }

  inline int inline_public_func();

  // a group of public function
  int outside_public_func();
  int outside_public_func2();

  // another group of public function
  int outside_public_func3();
  int outside_public_func4();

  // Tell what the function will do.
  //
  // Warning:
  // This function should called once only.
  //
  // @param [in] p1 blalala
  // @param [out] p2 blalala
  //
  // @retval OB_SUCCESS execute success
  // @retval OB_SOME_ERROR special errno need to handle
  //
  // Example:
  // Type p1;
  // p1.init(...);
  // OType p2;
  // if (o.is_inited()) {
  //   retcode = o.another_func(p1, p2);
  //   if (OB_SUCCESS == retcode) {
  //     ...
  //   } else if (OB_SOME_ERROR == retcode) {
  //     ...
  //   } else {
  //     ...
  //   }
  // }
  int another_func(const Type &p1, OType &p2);

  int with_default(const Type &p1, OType &p2, bool sync = ture);

private:
  enum
  {
    SUCCESS,         // execute rightly
    FAIL_BY_xxx      // ...
  };

  class ObExampleClass;  // maybe need by inner class
  static const int64_t MAX_NUMBER = 16;

  class InnerClass
  {
  public:
    InnerClass();
    virtual ~InnerClass();
  }; // end of class InnerClass

private:  // here's private functions
  inline int inline_private_func();

private:  // here's private class memebers
  bool inited_;
  // The member name is just a example which here is meaningless. You
  // should using a self described name in real world.
  int64_t int64_member_;
  int64_t *pint64_member_;

  DISALLOW_COPY_AND_ASSIGN(ObExampleClass);
}; // end of class ObExampleClass

int ExampleClass::inline_public_func()
{
  int ret = common::OB_SUCCESS;

  if (OB_SUCC(ret)) {
    if (common::OB_SUCCESS != (ret = inline_private_func())) {
      // print to log something?
    }
  }

  return ret;
}

} // end of namespace example
} // end of namespace oceanbase
