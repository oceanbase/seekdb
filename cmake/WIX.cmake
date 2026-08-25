# WiX v4 MSI packaging for Windows.
include(cmake/Pack.cmake)

set(CPACK_GENERATOR "WIX")
set(CPACK_WIX_VERSION "4")

# Keep this GUID stable so newer seekdb packages upgrade older installations.
set(CPACK_WIX_UPGRADE_GUID "A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "seekdb")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "SeekDB")
set(CPACK_WIX_ARCHITECTURE "x64")
set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "${OceanBase_HOMEPAGE_URL}")
set(CPACK_WIX_CMAKE_PACKAGE_REGISTRY "SeekDB")

if(EXISTS "${CMAKE_SOURCE_DIR}/tools/windows/installer/LICENSE.rtf")
  set(CPACK_WIX_LICENSE_RTF
    "${CMAKE_SOURCE_DIR}/tools/windows/installer/LICENSE.rtf")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/windows/installer/seekdb.ico")
  set(CPACK_WIX_PRODUCT_ICON
    "${CMAKE_SOURCE_DIR}/tools/windows/installer/seekdb.ico")
endif()

# Expose a checked post-install action on the standard WiX exit dialog.
set(CPACK_WIX_PROPERTY_WIXUI_EXITDIALOGOPTIONALCHECKBOXTEXT
  "Run seekdb Configurator")
set(CPACK_WIX_PROPERTY_WIXUI_EXITDIALOGOPTIONALCHECKBOX "1")

set(CPACK_WIX_PATCH_FILE
  "${CMAKE_SOURCE_DIR}/tools/windows/installer/wix_patch.xml")
set(CPACK_WIX_EXTRA_SOURCES
  "${CMAKE_SOURCE_DIR}/tools/windows/installer/wix_launch_configurator.wxs")

include(CPack)
