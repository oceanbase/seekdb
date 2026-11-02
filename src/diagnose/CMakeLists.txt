# Lua diagnose uses Linux-specific APIs (epoll, Unix sockets, process_vm_readv, pthread TSI)
if(NOT WIN32)
  add_subdirectory(lua)
endif()