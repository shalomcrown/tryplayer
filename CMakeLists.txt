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
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf)

if (NOT SDL2_INCLUDE_DIRS)
	ExternalProject_Add(SDL2  URL https://www.libsdl.org/release/SDL2-2.0.9.tar.gz 
		PREFIX ${CMAKE_SOURCE_DIR}/../SDL2 INSTALL_COMMAND "")
endif()

if (MSYS)
	ExternalProject_Add(ARGP GIT_REPOSITORY https://github.com/alexreg/libargp.git 
			PREFIX ${CMAKE_SOURCE_DIR}/../ARGP CONFIGURE_COMMAND "" BUILD_COMMAND "build" INSTALL_COMMAND "" BUILD_IN_SOURCE TRUE)
endif()

if (${CMAKE_SYSTEM} MATCHES  "Linux")
    find_package(X11 REQUIRED)
endif()

#pkg_check_modules(LIBAVCODEC REQUIRED libavcodec)
#pkg_check_modules(LIBAVFORMAT REQUIRED libavformat)
#pkg_check_modules(LIBAVUTIL REQUIRED libavutil)
#pkg_check_modules(LIBSWSCALE REQUIRED libswscale)

find_package(FFMPEG REQUIRED)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(tryplayer_SRC
  src/main.cpp
)

add_executable(tryplayer ${tryplayer_SRC})

include_directories(
    ${LIBAVUTIL_INCLUDE_DIRS}
    ${LIBAVCODEC_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIR}
    ${LIBSWSCALE_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${LIBAVFORMAT_INCLUDE_DIRS}
    ${X11_INCLUDE_DIR})

# Use the Widgets module from Qt 5.
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

if (${CMAKE_SYSTEM} MATCHES  "CYGWIN")
    target_link_libraries(tryplayer argp)
endif()
    
# Install the executable
install(TARGETS tryplayer DESTINATION bin)


#-lavutil -lavformat -lavcodec -lz -lavutil -lm
