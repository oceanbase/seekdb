oblib_add_library(restore OBJECT
  ob_fd_simulator.h
  ob_fd_simulator.cpp
  ob_io_device.h
  ob_io_device.cpp
  ob_device_common.h
  ob_i_storage.h
  ob_i_storage.cpp
  ob_storage.cpp
  ob_storage_file.cpp
  ob_storage_file.h
  ob_storage.h
  ob_storage_path.cpp
  ob_storage_path.h
  ob_storage_info.cpp
  ob_storage_info.h
  ob_object_device.cpp
  ob_object_device.h
  ob_object_storage_base.cpp
)

target_link_libraries(restore PUBLIC oblib_base)
