add_library(zstd_1_3_8_objs OBJECT ${SEEKDB_OBLIB_ZSTD_STANDALONE})


target_compile_definitions(zstd_1_3_8_objs PRIVATE -DZSTDERRORLIB_VISIBILITY= -DZDICTLIB_VISIBILITY= -DZSTDLIB_VISIBILITY=)
if(NOT WIN32)
  target_compile_options(zstd_1_3_8_objs PRIVATE -fvisibility=hidden)
endif()
target_link_libraries(zstd_1_3_8_objs PUBLIC oblib_base)

if(WIN32)
  # On Windows, use llvm-lib to create a static library from COFF object files
  add_custom_command(
    OUTPUT zstd_1_3_8_objs.lib
    COMMAND llvm-lib /OUT:zstd_1_3_8_objs.lib $<TARGET_OBJECTS:zstd_1_3_8_objs>
    DEPENDS zstd_1_3_8_objs
    COMMAND_EXPAND_LISTS
    )
  oblib_add_extra_objects(${CMAKE_CURRENT_BINARY_DIR}/zstd_1_3_8_objs.lib)
  set_source_files_properties(zstd_1_3_8_objs.lib PROPERTIES GENERATED TRUE)
  oblib_add_library(zstd_1_3_8 zstd_1_3_8_objs.lib)
else()
  # On Unix/Linux/macOS, use ld -r for partial linking
  add_custom_command(
    OUTPUT zstd_1_3_8_objs.o
    COMMAND ${OB_LD_BIN} ${ARCH_ARGS} ${OB_DEPLOY} -r $<TARGET_OBJECTS:zstd_1_3_8_objs> -o zstd_1_3_8_objs.tmp.o
    COMMAND ${OB_OBJCOPY_BIN} --localize-hidden zstd_1_3_8_objs.tmp.o zstd_1_3_8_objs.o
    DEPENDS zstd_1_3_8_objs
    COMMAND_EXPAND_LISTS
    )
  oblib_add_extra_objects(${CMAKE_CURRENT_BINARY_DIR}/zstd_1_3_8_objs.o)
  set_source_files_properties(zstd_1_3_8_objs.o PROPERTIES GENERATED TRUE)
  oblib_add_library(zstd_1_3_8 zstd_1_3_8_objs.o)
endif()

set_target_properties(zstd_1_3_8 PROPERTIES LINKER_LANGUAGE C)
