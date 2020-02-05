cmake_minimum_required(VERSION 2.8.11)
project(tryplayer)

include(CheckIncludeFile)
include(CheckIncludeFileCXX)
include(CheckIncludeFiles)
include(ExternalProject)

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/Modules/")
set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} " /cygdrive/c/Program\ Files/FFMPEG")
find_package(PkgConfig)
find_package(SDL2)
find_package(SDL2_image)
find_package(SDL2_ttf)
find_package(FLTK)
find_package (Threads)

if (NOT ${SDL2_FOUND})
	ExternalProject_Add(SDL2_PROJ  URL https://www.libsdl.org/release/SDL2-2.0.9.tar.gz
		PREFIX ${CMAKE_SOURCE_DIR}/../SDL2 INSTALL_COMMAND ""  )
	#ExternalProject_Get_Property(SDL2_PROJ
endif()

if (MSYS)
	ExternalProject_Add(ARGP GIT_REPOSITORY https://github.com/alexreg/libargp.git
			PREFIX ${CMAKE_SOURCE_DIR}/../ARGP CONFIGURE_COMMAND "" BUILD_COMMAND "build" INSTALL_COMMAND "" BUILD_IN_SOURCE TRUE)
endif()

if (${CMAKE_SYSTEM} MATCHES  "Linux")
    find_package(X11 REQUIRED)
endif()


find_package(FFMPEG REQUIRED)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_COMPILER_IS_GNUCC)
    ADD_COMPILE_OPTIONS(-std=c++17 -pthread)
endif()

SET(THREADS_PREFER_PTHREAD_FLAG ON)


set(tryplayer_SRC
  src/main.cpp
)

add_executable(tryplayer ${tryplayer_SRC})

add_executable(tryplayerfltk src/mainFltk.cpp)

include_directories(
    ${LIBAVUTIL_INCLUDE_DIRS}
    ${LIBAVCODEC_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIR}
    ${LIBSWSCALE_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${LIBAVFORMAT_INCLUDE_DIRS}
    ${X11_INCLUDE_DIR}
    ${FLTK_INCLUDE_DIR})

target_link_libraries(tryplayer
    ${LIBAVUTIL_LIBRARIES}
    ${LIBAVCODEC_LIBRARIES}
    ${FFMPEG_LIBAVUTIL}
    ${FFMPEG_LIBSWSCALE}
    ${FFMPEG_LIBAVCODEC}
    ${FFMPEG_LIBAVFORMAT}
    ${LIBSWSCALE_LIBRARIES}
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARY}
    ${LIBAVFORMAT_LIBRARIES}
    ${X11_LIBRARIES}
    m)

target_link_libraries(tryplayerfltk
    ${LIBAVUTIL_LIBRARIES}
    ${LIBAVCODEC_LIBRARIES}
    ${FFMPEG_LIBAVUTIL}
    ${FFMPEG_LIBSWSCALE}
    ${FFMPEG_LIBAVCODEC}
    ${FFMPEG_LIBAVFORMAT}
    ${LIBSWSCALE_LIBRARIES}
    ${LIBAVFORMAT_LIBRARIES}
    ${X11_LIBRARIES}
    ${FLTK_LIBRARIES}
    Threads::Threads
    m)

if (${CMAKE_SYSTEM} MATCHES  "CYGWIN")
    target_link_libraries(tryplayer argp)
endif()

# Install the executable
install(TARGETS tryplayer DESTINATION bin)


#-lavutil -lavformat -lavcodec -lz -lavutil -lm
