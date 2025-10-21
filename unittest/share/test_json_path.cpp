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

#include <gtest/gtest.h>
#define private public
#include "deps/oblib/src/lib/json_type/ob_json_base.h"
#undef private

using namespace oceanbase::common;

class TestJsonPath : public ::testing::Test {
public:
  TestJsonPath()
  {}
  ~TestJsonPath()
  {}
  virtual void SetUp()
  {}
  virtual void TearDown()
  {}

  static void SetUpTestCase()
  {}

  static void TearDownTestCase()
  {}

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(TestJsonPath);
};

TEST_F(TestJsonPath, test_get_cnt)
{
  ObJsonPath path(NULL);
  ASSERT_EQ(0, path.path_node_cnt());
}


TEST_F(TestJsonPath, test_is_mysql_terminator_mysql)
{
  char ch[] = "[. *ibc%^&";
  for(int i = 0; i < sizeof(ch); ++i)
  {
    if (i <= 3) {
      ASSERT_EQ(true, ObJsonPathUtil::is_key_name_terminator(ch[i]));
    } else {
      ASSERT_EQ(false, ObJsonPathUtil::is_key_name_terminator(ch[i]));
    }
  }
}
// Test basicNode's constructor
TEST_F(TestJsonPath, test_create_basic_node)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  // **
  ObJsonPathBasicNode temp1(&allocator);
  ASSERT_EQ(0, temp1.init(JPN_WILDCARD_ELLIPSIS, true));
  ASSERT_EQ(JPN_WILDCARD_ELLIPSIS, temp1.get_node_type());
  ASSERT_EQ(true, temp1.node_content_.is_had_wildcard_);
  // .*
  ObJsonPathBasicNode temp2(&allocator);
  ASSERT_EQ(0, temp2.init(JPN_MEMBER_WILDCARD, true));
  ASSERT_EQ(JPN_MEMBER_WILDCARD, temp2.get_node_type());
  ASSERT_EQ(true, temp2.node_content_.is_had_wildcard_);
  // [*]
  ObJsonPathBasicNode temp3(&allocator);
  ASSERT_EQ(0, temp3.init(JPN_ARRAY_CELL_WILDCARD, true));
  ASSERT_EQ(JPN_ARRAY_CELL_WILDCARD, temp3.get_node_type());
  ASSERT_EQ(true, temp3.node_content_.is_had_wildcard_);
  // [1]
  ObJsonPathBasicNode temp4(&allocator,1,false);
  ASSERT_EQ(JPN_ARRAY_CELL, temp4.get_node_type());
  ASSERT_EQ(1, temp4.node_content_.array_cell_.index_);
  ASSERT_EQ(false, temp4.node_content_.array_cell_.is_index_from_end_);
  // [last-3 to 6]
  ObJsonPathBasicNode temp5(&allocator,3, true, 6, false);
  ASSERT_EQ(JPN_ARRAY_RANGE, temp5.get_node_type());
  ASSERT_EQ(3, temp5.node_content_.array_range_.first_index_);
  ASSERT_EQ(true, temp5.node_content_.array_range_.is_first_index_from_end_);
  ASSERT_EQ(6, temp5.node_content_.array_range_.last_index_);
  ASSERT_EQ(false, temp5.node_content_.array_range_.is_last_index_from_end_);
  // .keyname
  ObString kn("keyname");
  ObJsonPathBasicNode temp6(&allocator,kn);
  ASSERT_EQ(JPN_MEMBER, temp6.get_node_type());
  std::cout<<temp6.node_content_.member_.object_name_<<std::endl;

  ObJsonPathNode *fa1 = &temp1;
  ASSERT_EQ(JPN_WILDCARD_ELLIPSIS, (static_cast<ObJsonPathBasicNode *> (fa1))->get_node_type());
  ASSERT_EQ(true, fa1->node_content_.is_had_wildcard_);
}
// test append function
TEST_F(TestJsonPath, test_append)
{
  // append **
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path(&allocator);
  ObJsonPathBasicNode temp1(&allocator);
  ASSERT_EQ(0, temp1.init(JPN_WILDCARD_ELLIPSIS, true));
  ObJsonPathNode *fa1 = &temp1;
  ret = test_path.append(fa1);
  ASSERT_EQ(OB_SUCCESS,ret);
  ASSERT_EQ(JPN_WILDCARD_ELLIPSIS, test_path.path_nodes_[0]->get_node_type());
  ASSERT_EQ(true, test_path.path_nodes_[0]->node_content_.is_had_wildcard_);

  // append .*
  ObJsonPathBasicNode temp2(&allocator);
  ASSERT_EQ(0, temp2.init(JPN_MEMBER_WILDCARD, true));
  ObJsonPathNode *fa2 = &temp2;
  ret = test_path.append(fa2);
  ASSERT_EQ(OB_SUCCESS,ret);
  ASSERT_EQ(JPN_MEMBER_WILDCARD, test_path.path_nodes_[1]->get_node_type());
  ASSERT_EQ(true, test_path.path_nodes_[1]->node_content_.is_had_wildcard_);

  // append [*]
  ObJsonPathBasicNode temp3(&allocator);
  ASSERT_EQ(0, temp3.init(JPN_ARRAY_CELL_WILDCARD, true));
  ObJsonPathNode *fa3 = &temp3;
  ret = test_path.append(fa3);
  ASSERT_EQ(OB_SUCCESS,ret);
  ASSERT_EQ(JPN_ARRAY_CELL_WILDCARD, test_path.path_nodes_[2]->get_node_type());
  ASSERT_EQ(true, test_path.path_nodes_[2]->node_content_.is_had_wildcard_);

  // append array_cell
  ObJsonPathBasicNode temp4(&allocator,1,false);
  ObJsonPathNode *fa4 = &temp4;
  ret = test_path.append(fa4);
  ASSERT_EQ(OB_SUCCESS,ret);
  ASSERT_EQ(JPN_ARRAY_CELL, test_path.path_nodes_[3]->get_node_type());
  ASSERT_EQ(1, test_path.path_nodes_[3]->node_content_.array_cell_.index_);
  ASSERT_EQ(false, test_path.path_nodes_[3]->node_content_.array_cell_.is_index_from_end_);

  // [last-3 to 6]
  ObJsonPathBasicNode temp5(&allocator,3, true, 6, false);
  ObJsonPathNode *fa5 = &temp5;
  ret = test_path.append(fa5);
  ASSERT_EQ(OB_SUCCESS,ret);
  ASSERT_EQ(JPN_ARRAY_RANGE, test_path.path_nodes_[4]->get_node_type());
  ASSERT_EQ(3, test_path.path_nodes_[4]->node_content_.array_range_.first_index_);
  ASSERT_EQ(true, test_path.path_nodes_[4]->node_content_.array_range_.is_first_index_from_end_);
  ASSERT_EQ(6, test_path.path_nodes_[4]->node_content_.array_range_.last_index_);
  ASSERT_EQ(false, test_path.path_nodes_[4]->node_content_.array_range_.is_last_index_from_end_);

  // .keyname
  ObString kn("keyname");
  ObJsonPathBasicNode temp6(&allocator,kn);
  ObJsonPathNode *fa6 = &temp6;
  ret = test_path.append(fa6);
  ASSERT_EQ(OB_SUCCESS,ret);
  ASSERT_EQ(JPN_MEMBER, test_path.path_nodes_[5]->get_node_type());
  std::cout<<"6: "<<test_path.path_nodes_[5]->node_content_.member_.object_name_<<std::endl;
}
// Test parse_array_index() function, used to get array_index (including handling of last and -)
TEST_F(TestJsonPath, test_parse_array_index)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path("$[last-10]", &allocator);
  test_path.index_ = 2;
  uint64_t array_index = 0;
  bool from_end = false;
  ret = test_path.parse_single_array_index(array_index, from_end);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(10, array_index);
  ASSERT_EQ(true, from_end);
}
// Test parse_array_node() function, used to get array_node (including [*], array_cell and array_range processing)
TEST_F(TestJsonPath, test_parse_array_node)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path1("$[  *   ]", &allocator);
  test_path1.index_ = 1;

  ret = test_path1.parse_single_array_node();
  ASSERT_EQ(1, test_path1.path_node_cnt());
  ASSERT_EQ(ret, OB_SUCCESS);
  // ASSERT_EQ(JPN_ARRAY_CELL_WILDCARD, test_path1.path_nodes_[0]->node_type);
  //ASSERT_EQ(10, test_path1.path_nodes_[0]->path_node_content_.array_cell_.index_);
  //ASSERT_EQ(false, test_path1.path_nodes_[0]->path_node_content_.array_cell_.index_);
}
// Test whether array_cell_node can be parsed correctly
TEST_F(TestJsonPath, test_array_cell_node)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path("$[last-10]", &allocator);
  test_path.is_mysql_ = false;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  ret = test_path.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // There is only one node
  ASSERT_EQ(1, test_path.path_node_cnt());
  ASSERT_EQ(JPN_MULTIPLE_ARRAY,test_path.path_nodes_[0]->get_node_type());
  ASSERT_EQ(10, test_path.path_nodes_[0]->node_content_.multi_array_[0]->first_index_);
  ASSERT_EQ(true,test_path.path_nodes_[0]->node_content_.multi_array_[0]->is_first_index_from_end_);
  ASSERT_EQ(10, test_path.path_nodes_[0]->node_content_.multi_array_[0]->last_index_);
  ASSERT_EQ(true,test_path.path_nodes_[0]->node_content_.multi_array_[0]->is_last_index_from_end_);
}
// Test whether array_range_node can be parsed correctly
TEST_F(TestJsonPath, test_array_range_node)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path("$[1 to 10]", &allocator);
  test_path.is_mysql_ = false;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  ret = test_path.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // There is only one node
  ASSERT_EQ(1, test_path.path_node_cnt());
  ASSERT_EQ(JPN_MULTIPLE_ARRAY,test_path.path_nodes_[0]->get_node_type());
  ASSERT_EQ(1, test_path.path_nodes_[0]->node_content_.multi_array_[0]->first_index_);
  ASSERT_EQ(false,test_path.path_nodes_[0]->node_content_.multi_array_[0]->is_first_index_from_end_);
  ASSERT_EQ(10, test_path.path_nodes_[0]->node_content_.multi_array_[0]->last_index_);
  ASSERT_EQ(false,test_path.path_nodes_[0]->node_content_.multi_array_[0]->is_last_index_from_end_);
}

