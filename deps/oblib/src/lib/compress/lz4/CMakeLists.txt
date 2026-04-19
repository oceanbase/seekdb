set(LZ4_LOCAL_LIBS)
set(LZ4_LOCAL_LIB_OBJS)

macro(gen_local_lib NAME)
  get_filename_component(TGT ${NAME} NAME_WE)
  add_library(${TGT} OBJECT ${ARGN})
  if(NOT WIN32)
    target_compile_options(${TGT} PRIVATE -fvisibility=hidden)
  endif()
  target_link_libraries(${TGT} PUBLIC oblib_base)
  target_compile_definitions(${TGT} PRIVATE LZ4LIB_VISIBILITY=)

  if(WIN32)
    # On Windows, convert .a to .lib and use llvm-lib
    string(REPLACE ".a" ".lib" ACTUAL_NAME "${NAME}")
    add_custom_command(
      OUTPUT ${ACTUAL_NAME}
      COMMAND llvm-lib /OUT:${ACTUAL_NAME} $<TARGET_OBJECTS:${TGT}>
      DEPENDS ${TGT}
      DEPENDS $<TARGET_OBJECTS:${TGT}>
      COMMAND_EXPAND_LISTS
    )
    list(APPEND LZ4_LOCAL_LIBS ${ACTUAL_NAME})
  else()
    # On Unix/Linux/macOS, use ld to create relocatable object
    add_custom_command(
      OUTPUT ${NAME}
      COMMAND ${OB_LD_BIN} ${ARCH_ARGS} ${OB_DEPLOY} -r $<TARGET_OBJECTS:${TGT}> -o ${NAME}
      COMMAND ${OB_OBJCOPY_BIN} --localize-hidden ${NAME}
      DEPENDS ${TGT}
      DEPENDS $<TARGET_OBJECTS:${TGT}>
      COMMAND_EXPAND_LISTS
    )
    list(APPEND LZ4_LOCAL_LIBS ${NAME})
  endif()
  list(APPEND LZ4_LOCAL_LIB_OBJS \$<TARGET_OBJECTS:${TGT}>)
endmacro()

gen_local_lib(lz4_171.a
  lz4_src/lz4_171.h
  lz4_src/lz4_171.c
  lz4_wrapper.h
  lz4_171_wrapper.cpp
)

gen_local_lib(lz4_191.a
  lz4_src/lz4_191.h
  lz4_src/lz4_191.c
  lz4_src/lz4hc_191.h
  lz4_src/lz4hc_191.c
  lz4_wrapper.h
  lz4_191_wrapper.cpp
  )

add_library(lz4_objs OBJECT
  ob_lz4_compressor.cpp
  ob_lz4_compressor.h
  ob_lz4_compressor_191.cpp
  ob_lz4_stream_compressor.h
  ob_lz4_stream_compressor.cpp
  ob_lz4_stream_compressor_191.cpp)
target_link_libraries(lz4_objs PUBLIC oblib_base)

if(WIN32)
  # On Windows, use llvm-lib to merge all libraries and objects
  add_custom_command(
    OUTPUT lz4-all.lib
    COMMAND llvm-lib /OUT:lz4-all.lib ${LZ4_LOCAL_LIBS} $<TARGET_OBJECTS:lz4_objs>
    DEPENDS ${LZ4_LOCAL_LIBS} lz4_objs
    DEPENDS ${LZ4_LOCAL_LIB_OBJS} $<TARGET_OBJECTS:lz4_objs>
    COMMAND_EXPAND_LISTS)
  oblib_add_library(lz4 lz4-all.lib)
  oblib_add_extra_objects(${CMAKE_CURRENT_BINARY_DIR}/lz4-all.lib)
else()
  # On Unix/Linux/macOS, use ld -r for partial linking
  add_custom_command(
    OUTPUT lz4-all.a.o
    COMMAND ${OB_LD_BIN} ${ARCH_ARGS} ${OB_DEPLOY} -r ${LZ4_LOCAL_LIBS} $<TARGET_OBJECTS:lz4_objs> -o lz4-all.a.o
    DEPENDS ${LZ4_LOCAL_LIBS} lz4_objs
    DEPENDS ${LZ4_LOCAL_LIB_OBJS} $<TARGET_OBJECTS:lz4_objs>
    COMMAND_EXPAND_LISTS)
  oblib_add_library(lz4 lz4-all.a.o)
  oblib_add_extra_objects(${CMAKE_CURRENT_BINARY_DIR}/lz4-all.a.o)
endif()

set_target_properties(lz4 PROPERTIES LINKER_LANGUAGE C)
