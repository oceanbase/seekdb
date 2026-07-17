#package_name: dbms_index_manager
#author: xiebaoma.xbm

CREATE OR REPLACE PACKAGE BODY dbms_index_manager

  PROCEDURE REFRESH();
  PRAGMA INTERFACE(C, REFRESH);

END dbms_index_manager;