TEST_F(TestJsonPath, test_multi_array_node)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path("$[last - 10, 1 to 10]", &allocator);
  test_path.is_mysql_ = false;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  ret = test_path.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // There is only one node
  ASSERT_EQ(1, test_path.path_node_cnt());
  ASSERT_EQ(JPN_MULTIPLE_ARRAY,test_path.path_nodes_[0]->get_node_type());
  ASSERT_EQ(2, test_path.path_nodes_[0]->node_content_.multi_array_.size());

  ASSERT_EQ(10, test_path.path_nodes_[0]->node_content_.multi_array_[0]->first_index_);
  ASSERT_EQ(true,test_path.path_nodes_[0]->node_content_.multi_array_[0]->is_first_index_from_end_);
  ASSERT_EQ(10, test_path.path_nodes_[0]->node_content_.multi_array_[0]->last_index_);
  ASSERT_EQ(true,test_path.path_nodes_[0]->node_content_.multi_array_[0]->is_last_index_from_end_);

  ASSERT_EQ(1, test_path.path_nodes_[0]->node_content_.multi_array_[1]->first_index_);
  ASSERT_EQ(false,test_path.path_nodes_[0]->node_content_.multi_array_[1]->is_first_index_from_end_);
  ASSERT_EQ(10, test_path.path_nodes_[0]->node_content_.multi_array_[1]->last_index_);
  ASSERT_EQ(false,test_path.path_nodes_[0]->node_content_.multi_array_[1]->is_last_index_from_end_);
}

TEST_F(TestJsonPath, test_filter_node)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  ObString str1("$?(@.LineItems[*].*..\"Part\".Description like \"Nixon\")");
  ObJsonPath test_path(str1, &allocator);
  test_path.is_mysql_ = false;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  ret = test_path.parse_path();
  if(OB_FAIL(ret)){
    std::cout<<"fail"<<std::endl;
    std::cout<<test_path.bad_index_<<std::endl;
    if(test_path.bad_index_>= test_path.expression_.length()){
      std::cout<<"end of path"<<std::endl;
    }else{
      std::cout<<test_path.expression_[test_path.bad_index_]<<std::endl;
    }
  }
  ASSERT_EQ(OB_SUCCESS, ret);
  std::cout<<test_path.path_nodes_[0]->get_node_type()<<std::endl;
  // There is only one node
  ASSERT_EQ(1, test_path.path_node_cnt());

  ObJsonBuffer str2(&allocator);
  ret = test_path.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  std::cout<<"to_string successed"<<std::endl;
  // Note that after member parsing, if keyname contains only letters and numbers, the double quotes will be removed
  // Direct comparison will result in an error
  std::cout<<str1.ptr()<<std::endl;
  std::cout<<str2.ptr()<<std::endl;
  if(0 == strcmp(str1.ptr(), str2.ptr()))  std::cout<<"same"<<std::endl;
}

