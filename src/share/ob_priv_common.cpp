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

#define USING_LOG_PREFIX SHARE_SCHEMA

#include "share/ob_priv_common.h"
#include "ob_define.h"

namespace oceanbase
{
using namespace common;

namespace share
{

#define N_PIRVS_PER_GROUP 30

/* 300 permissions */
int group_id_arr[] = 
{
    0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
};

/* 300 permissions */
int th_in_group_arr[] = 
{
    0,
    0,  2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26, 28,        /* 1- 15 */
    30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,        /* 16-30 */    
    0,  2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26, 28,        /* 31-45 */
    30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,        /* 46-60 */    
    0,  2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26, 28,        /* 61-75 */
    30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,        /* 76-90 */    
    0,  2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26, 28,        /* 91-105 */
    30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,        /* 106-120 */    
    0,  2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26, 28,        /* 121-135 */
    30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,        /* 136-150 */    
    0,  2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26, 28,        /* 151-165 */
    30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,        /* 166-180 */    
    0,  2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26, 28,        /* 181-195 */
    30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,        /* 196-210 */    
    0,  2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26, 28,        /* 211-225 */
    30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,        /* 226-240 */    
    0,  2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26, 28,        /* 241-255 */
    30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,        /* 256-270 */    
    0,  2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26, 28,        /* 271-285 */
    30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,        /* 286-300 */    

};

#define NTHID_TO_PACKED(i) (1LL << i)

int ObPrivPacker::init_packed_array(
    ObPackedPrivArray &array)
{
  int ret = OB_SUCCESS;
  array.reset();
  OZ (array.push_back(0));
  OZ (array.push_back(0));
  OZ (array.push_back(0));
  return ret;
}

int ObPrivPacker::raw_priv_to_packed_info(
    const uint64_t option,
    const ObRawPriv priv, 
    int &group_id,
    ObPrivSet &packed_priv)
{  
  int ret = OB_SUCCESS;
  CK (priv > 0 && priv < PRIV_ID_MAX);
  group_id = group_id_arr[priv];
  packed_priv = NTHID_TO_PACKED(th_in_group_arr[priv]);
  CK (option == NO_OPTION || option == ADMIN_OPTION);
  if (option == ADMIN_OPTION) {
    packed_priv |= packed_priv << 1;
  }
  return ret;
}

int ObPrivPacker::raw_obj_priv_to_packed_info(
    const uint64_t option,
    const ObRawObjPriv priv,
    ObPackedObjPriv &packed_priv)
{  
  int ret = OB_SUCCESS;
  CK (priv >= 0 && priv <= OBJ_PRIV_ID_MAX);
  packed_priv = NTHID_TO_PACKED(th_in_group_arr[priv]);
  CK (option == NO_OPTION || option == GRANT_OPTION);
  if (option == GRANT_OPTION) {
    packed_priv |= packed_priv << 1;
  }
  return ret;
}

/* Determine if a relative priv id is in the privset, and output option information */
int ObPrivPacker::has_raw_priv(
    const ObRawPriv raw_priv,
    const ObPrivSet priv_set,
    bool &exists,
    uint64_t &option)
{
  int ret = OB_SUCCESS;
  ObPrivSet packed_priv;
  exists = false;
  CK (raw_priv >= 0 && raw_priv <= PRIV_ID_MAX);
  if (OB_SUCC(ret)) {
    packed_priv = NTHID_TO_PACKED(th_in_group_arr[raw_priv]);
    if (packed_priv & priv_set) {
      exists = true;
      if ((packed_priv << 1) & priv_set) {
        option = 1;
      }
    }
  }
  return ret;
}

/* Restore relative raw priv and group idx to raw priv, according to option, push back to the corresponding list */
int ObPrivPacker::push_back_raw_priv_array(
    ObRawPriv raw_priv,
    bool exists,
    uint64_t option,
    ObRawPrivArray &raw_priv_array,
    ObRawPrivArray &raw_priv_array_with_option)
{
  int ret = OB_SUCCESS;
  if (exists) {
    if (option) {
      OZ (raw_priv_array_with_option.push_back(raw_priv));
    }
    else {
      OZ (raw_priv_array.push_back(raw_priv));
    }
  }
  return ret;
}

/* Parse packed_array to raw privs array, output two lists based on whether there is an option */

bool ObOraPrivCheck::user_is_owner(
   const ObString &user_name,
   const ObString &db_name)
{
  return ObCharset::case_sensitive_equal(user_name, db_name);
}

int ObPrivPacker::pack_raw_priv(
    const uint64_t option,
    const ObRawPriv priv,
    ObPackedPrivArray &packed_array)
{
  int group_id;
  ObPrivSet packed_priv;    
  int ret = OB_SUCCESS;
  
  OZ (raw_priv_to_packed_info(option, priv, group_id, packed_priv));

  if (OB_SUCC(ret)) {
    if (packed_array.count() > 0) {
      if (group_id >= packed_array.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("group id error", K(ret));
      } else {
        packed_array[group_id] |= packed_priv;
      }
    } else {
      LOG_WARN("packed_array has no space reserved");
    }
  }
  return ret;
}


int ObPrivPacker::pack_raw_obj_priv(
    const uint64_t option,
    const ObRawPriv priv,
    ObPackedObjPriv &packed_obj_priv)
{ 
  int ret = OB_SUCCESS;  
  OZ (raw_obj_priv_to_packed_info(option, priv, packed_obj_priv),
      option, priv);
  return ret;
}

int ObPrivPacker::pack_raw_obj_priv_list(
    const uint64_t option,
    const ObRawObjPrivArray priv_list,
    ObPackedObjPriv &packed_obj_priv)
{ 
  int ret = OB_SUCCESS;
  packed_obj_priv = 0;
  for (int i = 0; OB_SUCC(ret) && i < priv_list.count(); i++) {
    const ObRawObjPriv priv = priv_list.at(i);
    OZ (append_raw_obj_priv(option, priv, packed_obj_priv),
        option, priv);
  }
  return ret;
}

int ObPrivPacker::append_raw_obj_priv(
    const uint64_t option,
    const ObRawObjPriv priv,
    ObPackedObjPriv &packed_obj_privs)
{
  int ret = OB_SUCCESS;
  ObPackedObjPriv tmp_packed_priv;
  OZ (raw_obj_priv_to_packed_info(option, priv, tmp_packed_priv));
  OX (packed_obj_privs |= tmp_packed_priv);
  return ret;
}

int ObPrivPacker::raw_obj_priv_from_pack(
    const ObPackedObjPriv &packed_obj_privs,
    ObRawObjPrivArray &raw_priv_array)
{
  int ret = OB_SUCCESS;
  ObRawObjPriv raw_priv;
  bool exists;
  raw_priv_array.reset();
  if (packed_obj_privs > 0) {
    CK (OBJ_PRIV_ID_MAX <= N_PIRVS_PER_GROUP);
    for (raw_priv = 1; OB_SUCC(ret) && raw_priv <= OBJ_PRIV_ID_MAX; raw_priv ++) {
      OZ (ObOraPrivCheck::raw_obj_priv_exists(raw_priv, packed_obj_privs, exists));
      if (OB_SUCC(ret) && exists) {
        OZ (raw_priv_array.push_back(raw_priv));
      }
    }
  }
  return ret;
}

int ObPrivPacker::raw_option_obj_priv_from_pack(
    const ObPackedObjPriv &packed_obj_privs,
    ObRawObjPrivArray &raw_priv_array)
{
  int ret = OB_SUCCESS;
  ObRawObjPriv raw_priv;
  bool exists;
  raw_priv_array.reset();
  if (packed_obj_privs > 0) {
    CK (OBJ_PRIV_ID_MAX <= N_PIRVS_PER_GROUP);
    for (raw_priv = 1; OB_SUCC(ret) && raw_priv <= OBJ_PRIV_ID_MAX; raw_priv ++) {
      OZ (ObOraPrivCheck::raw_obj_priv_exists(raw_priv, GRANT_OPTION, packed_obj_privs, exists));
      if (OB_SUCC(ret) && exists) {
        OZ (raw_priv_array.push_back(raw_priv));
      }
    }
  }
  return ret;
}

int ObPrivPacker::raw_no_option_obj_priv_from_pack(
    const ObPackedObjPriv &packed_obj_privs,
    ObRawObjPrivArray &raw_priv_array)
{
  int ret = OB_SUCCESS;
  ObRawObjPriv raw_priv;
  bool exists_grant;
  bool exists;
  raw_priv_array.reset();
  if (packed_obj_privs > 0) {
    CK (OBJ_PRIV_ID_MAX <= N_PIRVS_PER_GROUP);
    for (raw_priv = 1; OB_SUCC(ret) && raw_priv <= OBJ_PRIV_ID_MAX; raw_priv ++) {
      OZ (ObOraPrivCheck::raw_obj_priv_exists(raw_priv, packed_obj_privs, exists));
      OZ (ObOraPrivCheck::raw_obj_priv_exists(raw_priv, GRANT_OPTION, 
              packed_obj_privs, exists_grant));
      if (OB_SUCC(ret) && exists && !exists_grant) {
        OZ (raw_priv_array.push_back(raw_priv));
      }
    }
  }
  return ret;
}

int ObPrivPacker::merge_two_packed_array(
    ObPackedPrivArray &packed_array_1,
    const ObPackedPrivArray &packed_array_2)
{
  int ret = OB_SUCCESS;
  int i;
  if (packed_array_1.count() == 0) {
    packed_array_1 = packed_array_2;
  } else if (packed_array_2.count() > 0) {
    CK (packed_array_1.count() == packed_array_2.count());
    for (i = 0; i < packed_array_1.count() && OB_SUCC(ret); i++) {
      packed_array_1[i] |= packed_array_2[i];
    }
  }
  return ret;
}

int ObOraPrivCheck::raw_sys_priv_exists(
    const uint64_t option,
    const ObRawPriv priv,
    const ObPackedPrivArray &packed_array,
    bool &exists)
{
  int ret = OB_SUCCESS;
  int group_id;
  ObPrivSet packed_priv;    
  exists = false;
  if (packed_array.count() > 0) {
    if (priv == OBJ_PRIV_ID_NONE) {
      exists = FALSE;
    } else {
      OZ (ObPrivPacker::raw_priv_to_packed_info(option, priv, group_id, packed_priv));
      if (OB_SUCC(ret)) {
        if (group_id >= packed_array.count()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("group id error", K(option), K(priv), K(packed_array), K(group_id), K(ret));
        } else if (OB_TEST_PRIVS(packed_array.at(group_id), packed_priv)) {
          exists = true;
        }
      }
    }
  }
  return ret;
}

int ObOraPrivCheck::raw_sys_priv_exists(
    const ObRawPriv priv,
    const ObPackedPrivArray &packed_array,
    bool &exists)
{
  int ret = OB_SUCCESS;
  int group_id;
  ObPrivSet packed_priv;    
  exists = false;
  if (priv > 0 && packed_array.count() > 0) {
    OZ (ObPrivPacker::raw_priv_to_packed_info(NO_OPTION, priv, group_id, packed_priv), priv);
    if (OB_SUCC(ret)) {
      if (group_id >= packed_array.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("group id error", K(priv), K(packed_array), K(group_id), K(ret));
      } else if (OB_TEST_PRIVS(packed_array.at(group_id), packed_priv)) {
        exists = true;
      }
    }
  }
  return ret;
}



int ObOraPrivCheck::raw_obj_priv_exists(
    const ObRawObjPriv priv,
    const ObPackedObjPriv &obj_privs,
    bool &exists)
{
  int ret = OB_SUCCESS;
  exists = false;
  if (priv > 0) {
    ObPrivSet packed_priv;
    OZ (ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, priv, packed_priv));
    OX (exists = OB_TEST_PRIVS(obj_privs, packed_priv));
  }
  return ret;
}

int ObOraPrivCheck::raw_obj_priv_exists_with_info(
    const ObRawObjPriv priv,
    const ObPackedObjPriv &obj_privs,
    bool &exists,
    uint64_t &option)
{
  int ret = OB_SUCCESS;
  exists = false;
  option = NO_OPTION;
  if (priv > 0) {
    ObPrivSet packed_priv;
    OZ (ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, priv, packed_priv));
    OX (exists = OB_TEST_PRIVS(obj_privs, packed_priv));
    if (OB_SUCC(ret) && exists) {
      bool tmp_exist = false;
      OZ (ObPrivPacker::raw_obj_priv_to_packed_info(GRANT_OPTION, priv, packed_priv));
      OX (tmp_exist = OB_TEST_PRIVS(obj_privs, packed_priv));
      OX (option = tmp_exist ? GRANT_OPTION: NO_OPTION);
    }
  }
  return ret;
}

