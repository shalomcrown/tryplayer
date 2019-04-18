cmake_minimum_required(VERSION 2.8.11)
project(tryplayer)


SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/Modules/")
find_package(PkgConfig)
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)

pkg_check_modules(LIBAVCODEC REQUIRED libavcodec)
pkg_check_modules(LIBAVFORMAT REQUIRED libavformat)
pkg_check_modules(LIBAVUTIL REQUIRED libavutil)
pkg_check_modules(LIBSWSCALE REQUIRED libswscale)


# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

# Find the QtWidgets library
find_package(Qt5Widgets CONFIG REQUIRED)

set(tryplayer_SRC
  src/main.cpp
)

# Create code from a list of Qt designer ui files.
#set(CMAKE_AUTOUIC ON) # use this if you have CMake 3.x instead of the following
#qt5_wrap_ui(tryplayer_SRC src/tryplayer.ui)

# Tell CMake to create the helloworld executable
add_executable(tryplayer ${tryplayer_SRC})

include_directories(
    ${LIBAVUTIL_INCLUDE_DIRS}
    ${LIBAVCODEC_INCLUDE_DIRS}
    ${LIBSWSCALE_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${LIBAVFORMAT_INCLUDE_DIRS})

# Use the Widgets module from Qt 5.
target_link_libraries(tryplayer #Qt5::Widgets
    ${LIBAVUTIL_LIBRARIES}
    ${LIBAVCODEC_LIBRARIES}
    ${LIBSWSCALE_LIBRARIES}
    ${SDL2_LIBRARIES}
    ${LIBAVFORMAT_LIBRARIES}
    z m)

# Install the executable
install(TARGETS tryplayer DESTINATION bin)


#-lavutil -lavformat -lavcodec -lz -lavutil -lm
