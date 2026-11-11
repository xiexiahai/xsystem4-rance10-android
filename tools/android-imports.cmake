# All dependency files are physical copies inside this port.
if(PROJECT_NAME STREQUAL "xsystem4" AND NOT TARGET SDL2::SDL2)
  set(R10_DEPS "${CMAKE_CURRENT_LIST_DIR}/../android/deps")
  add_library(SDL2::SDL2 SHARED IMPORTED)
  set_target_properties(SDL2::SDL2 PROPERTIES
    IMPORTED_LOCATION "${R10_DEPS}/stage/lib/libSDL2.so"
    INTERFACE_INCLUDE_DIRECTORIES "${R10_DEPS}/sdl-config;${R10_DEPS}/sdl-include")
  add_library(Freetype::Freetype STATIC IMPORTED)
  set_target_properties(Freetype::Freetype PROPERTIES
    IMPORTED_LOCATION "${R10_DEPS}/stage/lib/libfreetype.a"
    INTERFACE_INCLUDE_DIRECTORIES "${R10_DEPS}/stage/include/freetype2"
    INTERFACE_LINK_LIBRARIES "${R10_DEPS}/stage/lib/libpng.a;z")
  add_library(cglm SHARED IMPORTED)
  set_target_properties(cglm PROPERTIES
    IMPORTED_LOCATION "${R10_DEPS}/stage/lib/libcglm.so"
    INTERFACE_INCLUDE_DIRECTORIES "${R10_DEPS}/stage/include")
  add_library(SndFile::sndfile STATIC IMPORTED)
  set_target_properties(SndFile::sndfile PROPERTIES
    IMPORTED_LOCATION "${R10_DEPS}/stage/lib/libsndfile.a"
    INTERFACE_INCLUDE_DIRECTORIES "${R10_DEPS}/stage/include"
    INTERFACE_LINK_LIBRARIES "${R10_DEPS}/stage/lib/libogg.a;${R10_DEPS}/stage/lib/libvorbis.a;${R10_DEPS}/stage/lib/libvorbisenc.a;${R10_DEPS}/stage/lib/libvorbisfile.a")
endif()
