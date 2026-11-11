# The prebuilt ARM64 libsndfile lives in android/deps/stage; the imported
# target is declared by tools/android-imports.cmake so no system search is done.
if(NOT TARGET SndFile::sndfile)
  message(FATAL_ERROR "SndFile::sndfile must be provided by tools/android-imports.cmake")
endif()
set(SndFile_FOUND TRUE)
