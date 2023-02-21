add_subdirectory(core)

add_subdirectory(cmrc)
if(NOT USE_LIBCPP)
    add_subdirectory(serialiser)
    add_subdirectory(disruptor)
    add_subdirectory(majordomo)
    add_subdirectory(client)
endif()
