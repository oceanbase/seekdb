oblib_add_library(restore OBJECT ${SEEKDB_OBLIB_RESTORE_STANDALONE})

target_link_libraries(restore PUBLIC oblib_base)
