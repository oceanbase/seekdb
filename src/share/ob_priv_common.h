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

#ifndef OCEANBASE_SHARE_OB_PRIV_COMMON_
#define OCEANBASE_SHARE_OB_PRIV_COMMON_

#include "lib/container/ob_se_array.h"
#include "share/schema/ob_priv_type.h"

namespace oceanbase
{

namespace share
{

typedef int64_t ObRawPriv;       /* raw sys priv 
                                    value is PRIV_ID_CREATE_SESSION etc 
                                    ob_sys_priv_type.h defined in  */

typedef int64_t ObRawObjPriv;    /* raw obj priv
                                    value is OBJ_PRIV_ID_ALTER etc
                                    ob_obj_priv_type.h defined in */

typedef common::ObSEArray<ObRawPriv, 4> ObRawPrivArray;   // used in grant priv, revoke priv

typedef common::ObSEArray<ObRawObjPriv, 4> ObRawObjPrivArray; // used in grant, revoke objpriv

typedef common::ObSEArray<ObPrivSet, 3> ObPackedPrivArray;  // used in priv schema, packed

typedef ObPrivSet ObPackedObjPriv;  // used in obj priv in priv schema packed


/* database object type definition 
   table, view, pf(procedure/function)
   here we do not use new types, reuse the id_type from ob_max_id_fetch.h,
   for example, user/role type, use OB_MAX_USED_USER_ID_TYPE */

//typedef share::schema::ObObjectType ObDbObjType;

#define NO_OPTION    0
#define ADMIN_OPTION 1              /* used in system privilege */
#define GRANT_OPTION 1              /* used in obj privilege */

#define OBJ_LEVEL_FOR_TAB_PRIV         OB_ALL_MAX_COLUMN_ID   //When table-level privileges are set, colid is set to this value
#define OBJ_LEVEL_FOR_COL_PRIV         (OBJ_LEVEL_FOR_TAB_PRIV + 1) // When column privileges can be checked, colid is set to this
#define ALL_DIR_NAME                "DIRECTORY"
#define OBJ_ID_FOR_DIR              0

class ObPrivPacker {

public:
  static int init_packed_array(
      ObPackedPrivArray &array);

  static int raw_priv_to_packed_info(
      const uint64_t option,
      const ObRawPriv priv, 
      int &group_id,
      ObPrivSet &packed_priv);

  static int raw_obj_priv_to_packed_info(
      const uint64_t option,
      const ObRawObjPriv priv,
      ObPackedObjPriv &packed_priv);
      
  static int pack_raw_priv(
      const uint64_t option,
      const ObRawPriv priv,
      ObPackedPrivArray &packed_array);


  static int pack_raw_obj_priv(
      const uint64_t option,
      const ObRawObjPriv priv,
      ObPackedObjPriv &packed_obj_privs);

  static int pack_raw_obj_priv_list(
      const uint64_t option,
      const ObRawObjPrivArray priv_list,
      ObPackedObjPriv &packed_obj_privs);

  static int append_raw_obj_priv(
      const uint64_t option,
      const ObRawObjPriv priv,
      ObPackedObjPriv &packed_obj_privs);

  static int raw_obj_priv_from_pack(
      const ObPackedObjPriv &packed_obj_privs,
      ObRawObjPrivArray &raw_priv_array
  );

  static int raw_option_obj_priv_from_pack(
      const ObPackedObjPriv &packed_obj_privs,
      ObRawObjPrivArray &raw_priv_array
  );

  static int raw_no_option_obj_priv_from_pack(
      const ObPackedObjPriv &packed_obj_privs,
      ObRawObjPrivArray &raw_priv_array
  );

  static int merge_two_packed_array(
      ObPackedPrivArray &packed_array_1,
      const ObPackedPrivArray &packed_array_2);


  static int get_total_obj_privs(
      const ObPackedObjPriv &packed_obj_privs,
      int &n_cnt);

  static int get_total_privs(
      const ObPackedPrivArray &packed_array,
      int &n_cnt);

private:
  static int has_raw_priv(
      const ObRawPriv raw_priv,
      const ObPrivSet priv_set,
      bool &exists,
      uint64_t &option);

  static int push_back_raw_priv_array(
      ObRawPriv raw_priv,
      bool exists,
      uint64_t option,
      ObRawPrivArray &raw_priv_array,
      ObRawPrivArray &raw_priv_array_with_option);

};

class ObOraPrivCheck {
public:
  
  /* check if rawsyspriv exists */
  static int raw_sys_priv_exists(
      const uint64_t option,
      const ObRawPriv priv,
      const ObPackedPrivArray &packed_array,
      bool &exists);
  
  static int raw_sys_priv_exists(
      const ObRawPriv priv,
      const ObPackedPrivArray &packed_array,
      bool &exists);

  /* whether there is at least one in the plist */


  static int raw_obj_priv_exists(
      const ObRawObjPriv priv,
      const uint64_t option, 
      const ObPackedObjPriv &obj_privs,
      bool &exists);

  static int raw_obj_priv_exists(
      const ObRawObjPriv priv,
      const ObPackedObjPriv &obj_privs,
      bool &exists);

  static int raw_obj_priv_exists_with_info(
      const ObRawObjPriv priv,
      const ObPackedObjPriv &obj_privs,
      bool &exists,
      uint64_t &option);


  static bool user_is_owner(
      const common::ObString &user_name,
      const common::ObString &db_name);

  /* check p1, if not passed, then cond==true and check p2 */

  static bool raw_priv_can_be_granted_to_column(const ObRawObjPriv priv);
};
} // end namespace share
} // end namespace oceanbase

#endif