int ObOraPrivCheck::raw_obj_priv_exists(
    const ObRawObjPriv priv,
    const uint64_t option, 
    const ObPackedObjPriv &obj_privs,
    bool &exists)
{
  int ret = OB_SUCCESS;
  exists = false;
  if (priv > 0) {
    ObPrivSet packed_priv;
    OZ (ObPrivPacker::raw_obj_priv_to_packed_info(option, priv, packed_priv));
    OX (exists = OB_TEST_PRIVS(obj_privs, packed_priv));
  }
  return ret;
}



bool ObOraPrivCheck::raw_priv_can_be_granted_to_column(const ObRawObjPriv priv)
{
  return (OBJ_PRIV_ID_INSERT == priv
          || OBJ_PRIV_ID_UPDATE == priv
          || OBJ_PRIV_ID_REFERENCES == priv);
}

int ObPrivPacker::get_total_privs(
    const ObPackedPrivArray &packed_array,
    int &n_cnt)
{
  int ret = OB_SUCCESS;
  int i;
  int j;
  ObRawPriv raw_priv;
  n_cnt = 0;
  for (i = 0; i < packed_array.count() && OB_SUCC(ret); i++) {
    const ObPrivSet &item = packed_array[i];
    if (item > 0) {
      raw_priv  = N_PIRVS_PER_GROUP * i + 1;
      for (j = 0; j < N_PIRVS_PER_GROUP && OB_SUCC(ret); j++) {
        if ((item & NTHID_TO_PACKED(th_in_group_arr[raw_priv])) != 0)
          n_cnt ++;
        OX (raw_priv ++);
      }
    }
  }
  return ret;
}    

int ObPrivPacker::get_total_obj_privs(
    const ObPackedObjPriv &packed_obj_privs,
    int &n_cnt)
{
  int ret = OB_SUCCESS;
  ObRawObjPriv raw_priv;
  bool exists;
  n_cnt = 0;
  if (packed_obj_privs > 0) {
    CK (OBJ_PRIV_ID_MAX <= N_PIRVS_PER_GROUP);
    for (raw_priv = 1; OB_SUCC(ret) && raw_priv <= OBJ_PRIV_ID_MAX; raw_priv ++) {
      OZ (ObOraPrivCheck::raw_obj_priv_exists(raw_priv, packed_obj_privs, exists));
      if (OB_SUCC(ret) && exists) {
        n_cnt ++;
      }
    }
  }
  return ret;
}    

}//end namespace share
}//end namespace oceanbase
