ob_set_subtarget(ob_diagnose_lua common
  ob_lua_api.cpp
  ob_lua_handler.cpp
)

ob_server_add_target(ob_diagnose_lua)

if(WIN32)
  set(VCPKG_LIB_DIR "${OB_VCPKG_DIR}/lib")
  target_link_libraries(ob_diagnose_lua INTERFACE
    ${VCPKG_LIB_DIR}/lua.lib)
else()
  target_link_libraries(ob_diagnose_lua INTERFACE
    ${DEP_DIR}/lib/liblua.a)
endif()