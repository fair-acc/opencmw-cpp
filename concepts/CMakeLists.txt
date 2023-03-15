add_subdirectory(core)
add_subdirectory(cmrc)
add_subdirectory(client)

if(NOT USE_LIBCPP)
    add_subdirectory(serialiser)
    add_subdirectory(disruptor)
    add_subdirectory(majordomo)
endif()