TEST_F(TestJsonPath, test_good_filter_to_string)
{
  /*
  The following are path expressions from the oracle documentation example
  29. $.friends[3, 8 to 10, 12].cars[0]?(@.year > 2016)
  30. $.friends[3].cars[0]?(@.year.number() > 2016)
  31. $.friends[3].cars[0]?(@.year.numberOnly() > 2016)
  32. $.friends[3]?(@.addresses.city == "San Francisco")
  33. $.friends[*].addresses?(@.addresses.city starts with "San ")
  34. $.friends[3]?(@.addresses.city == "San Francisco" && @.addresses.state == "Nevada")
  35. $.friends[3].addresses?(@.city == "San Francisco" && @.state == "Nevada")
  36. $?(@.LineItems.Part.UPCCode == 85391628927  && @.LineItems.Quantity > 3)
  37. $?(@.User == "ABULL" && exists(@.LineItems[*]?(@.Part.UPCCode == 85391628927 && @.Quantity > 3)))
  */

  /*
  Comparison predicates
  // The string obtained from node_to_string will not have extra spaces
  38. $.friends[*].addresses?(@.addresses.city starts      with \"San \")
  39. $.friends[*].addresses?(@.addresses.city has substring \"San \")
  40. $.friends[*].addresses?(@.addresses.city has    substring \"San \")
  41. $.friends[*].addresses?(@.addresses.city like \"San \")
  42. $.friends[*].addresses?(@.addresses.city   like   \"San \")
  43. $.friends[*].addresses?(@.addresses.city like_regex \"San \")
  44. $.friends[*].addresses?(@.addresses.city   like_regex   \"San \")
  45. $.friends[*].addresses?(@.addresses.city eq_regex \"San \")
  46. $?(@.User == \"ABULL\" &&    exists(@.LineItems[*]?(@.Part.UPCCode == 85391628927 && @.Quantity > 3)))
  47. $?(@.User == \"ABULL\" &&    exists   (@.LineItems[*]?(@.Part.UPCCode == 85391628927 && @.Quantity > 3)))
  48. $?(@.User == \"ABULL\" && !exists(@.LineItems[*]?(@.Part.UPCCode == 85391628927 && @.Quantity > 3)))
  49. $?(@.User == \"ABULL\" &&   !exists  (@.LineItems[*]?(@.Part.UPCCode == 85391628927 && @.Quantity > 3)))
  50. $?(@.User == \"ABULL\" && !   exists(@.LineItems[*]?(@.Part.UPCCode == 85391628927 && @.Quantity > 3)))
  51. $.friends[3].cars[0]?(@.year.number() >   2016)
  52. $.friends[3].cars[0]?(@.year.number() > 2016  )
  53. $.friends[3].cars[0]?(@.year.number() > 2016.1)
  54. $.friends[3].cars[0]?(@.year.number() > -2016.1)
  55. $.friends[3].cars[0]?(@.year.number() > -4294967295)
  56. $.friends[3].cars[0]?(@.year.number() < 2016)
  57. $.friends[3].cars[0]?(@.year.number() >= 2016)
  58. $.friends[3].cars[0]?(@.year.number() <= 2016)
  59. $.friends[3].cars[0]?(@.year.number() == 2016)
  60. $.friends[3].cars[0]?(@.year.number() != 2016)
  61. $.friends[3].cars[0]?(@.year.number() != 2016 && @.year.number() > 2016)
  62. $.friends[3].cars[0]?(@.year.number() != 2016 || @.year.number() > 2016)
  63. $.friends[3].cars[0]?(@.year.number() != 2016 && !(@.year.number() > 2016))
  64. $.friends[3].cars[0]?(@.year.number() == 2016 || @.year.number() != 2016  &&  !(@.year.number() > 2016))
  65. $.friends[3].cars[0]?((@.year.number() == 2016 || @.year.number() != 2016) && !(@.year.number() > 2016))
  66. $.friends[3].cars[0]?(@.year.date() == "2022-8-22")
  67. $.friends[3].cars[0]?(@.year.date() ==   "2022-8-22"  )
  68. $.LineItems.Part?(@.UPCCode == $v1)
  69. $.LineItems.Part?(@.UPCCode ==   $v1  )
  70. $.LineItems.Part?(@.UPCCode == true)
  71. $.LineItems.Part?(@.UPCCode ==   true  )
  72. $.LineItems.Part?(@.UPCCode == false)
  73. $.LineItems.Part?(@.UPCCode ==  false  )
  74. $.LineItems.Part?(@.UPCCode == null)
  75. $.LineItems.Part?(2016 == 2016)
  76. $.LineItems.Part?(\"San \" == \"San \")
  77. $.LineItems.Part?(2016 == @.UPCCode)
  78. $.LineItems.Part?(  2016   == @.UPCCode)
  79. $.LineItems.Part?($v1 == @.UPCCode)
  80. $.LineItems.Part?(  $v1   == @.UPCCode)
  81. $.LineItems.Part?(  \"San \" == @.UPCCode)
  82. $?(@.LineItems[*].*..Part.Description like \"Nixon\" && $v1 == @.UPCCode)
  83. $.friends[  *   ].  addresses?(@.addresses.city has substring \"San \")
  84. $.friends[*].addresses?(\"San \" has substring @.addresses.city)
  85. $.friends[*].addresses?(@.addresses.city has substring $v1)
  86. $.LineItems.Part?(\"San \" has substring \"San \")
  87. $.friends[*].addresses?(@.addresses.city like_regex $v1)
  88. $.friends[*].addresses?(@.addresses.city starts with $v1)
  89. $.friends[*].addresses?(@.addresses.city starts  with $v1)
  90. $.friends[*].addresses   ?(@.addresses.city starts with $v1)
  91. $.friends[*].addresses   ?   (@.addresses.city starts  with $v1)

  Comparison symbols (==, !=, >, ...) combinations on both sides can be as follows:
  (scalar, scalar), (scalar, subpath) (subpath, scalar), (subpath, variable), (variable, subpath)
  Any other combination is illegal

  However, starts with, has substring, etc., have more restrictions and can only be the following combinations:
  (subpath, str), (subpath, variable), (str, str)
  */
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  ObString str0("$?(@.UPCCode.number() >= $v1 && @.addresses.city starts with $v1)");
  ObString str1("$?(@.UPCCode.number() >= $v1 && @.addresses.city starts with $v1)");
  ObJsonPath test_path(str0, &allocator);
  test_path.is_mysql_ = false;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  std::cout<<str1.ptr()<<std::endl;
  ret = test_path.parse_path();
  if(OB_FAIL(ret)){
    std::cout<<"fail"<<std::endl;
    std::cout<<test_path.bad_index_<<std::endl;
    if(test_path.bad_index_ >= test_path.expression_.length()){
      std::cout<<"end of path"<<std::endl;
    }else{
      std::cout<<test_path.expression_[test_path.bad_index_]<<std::endl;
      std::cout<<test_path.expression_[test_path.bad_index_ + 1]<<std::endl;
    }
  }
  ASSERT_EQ(OB_SUCCESS, ret);

  ObJsonBuffer str2(&allocator);
  ret = test_path.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  std::cout<<"to_string successed"<<std::endl;
  // Note that after member parsing, if keyname contains only letters and numbers, the double quotes will be removed
  // Direct comparison will result in an error
  ObString str3(str2.ptr());
  std::cout<<str3.ptr()<<std::endl;
  if(0 == strcmp(str1.ptr(), str2.ptr()))  std::cout<<"same"<<std::endl;
  ASSERT_EQ(str1, str3);

  str0 = "$?(@.User == \"ABULL\" && exists(@.LineItems[*]?(@.Part.UPCCode == 85391628927 && 1 == 1)))";
  str1 = "$?(@.User == \"ABULL\" && exists(@.LineItems[*]?(@.Part.UPCCode == 85391628927 && 1 == 1)))";
  ObJsonPath test_path1(str0, &allocator);
  test_path1.is_mysql_ = false;
  ret = test_path1.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);

  str2.reuse();
  ret = test_path1.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  std::cout<<str1.ptr()<<std::endl;
  std::cout<<str2.ptr()<<std::endl;
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  str0 = "$?(@.LineItems[*].*..Part.Description like \"Nixon\" && $v1 < @.UPCCode)";
  str1 = "$?(@.LineItems[*].*..Part.Description like \"Nixon\" && $v1 < @.UPCCode)";
  ObJsonPath test_path2(str0, &allocator);
  test_path2.is_mysql_ = false;
  ret = test_path2.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);

  str2.reuse();
  ret = test_path2.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  std::cout<<str1.ptr()<<std::endl;
  std::cout<<str2.ptr()<<std::endl;
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  str0 = "$.friends[3, 8 to 10, 12].cars[0]?(@.year has substring \"2016\")";
  str1 = "$.friends[3, 8 to 10, 12].cars[0]?(@.year has substring \"2016\")";
  ObJsonPath test_path3(str0, &allocator);
  test_path3.is_mysql_ = false;
  ret = test_path3.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);

  str2.reuse();
  ret = test_path3.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  std::cout<<str1.ptr()<<std::endl;
  std::cout<<str2.ptr()<<std::endl;
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));
}

