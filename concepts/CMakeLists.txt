add_subdirectory(core)
add_subdirectory(cmrc)
add_subdirectory(client)

if(NOT EMSCRIPTEN)
  add_subdirectory(serialiser)
  add_subdirectory(buffer)
  add_subdirectory(majordomo)
endif()
