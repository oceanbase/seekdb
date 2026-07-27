oblib_add_library(restore OBJECT
  ob_io_device.h
  ob_io_device.cpp
  ob_device_common.h
)

target_link_libraries(restore PUBLIC oblib_base)