TEST_F(TestJsonPath, test_bad_filter_to_string)
{
  /*
  filter expression
  comparison predicates
  has substring, starts with, like, like_regex, eq_regex, exists, !exists
  23. $.friends[3, 8 to 10, 12].cars[0]?($.year > 2016)
  24. $.friends[*].addresses?(@.addresses.city starts with "San )
  25. $.friends[3].cars[0]?(@.year.number( > 2016)
  26. $.friends[*].addresses?(@.addresses.city start with "San ")
  27. $.friends[*].addresses?(@.addresses.city had substring "San ")
  28. $.friends[*].addresses?(@.addresses.city likes "San ")
  29. $.friends[*].addresses?(@.addresses.city like-regex "San ")
  30. $.friends[*].addresses?(@.addresses.city like _regex "San ")
  31. $.friends[*].addresses?(@.addresses.city eq_Regex "San ")
  32. $?(@.User == "ABULL" && exist(@.LineItems[*]?(@.Part.UPCCode == 85391628927 && @.Quantity > 3)))
  33. $?(@.User == "ABULL" && exist @.LineItems[*]?(@.Part.UPCCode == 85391628927 && @.Quantity > 3))
  34. $?(@.User == "ABULL" && !exist(@.LineItems[*]?(@.Part.UPCCode == 85391628927 && @.Quantity > 3)))
  35. $.friends[3].cars[0]?(@.year.number() >  201 6)
  36. $.friends[3].cars[0]?(@.year.number() > )
  37. $.friends[3].cars[0]?(>  201)
  38. $.friends[3].cars[0]?(@.year.number() > 201.6.1)
  39. $.friends[3].cars[0]?(@.year.number() > --2016.1)
  40. $.friends[3].cars[0]?(@.year.number() > +4294967295)
  41. $.friends[3].cars[0]?(@.year.number() < 2-016)
  42. $.friends[3].cars[0]?(@.year.number() > = 2016)
  43. $.friends[3].cars[0]?(@.year.number() < = 2016)
  44. $.friends[3].cars[0]?(@.year.number() = = 2016)
  45. $.friends[3].cars[0]?(@.year.number() ! = 2016)
  46. $.friends[3].cars[0]?(@.year.number() != 2016 & & @.year.number() > 2016)
  47. $.friends[3].cars[0]?(@.year.number() != 2016 | | @.year.number() > 2016)
  48. $.friends[3].cars[0]?(@.year.number() != 2016 && !@.year.number() > 2016)
  49. $.friends[3].cars[0]?(@.year.date() == "2022-8-22 )
  50. $.LineItems.Part?(@.UPCCode ==   $ v1)
  51. $.LineItems.Part?(@.UPCCode == TRUE)
  52. $.LineItems.Part?(@.UPCCode == False)
  53. $.LineItems.Part?(@.UPCCode == NULL)
  mismatched/illegal brackets
  54. $.LineItems.Part?(@.UPCCode == null))
  55. $.LineItems.Part?((@.UPCCode == null)
  56. $.LineItems.Part?(@.UPCCode == null())
  57. $.LineItems.Part?([@.UPCCode ==   $ v1])
  58. $.LineItems.Part?(@.UPCCode ==   $v1
  59. $.LineItems.Part?@.UPCCode ==   $v1)
  60. $.LineItems.Part?()(@.UPCCode ==   $v1)
  illegal variable names
  61. $.LineItems.Part?([@.UPCCode ==   $1v])
  62. $.LineItems.Part?([@.UPCCode ==   $v@])
  63. $.LineItems.Part?([@.UPCCode ==   $v**])
  64. $.LineItems.Part?(@.UPCCode ==   $(v1))
  65. $.LineItems.Part?(@.UPCCode ==   $"v1")
  illegal combinations
  66. $.LineItems.Part?(2016 == $v1)
  67. $.LineItems.Part?("abc" == $v1)
  68. $.LineItems.Part?(true == $v1)
  69. $.LineItems.Part?(  $v1   == 2016)
  70. $.LineItems.Part?(  $v1   == "abc")
  71. $.LineItems.Part?(  $v1   == false)
  72. $.LineItems.Part?(  $v1   == $v4)
  73. $.LineItems.Part?(@.UPCCode == @.UPCCode)
  74. $.friends[*].addresses?(@.city like_regex 1234)
  75. $.friends[*].addresses?(@.city like_regex false)
  76. $.friends[*].addresses?(@.city eq_regex true)
  77. $.friends[*].addresses?(@.city like null)
  78. $.friends[*].addresses?(@.city like_regex @.UPCCode)
  79. $.LineItems.Part?(  $v1  starts with @.city)
  80. $.LineItems.Part?(  $v1  starts with $v1)
  81. $.LineItems.Part?(  $v1  starts with "abc")
  82. $.LineItems.Part?(  $v1  starts with 123.5)
  83. $.LineItems.Part?(  $v1  starts with true)
  84. $.LineItems.Part?(  "abc"  starts with $friends)
  85. $.LineItems.Part?( "abc"  starts with 123.5)
  86. $.LineItems.Part?(  "abc"  starts with true)
  87. $.LineItems.Part?(true starts with true)
  88. $.LineItems.Part?(  true  starts with $friends)
  89. $.LineItems.Part?( 12345 starts with $friends)
  90. $.LineItems.Part?(  true  starts with @.city)
  91. $.LineItems.Part?( 12345 starts with @.city)
  92. $?(exists(85391628927 == 85391628927))
  93. $?(exists($v1 == 85391628927))
  94. $?(exists($v1 == $v1))
  95. $?(exists($v1 == "abc"))
  96. $?(exists($v1 == true))
  97. $?(exists("abc" == "abc"))
  98. $?(exists("abc" has substring "abc"))
  99. $?(exists("abc" starts with "abc"))
  100. $?(exists(true == true))
  101. $?(exists(true != true))
  102. $.LineItems.Part?(@.UPCCode[1-] == null)
  103. $.LineItems.Part?(@.UPCCode[1].. == null)
  104. $.LineItems.Part?(@.UPCCode[1]...abc == null)
  105. $.LineItems.Part?(@.UPCCode[1, ] == null)
  106. $.friends[3].cars[0]?(@.year.number() != 2016 &| @.year.number() > 2016)
  107. $.friends[3].cars[0]?(@.year.number() != 2016 !& @.year.number() > 2016)
  108. $.friends[3].cars[0]?(@.year.number() != 2016 && !)@.year.number() > 2016))
  109. $.friends[3].cars[0]?(@.year.number() != 2016 && !(&&@.year.number() > 2016))
  110. $.friends[3].cars[0]?(@.year.number() == 2016 || && @.year.number() != 2016  &&  !(@.year.number() > 2016))
  */
  int ret = OB_SUCCESS;
  ObString str = "$.friends[3].cars[0]?(@.year.number() == 2016 || && @.year.number() != 2016  &&  !(@.year.number() > 2016))";
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path(str, &allocator);
  test_path.is_mysql_ = false;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  ret = test_path.parse_path();
  std::cout<<"str:"<<str.ptr()<<std::endl;
  std::cout<<"ret:"<<ret<<std::endl;
  ASSERT_EQ(true, OB_FAIL(ret));
  // ASSERT_EQ(num,test_path.bad_index_);
  std::cout<<test_path.bad_index_<<std::endl;
  ObString bad_expression_(test_path.expression_.ptr()+test_path.bad_index_);
  std::cout<<"bad str:"<<bad_expression_.ptr()<<std::endl;
  /*
  if(OB_FAIL(ret)){
    std::cout<<"fail"<<std::endl;
    std::cout<<test_path.bad_index_<<std::endl;
    if(test_path.bad_index_>= test_path.expression_.length()){
      std::cout<<"end of path"<<std::endl;
    }else{
      std::cout<<test_path.expression_[test_path.bad_index_]<<std::endl;
      std::cout<<test_path.expression_[test_path.bad_index_+1]<<std::endl;
    }
  }
  */
}

