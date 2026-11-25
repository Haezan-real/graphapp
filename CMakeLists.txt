cmake_minimum_required(VERSION 3.24)

project(graphapp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../graphapp-deps/visage/CMakeLists.txt"
    AND NOT EXISTS "C:/Users/zeela/graphapp-deps/visage/CMakeLists.txt")
  message(FATAL_ERROR "Visage was not found. Clone https://github.com/VitalAudio/visage into C:/Users/zeela/graphapp-deps/visage")
endif ()

set(VISAGE_SOURCE "C:/Users/zeela/graphapp-deps/visage")
set(VISAGE_APPLICATION_NAME "graphapp" CACHE STRING "" FORCE)
set(VISAGE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VISAGE_BUILD_TESTS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(visage SOURCE_DIR "${VISAGE_SOURCE}")
FetchContent_MakeAvailable(visage)

find_package(Qt6 REQUIRED COMPONENTS
  Widgets
  OpenGL
  OpenGLWidgets
  Multimedia
  WebEngineWidgets
  WebChannel
  Svg
)

qt_add_executable(graphapp
  src/main.cpp
  src/expr.h
  src/expr.cpp
  src/audio_engine.h
  src/audio_engine.cpp
  src/title_bar.h
  src/title_bar.cpp
  src/function_card.h
  src/function_card.cpp
  src/graph_widget.h
  src/graph_widget.cpp
  src/main_window.h
  src/main_window.cpp
)

target_include_directories(graphapp PRIVATE src)

target_link_libraries(graphapp PRIVATE
  Qt6::Widgets
  Qt6::OpenGL
  Qt6::OpenGLWidgets
  Qt6::Multimedia
  Qt6::WebEngineWidgets
  Qt6::WebChannel
  Qt6::Svg
  visage
)

if (WIN32)
  target_link_libraries(graphapp PRIVATE d3d11 dxgi d3dcompiler d2d1 dwrite windowscodecs dwmapi)
  target_compile_definitions(graphapp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_compile_options(graphapp PRIVATE /utf-8 /W3)
endif ()

add_custom_command(TARGET graphapp POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/web"
          "$<TARGET_FILE_DIR:graphapp>/web"
  COMMENT "Copying KaTeX card page"
)

set_target_properties(graphapp PROPERTIES OUTPUT_NAME "graphapp" WIN32_EXECUTABLE TRUE)
if (WIN32)
  target_sources(graphapp PRIVATE resources/graphapp.manifest)
endif ()
