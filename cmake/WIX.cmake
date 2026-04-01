# WiX MSI packaging configuration for Windows
# Mirrors cmake/Pack.cmake but adapted for Windows directory layout

include(cmake/Pack.cmake)

set(CPACK_GENERATOR "WIX")
set(CPACK_WIX_VERSION "4")

# Fixed GUIDs for upgrade consistency across versions
set(CPACK_WIX_UPGRADE_GUID "A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D")

set(CPACK_PACKAGE_INSTALL_DIRECTORY "seekdb")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "SeekDB")
set(CPACK_WIX_ARCHITECTURE "x64")

set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "https://github.com/oceanbase/oceanbase")

if(EXISTS "${CMAKE_SOURCE_DIR}/tools/windows/installer/LICENSE.rtf")
  set(CPACK_WIX_LICENSE_RTF "${CMAKE_SOURCE_DIR}/tools/windows/installer/LICENSE.rtf")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/tools/windows/installer/seekdb.ico")
  set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/tools/windows/installer/seekdb.ico")
endif()

# Add bin/ to system PATH
set(CPACK_WIX_CMAKE_PACKAGE_REGISTRY "SeekDB")

include(CPack)