TEST_F(TestJsonPath, test_func_node)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path("$.a.type()", &allocator);
  test_path.is_mysql_ = false;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  ret = test_path.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // There is only one node
  ASSERT_EQ(2, test_path.path_node_cnt());
  ASSERT_EQ(JPN_MEMBER,test_path.path_nodes_[0]->get_node_type());
  ASSERT_EQ(JPN_TYPE,test_path.path_nodes_[1]->get_node_type());
}
// Test whether array_range_wildcard_node can be parsed correctly
TEST_F(TestJsonPath, test_array_wildcard_node)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path("$[*]", &allocator);
  test_path.is_mysql_ = false;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  ret = test_path.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // There is only one node
  ASSERT_EQ(1, test_path.path_node_cnt());
  ASSERT_EQ(JPN_ARRAY_CELL_WILDCARD,test_path.path_nodes_[0]->get_node_type());
}
// Test whether member_wildcard_node can be parsed correctly
TEST_F(TestJsonPath, test_member_wildcard_node)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path("$.*", &allocator);
  test_path.is_mysql_ = false;
  ret = test_path.parse_path();
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  }
  ASSERT_EQ(OB_SUCCESS, ret);
  // There is only one node
  ASSERT_EQ(1, test_path.path_node_cnt());
  ASSERT_EQ(JPN_MEMBER_WILDCARD,test_path.path_nodes_[0]->get_node_type());
}
// Test whether member_node can be parsed correctly
TEST_F(TestJsonPath, test_member_node)
{
  int ret = OB_SUCCESS;
  ObString str_orgin("$.name");
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path(str_orgin, &allocator);
  test_path.is_mysql_ = false;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  ret = test_path.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  std::cout<<"test"<<std::endl;
  // There is only one node
  ASSERT_EQ(1, test_path.path_node_cnt());
  ASSERT_EQ(JPN_MEMBER,test_path.path_nodes_[0]->get_node_type());
  const auto &member = test_path.path_nodes_[0]->node_content_.member_;
  ObString str(member.len_, member.object_name_);
  ASSERT_TRUE(str.case_compare("name") == 0);
  std::cout<<test_path.path_nodes_[0]->node_content_.member_.object_name_<<std::endl;
}
// Test whether ellipsis_node can be parsed correctly
TEST_F(TestJsonPath, test_ellipsis_node)
{
  int ret = OB_SUCCESS;
  set_compat_mode(oceanbase::lib::Worker::CompatMode::MYSQL);
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path("$**[10]", &allocator);
  ret = test_path.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // There is only one node
  ASSERT_EQ(2, test_path.path_node_cnt());
  ASSERT_EQ(JPN_WILDCARD_ELLIPSIS,test_path.path_nodes_[0]->get_node_type());
  ASSERT_EQ(JPN_ARRAY_CELL,test_path.path_nodes_[1]->get_node_type());
  ASSERT_EQ(10,test_path.path_nodes_[1]->node_content_.array_cell_.index_);
  ASSERT_EQ(false,test_path.path_nodes_[1]->node_content_.array_cell_.is_index_from_end_);
}
// Test whether the path expression can be successfully parsed
TEST_F(TestJsonPath, test_parse_path)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path("$[last-10 to last-1]", &allocator);
  ret = test_path.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // There is only one node
  ASSERT_EQ(1, test_path.path_node_cnt());

  if (OB_SUCC(ret)) {
    for(int i=0; i<test_path.path_node_cnt(); ++i)
    {

      if (i==0) {
        // Node type has wildcard
        // ASSERT_EQ(JPN_MEMBER_WILDCARD,test_path.path_nodes_[i]->node_type);
        std::cout<<i<<std::endl;
        //ASSERT_EQ(JPN_ARRAY_CELL_WILDCARD,test_path.path_nodes_[i]->node_type);
        /*
        ASSERT_EQ(JPN_MEMBER,test_path.path_nodes_[i]->node_type);
        std::cout<<test_path.path_nodes_[0]->node_content_.member_.object_name_<<std::endl;
        ASSERT_EQ(JPN_ARRAY_CELL,test_path.path_nodes_[i]->node_type);
        ASSERT_EQ(10,test_path.path_nodes_[i]->node_content_.array_cell_.index_);
        ASSERT_EQ(true,test_path.path_nodes_[i]->node_content_.array_cell_.is_index_from_end_);
        */
        ASSERT_EQ(10,test_path.path_nodes_[i]->node_content_.array_range_.first_index_);
        ASSERT_EQ(true,test_path.path_nodes_[i]->node_content_.array_range_.is_first_index_from_end_);
        ASSERT_EQ(1,test_path.path_nodes_[i]->node_content_.array_range_.last_index_);
        ASSERT_EQ(true,test_path.path_nodes_[i]->node_content_.array_range_.is_last_index_from_end_);

      }
    }
  } else {
    std::cout<<"fail\n";
  }
}


