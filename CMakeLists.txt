cmake_minimum_required(VERSION 3.20)
project(pixel_game LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)

FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(glfw stb nlohmann_json)

# Local glad (generated loader)
add_library(glad STATIC
  3rd/glad/src/gl.c
)
target_include_directories(glad PUBLIC
  3rd/glad/include
)

find_package(OpenGL REQUIRED)

add_executable(pixel_game
  src/main.cpp
  src/office.cpp
  src/person.cpp
  src/journal.cpp
  src/stb_image_impl.cpp
)

target_include_directories(pixel_game PRIVATE
  ${stb_SOURCE_DIR}
)

target_link_libraries(pixel_game PRIVATE
  glfw
  glad
  nlohmann_json::nlohmann_json
  OpenGL::GL
)

if(APPLE)
  target_compile_definitions(pixel_game PRIVATE GL_SILENCE_DEPRECATION)
endif()
