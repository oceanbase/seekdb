# Pure SQL packages have no native target. Keep their delivery reachable from
# the top-level install, independently of optional plugin binaries excluded
# from ALL. In particular this must not force GIS/model DSOs into a core build.
include(GNUInstallDirs)
install(FILES
  ../gis/sql/gis.control
  ../gis/sql/gis--1.0.sql
  DESTINATION "${CMAKE_INSTALL_DATADIR}/seekdb/extension"
  COMPONENT plugins)
install(FILES
  native_math/native_math.control
  native_math/native_math--1.0.sql
  native_math/native_math--1.0--1.1.sql
  native_math/native_math--1.1--1.2.sql
  DESTINATION "${CMAKE_INSTALL_DATADIR}/seekdb/extension"
  COMPONENT plugins)
install(FILES
  text_ops/text_ops.control
  text_ops/text_ops--1.0.sql
  text_ops/text_ops--1.0--1.1.sql
  DESTINATION "${CMAKE_INSTALL_DATADIR}/seekdb/extension"
  COMPONENT plugins)

# An Extension may compose another installed Extension without a native DSO.
install(FILES
  text_composed/text_composed.control
  text_composed/text_composed--1.0.sql
  text_composed/text_composed--1.0--1.1.sql
  DESTINATION "${CMAKE_INSTALL_DATADIR}/seekdb/extension"
  COMPONENT plugins)

# SQL wrappers can declare an already installed native module without pulling
# that optional binary into the core build or loading it during installation.
install(FILES
  rust_text_ops/rust_text_ops.control
  rust_text_ops/rust_text_ops--1.0.sql
  rust_text_ops/rust_text_ops--1.0--1.1.sql
  DESTINATION "${CMAKE_INSTALL_DATADIR}/seekdb/extension"
  COMPONENT plugins)
install(FILES
  rust_text_native/rust_text_native.control
  rust_text_native/rust_text_native--1.0--1.1.sql
  DESTINATION "${CMAKE_INSTALL_DATADIR}/seekdb/extension"
  COMPONENT plugins)
install(FILES
  rust_text_built/rust_text_built.control
  DESTINATION "${CMAKE_INSTALL_DATADIR}/seekdb/extension"
  COMPONENT plugins)