// test tostring
// test array_cell node to string
TEST_F(TestJsonPath, test_array_cell_node_to_string)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonBuffer str(&allocator);
  ObJsonPathBasicNode node(&allocator,10, true);
  node.node_to_string(str, true, false);
  std::cout<<str.ptr()<<std::endl;
}

// test array_range node to string
TEST_F(TestJsonPath, test_array_range_node_to_string)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonBuffer str(&allocator);
  ObJsonPathBasicNode node(&allocator, 5, true, 1, true);
  node.node_to_string(str, true, false);
  std::cout<<"test\n";
  std::cout<<str.ptr()<<std::endl;
}

// test array_wildcard node to string
TEST_F(TestJsonPath, test_array_wildcard_node_to_string)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonBuffer str(&allocator);
  ObJsonPathBasicNode node(&allocator);
  ASSERT_EQ(0, node.init(JPN_ARRAY_CELL_WILDCARD, true));
  node.node_to_string(str, true, false);
  std::cout<<"test\n";
  std::cout<<str.ptr()<<std::endl;
}

// test member_wildcard node to string
TEST_F(TestJsonPath, test_member_wildcard_node_to_string)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonBuffer str(&allocator);
  ObJsonPathBasicNode node(&allocator);
  ASSERT_EQ(0, node.init(JPN_MEMBER_WILDCARD, true));
  node.node_to_string(str, true, false);
  std::cout<<"test\n";
  std::cout<<str.ptr()<<std::endl;
}

// test member node to string
TEST_F(TestJsonPath, test_member_node_to_string)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonBuffer str(&allocator);
  ObString kn("keyname");
  ObJsonPathBasicNode node(&allocator, kn);
  node.node_to_string(str, true, false);
  std::cout<<"test\n";
  std::cout<<str.ptr()<<std::endl;
}

// test ellipsis node to string
TEST_F(TestJsonPath, test_ellipsis_node_to_string)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonBuffer str(&allocator);
  ObJsonPathBasicNode node(&allocator);
  ASSERT_EQ(0, node.init(JPN_WILDCARD_ELLIPSIS, true));
  node.node_to_string(str, true, false);
  std::cout<<"test\n";
  std::cout<<str.ptr()<<std::endl;
}

// test ObJsonPath::to_string()
TEST_F(TestJsonPath, test_path_to_string)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonBuffer str(&allocator);
  ObJsonPath test_path(&allocator);
  ObString name = "keyname";
  ObJsonPathBasicNode node(&allocator);
  ObJsonPathBasicNode member_node(&allocator, name);
  test_path.is_mysql_ = false;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  ASSERT_EQ(0, node.init(JPN_DOT_ELLIPSIS, test_path.is_mysql_));
  ObJsonPathNode* fa = &node;
  test_path.append(fa);
  fa =&member_node;
  test_path.append(fa);
  test_path.to_string(str);
  std::cout<<"Path\n";
  std::cout<<str.ptr()<<std::endl;
}

// test bad path
// record bad_index
TEST_F(TestJsonPath, test_bad_path)
{
  int ret = OB_SUCCESS;
  ObString str = "$\"abcd\"";
  // int num = 5;
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path(str, &allocator);
  ret = test_path.parse_path();
  std::cout<<"str:"<<str.ptr()<<std::endl;
  std::cout<<"ret:"<<ret<<std::endl;
  ASSERT_EQ(true, OB_FAIL(ret));
  // ASSERT_EQ(num,test_path.bad_index_);
  std::cout<<test_path.bad_index_<<std::endl;
}

TEST_F(TestJsonPath, test_random)
{
  int dice = 0;
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonBuffer str(&allocator);
  str.append("$");
  int node_num = 99;
  for (int i=0; i<node_num; ++i) {
    dice = rand() % 5;
    switch (dice) {
      case 0:
      // Add JPN_MEMBER
        str.append(".keyname");
        break;

      case 1:
      // Add JPN_MEMBER_WILDCARD
        str.append(".*");
        break;

      case 2:
      // JPN_ARRAY_CELL
        str.append("[last-5]");
        break;

      case 3:
      // JPN_ARRAY_RANGE
        str.append("[10 to last-1]");
        break;

      case 4:
      // JPN_ARRAY_CELL_WILDCARD
        str.append("[*]");
        break;

      case 5:
      // JPN_WILDCARD_ELLIPSIS
        str.append("**");
        break;

      default:
        break;
    }
  }
  // Prevent the last node from being **
  str.append("[1]");

  int ret = OB_SUCCESS;
  ObString str_origin(str.ptr());
  std::cout<<str_origin.ptr()<<std::endl;

  ObJsonPath test_path(str_origin, &allocator);
  test_path.is_mysql_ = true;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  ret = test_path.parse_path();
  std::cout<<"count:"<<test_path.path_node_cnt()<<std::endl;
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  ObJsonBuffer str2(&allocator);
  ret = test_path.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ObString str3(str2.ptr());
  ASSERT_EQ(str_origin, str3);
  std::cout<<str2.ptr()<<std::endl;

}

// test good path including func_node
// include parse and to_string
TEST_F(TestJsonPath, test_good_func_path)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  // str0 is used for parsing
  // str1 is used to compare with the result of to_string
  /* good path
  1. $.friends[3, 8 to 10, 12].cars[0].abs()
  2. $.friends[3, 8 to 10, 12].cars[0].boolean()
  3. $.friends[3, 8 to 10, 12].cars[0].booleanOnly()
  4. $.friends[3, 8 to 10, 12].cars[0].ceiling()
  5. $.friends[3, 8 to 10, 12].cars[0].floor()
  6. $.friends[3, 8 to 10, 12].cars[0].double()
  7. $.type()
  8. $.memeber.type()
  9. $.size()
  10. $[*].size()
  11. $.number()
  12. $[1 to last-10, 15].number()
  13. $.numberOnly()
  14. $.string()
  15. $..string()
  16. $.stringOnly()
  17. $.length()
  18. $.lower()
  19. $.*.lower()
  20. $.upper()
  21. $.date()
  22. $.timestamp()
  23. $.  timestamp()
  24. $.timestamp(  )
  25. $.timestamp  ()
  26. "$.timestamp  (  )  "
  27. $.abc[10][last-1 to 96].\"number 1\".*[*]..abs()
  28. $[*]..size()
  */
  ObString str0 = "$[*]..size()";
  ObString str1 = "$[*]..size()";

  ObJsonPath test_path(str0, &allocator);
  // Parse
  test_path.is_mysql_ = false;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  ret = test_path.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  std::cout<<"parse successed"<<std::endl;
  // to_string
  ObJsonBuffer str2(&allocator);
  ret = test_path.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  std::cout<<"to_string successed"<<std::endl;
  // Verify if they are equal (ObSqlString direct comparison will cause an error because == is not overloaded)
  ObString str3(str2.ptr());
  std::cout<<str1.ptr()<<std::endl;
  std::cout<<str3.ptr()<<std::endl;
  ASSERT_EQ(str1, str3);

  // abs()
  str0 = "$.abs()";
  str1 = "$.abs()";
  ObJsonPath test_path2(str0, &allocator);
  // Parse
  test_path2.is_mysql_ = false;
  ret = test_path2.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path2.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  // boolean()
  str0 = "$.boolean()";
  str1 = "$.boolean()";
  ObJsonPath test_path3(str0, &allocator);
  // Parse
  test_path3.is_mysql_ = false;
  ret = test_path3.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path3.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  // booleanOnly()
  str0 = "$.booleanOnly()";
  str1 = "$.booleanOnly()";
  ObJsonPath test_path4(str0, &allocator);
  // Parse
  test_path4.is_mysql_ = false;
  ret = test_path4.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path4.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  // ceiling()
  str0 = "$.ceiling()";
  str1 = "$.ceiling()";
  ObJsonPath test_path5(str0, &allocator);
  // Parse
  test_path5.is_mysql_ = false;
  ret = test_path5.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path5.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  // date()
  str0 = "$.date()";
  str1 = "$.date()";
  ObJsonPath test_path6(str0, &allocator);
  // Parse
  test_path6.is_mysql_ = false;
  ret = test_path6.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path6.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  // double()
  str0 = "$.double()";
  str1 = "$.double()";
  ObJsonPath test_path7(str0, &allocator);
  // Parse
  test_path7.is_mysql_ = false;
  ret = test_path7.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path7.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  // floor()
  str0 = "$.floor()";
  str1 = "$.floor()";
  ObJsonPath test_path8(str0, &allocator);
  // Parse
  test_path8.is_mysql_ = false;
  ret = test_path8.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path8.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  // length()
  str0 = "$.length()";
  str1 = "$.length()";
  ObJsonPath test_path9(str0, &allocator);
  // Parse
  test_path9.is_mysql_ = false;
  ret = test_path9.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path9.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  // lower()
  str0 = "$.lower()";
  str1 = "$.lower()";
  ObJsonPath test_path10(str0, &allocator);
  // Parse
  test_path10.is_mysql_ = false;
  ret = test_path10.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path10.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  // number()
  str0 = "$.number()";
  str1 = "$.number()";
  ObJsonPath test_path11(str0, &allocator);
  // Parse
  test_path11.is_mysql_ = false;
  ret = test_path11.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path11.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  // numberOnly()
  str0 = "$.numberOnly()";
  str1 = "$.numberOnly()";
  ObJsonPath test_path12(str0, &allocator);
  // Parse
  test_path12.is_mysql_ = false;
  ret = test_path12.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path12.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  // string()
  str0 = "$.string()";
  str1 = "$.string()";
  ObJsonPath test_path13(str0, &allocator);
  // Parse
  test_path13.is_mysql_ = false;
  ret = test_path13.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path13.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  // stringOnly()
  str0 = "$.stringOnly()";
  str1 = "$.stringOnly()";
  ObJsonPath test_path14(str0, &allocator);
  // Parse
  test_path14.is_mysql_ = false;
  ret = test_path14.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path14.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  // timestamp()
  str0 = "$.timestamp()";
  str1 = "$.timestamp()";
  ObJsonPath test_path15(str0, &allocator);
  // Parse
  test_path15.is_mysql_ = false;
  ret = test_path15.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path15.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));

  // type()
  str0 = "$.type()";
  str1 = "$.type()";
  ObJsonPath test_path16(str0, &allocator);
  // Parse
  test_path16.is_mysql_ = false;
  ret = test_path16.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  str2.reuse();
  ret = test_path16.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, strcmp(str1.ptr(), str2.ptr()));
}

// test good path including func_node
// include parse and to_string
TEST_F(TestJsonPath, test_bad_func_path)
{
  int ret = OB_SUCCESS;
  /*
  1. $.friends[3, 8 to 10, 12].cars[0].a bs()
  2. $.friends[3, 8 to 10, 12].cars[0].boo lean()
  3. $.friends[3, 8 to 10, 12].cars[0].boolean Only()
  4. $.friends[3, 8 to 10, 12].cars[0].ceilng()
  5. $.friends[3, 8 to 10, 12].cars[0].float()
  6. $.friends[3, 8 to 10, 12].cars[0].doubble()
  7. $.tyype()
  8. $.siize()
  9. $.Size()
  10. $.numbe()
  11. $.numbeR()
  12. $.numberOn()
  13. $.string().length()
  14. $.stringOny()
  15. $.length()[3]
  16. $.lower().member
  17. $.*.lower()[*]
  18. $..upper().a
  19. $.data()
  20. $.data(1)
  21. $.timesdamp()
  22. $.  timestamps()
  23. $date()
  */
  ObString str = "$.date().a";
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path(str, &allocator);
  test_path.is_mysql_ = false;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  ret = test_path.parse_path();
  std::cout<<"str:"<<str.ptr()<<std::endl;
  std::cout<<"ret:"<<ret<<std::endl;
  ASSERT_EQ(true, OB_FAIL(ret));
  // ASSERT_EQ(num,test_path.bad_index_);
  std::cout<<test_path.bad_index_<<std::endl;
  ObString bad_expression_(test_path.expression_.ptr() + test_path.bad_index_);
  std::cout<<"bad str:"<<bad_expression_.ptr()<<std::endl;
}


// test good path
// include parse and to_string
TEST_F(TestJsonPath, test_oracle_good_path)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  // Used for parsing
  ObString str0 = "$.branch_code";
  //ObString str0 = "$.abc.\"\".def";
  //.keyname[last-10 to last-1][5].a
  // ObString str0 = "$[9999999999999].keyname";
  std::cout<<str0.ptr()<<std::endl;
  // Used for comparison with to_string (i.e., str0 without extra spaces)
  // The interface for removing escape characters to be implemented
  // At this time, the escape character will also be processed as part of the keyname
  ObString str1 = "$.branch_code";
  // ObString str1 = "$[9999999999999].keyname";
  ObJsonPath test_path(str0, &allocator);
  // Parse
  test_path.is_mysql_ = false;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  ret = test_path.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  std::cout<<"parse successed"<<std::endl;
  // to_string
  ObJsonBuffer str2(&allocator);
  ret = test_path.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);
  std::cout<<"to_string successed"<<std::endl;
  // Verify if they are equal (ObSqlString direct comparison will cause an error because == is not overloaded)
  ObString str3(str2.ptr());
  std::cout<<str2.ptr()<<std::endl;
  std::cout<<str3.ptr()<<std::endl;
  if(0 == strcmp(str1.ptr(), str2.ptr()))  std::cout<<"same"<<std::endl;
  ASSERT_EQ(str1, str3);
}

TEST_F(TestJsonPath, test_oracle_bad_path)
{
  int ret = OB_SUCCESS;
  ObString str = "$[1,]";
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonPath test_path(str, &allocator);
  test_path.is_mysql_ = false;
  ret = test_path.parse_path();
  ASSERT_EQ(true, OB_FAIL(ret));
  std::cout<<test_path.bad_index_<<std::endl;
  ObString str1 = "$..[1]";
  ObJsonPath test_path1(str, &allocator);
  test_path1.is_mysql_ = false;
  ret = test_path1.parse_path();
  ASSERT_EQ(true, OB_FAIL(ret));
  std::cout<<test_path1.bad_index_<<std::endl;
}

// test good path
// include parse and to_string
TEST_F(TestJsonPath, test_good_path)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);
  // Used for parsing
  ObString str0 = "$.\"abc d\"";
  //ObString str0 = "$.abc.\"\".def";
  //.keyname[last-10 to last-1][5].a
  // ObString str0 = "$[9999999999999].keyname";
  std::cout<<str0.ptr()<<std::endl;
  // Used for comparison with to_string (i.e., str0 without extra spaces)
  // The interface for removing escape characters to be implemented
  // At this time, the escape character will also be processed as part of the keyname
  ObString str1 = "$.\"abc d\"";
  // ObString str1 = "$[9999999999999].keyname";
  ObJsonPath test_path(str0, &allocator);
  // Parse
  test_path.is_mysql_ = false;
  if(test_path.is_mysql_ == false){
    std::cout<<"oracle"<<std::endl;
  } else {
    std::cout<<"mysql"<<std::endl;
  }
  ret = test_path.parse_path();
  ASSERT_EQ(OB_SUCCESS, ret);
  // to_string
  ObJsonBuffer str2(&allocator);
  ret = test_path.to_string(str2);
  ASSERT_EQ(OB_SUCCESS, ret);

  for(int i=0; i<test_path.path_node_cnt(); ++i)
  {
    std::cout<<"type:"<<test_path.path_nodes_[i]->get_node_type()<<std::endl;
    if (i==0) {
      std::cout<<"content:"<<test_path.path_nodes_[i]->node_content_.member_.object_name_<<std::endl;
    }
    if (i==1) {
      std::cout<<"content:"<<test_path.path_nodes_[i]->node_content_.array_cell_.index_<<std::endl;
      std::cout<<"content:"<<test_path.path_nodes_[i]->node_content_.array_cell_.is_index_from_end_<<std::endl;
    }
  }
  // Verify if they are equal (ObSqlString direct comparison will cause an error because == is not overloaded)
  ObString str3(str2.ptr());
  std::cout<<str2.ptr()<<std::endl;
  ASSERT_EQ(str1, str3);
  std::cout<<"end test"<<std::endl;
}


TEST_F(TestJsonPath, test_pathcache_funcion) {
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);

  ObJsonPathCache path_cache(&allocator);
  ObString ok_path1 = "$.\"abc d\"";

  // initial state verify
  ASSERT_EQ(path_cache.get_allocator(), &allocator);
  ASSERT_EQ(path_cache.path_stat_at(0), ObPathParseStat::UNINITIALIZED);

  // test a nornal json path
  ObJsonPath* json_path = NULL;
  int ok_path1_idx = path_cache.size();

  // parse a normal path
  ret = path_cache.find_and_add_cache(json_path, ok_path1, ok_path1_idx);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(path_cache.path_stat_at(ok_path1_idx), ObPathParseStat::OK_NOT_NULL);

  // can read again
  ObJsonPath* read_ok_path1 = path_cache.path_at(ok_path1_idx);
  ASSERT_STREQ(json_path->get_path_string().ptr(), read_ok_path1->get_path_string().ptr());

  // cache stratety works, iff the path string unchanged, the path needn't parse again
  ret = path_cache.find_and_add_cache(json_path, ok_path1, ok_path1_idx);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(json_path, read_ok_path1);

  ObString ok_path2 = "  $.\"  abc d \"  ";
  // some spaces do not has any effect upon the path cache
  ret = path_cache.find_and_add_cache(json_path, ok_path2, ok_path1_idx);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(json_path, read_ok_path1);
  ObJsonPath* read_ok_path2 = path_cache.path_at(ok_path1_idx);
  ASSERT_EQ(read_ok_path2, read_ok_path1);
}

TEST_F(TestJsonPath, test_pathcache_exprire) {
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);

  ObJsonPathCache path_cache(&allocator);
  ObString ok_path1 = "$.\"abc d\"";

  // test a nornal json path
  ObJsonPath* json_path = NULL;
  int ok_path1_idx = path_cache.size();

  // parse a normal path
  ret = path_cache.find_and_add_cache(json_path, ok_path1, ok_path1_idx);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(path_cache.path_stat_at(ok_path1_idx), ObPathParseStat::OK_NOT_NULL);

  ObJsonPath* read_ok_path1 = path_cache.path_at(ok_path1_idx);

  ObString ok_path2 = "  $.\"efs d \"";
  // some spaces do not has any effect upon the path cache
  ret = path_cache.find_and_add_cache(json_path, ok_path2, ok_path1_idx);
  ASSERT_EQ(ret, OB_SUCCESS);

  // json path string differs, cache invalid
  ASSERT_NE(json_path, read_ok_path1);
  ObJsonPath* read_ok_path2 = path_cache.path_at(ok_path1_idx);
  ASSERT_EQ(read_ok_path2, json_path);
}

TEST_F(TestJsonPath, test_pathcache_reset) {
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::TEST);

  ObJsonPathCache path_cache(&allocator);
  ObString ok_path1 = "$.\"abc d\"";

  // test a nornal json path
  ObJsonPath* json_path = NULL;
  int ok_path1_idx = path_cache.size();

  // parse a normal path
  ret = path_cache.find_and_add_cache(json_path, ok_path1, ok_path1_idx);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(path_cache.path_stat_at(ok_path1_idx), ObPathParseStat::OK_NOT_NULL);

  path_cache.reset();
  ASSERT_EQ(path_cache.path_stat_at(ok_path1_idx), ObPathParseStat::UNINITIALIZED);
  ASSERT_EQ(path_cache.size(), 0);
}


int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  /*
  system("rm -f test_json_path.log");
  OB_LOGGER.set_file_name("test_json_path.log");
  OB_LOGGER.set_log_level("INFO");
  */
  return RUN_ALL_TESTS();
}
