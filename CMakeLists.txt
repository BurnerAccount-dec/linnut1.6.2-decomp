cmake_minimum_required(VERSION 3.10)
project(ab_engine C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ab_game src/game/collision_damage.cpp src/game/frame_update.cpp)
target_include_directories(ab_game PUBLIC src)

enable_testing()
add_executable(ab_tests tests/collision_damage_test.cpp)
target_link_libraries(ab_tests ab_game)
add_test(NAME collision_damage COMMAND ab_tests)
add_executable(ab_frame_tests tests/frame_update_test.cpp)
target_link_libraries(ab_frame_tests ab_game)
add_test(NAME frame_update COMMAND ab_frame_tests)

# Lua 5.1 with float numbers (see third_party/lua/luaconf.h)
file(GLOB LUA_SOURCES third_party/lua/*.c)
add_library(lua51 STATIC ${LUA_SOURCES})
target_include_directories(lua51 PUBLIC third_party/lua)

# Box2D 2.1.1 with the exe's constants (third_party/box2d/Box2D/Common/b2Settings.h)
file(GLOB_RECURSE BOX2D_SOURCES third_party/box2d/Box2D/*.cpp)
add_library(box2d STATIC ${BOX2D_SOURCES})
target_include_directories(box2d PUBLIC third_party/box2d)

add_library(ab_physics src/physics/physics_world.cpp)
target_include_directories(ab_physics PUBLIC src)
target_link_libraries(ab_physics PUBLIC box2d ab_game)

add_executable(ab_physics_tests tests/physics_world_test.cpp)
target_link_libraries(ab_physics_tests ab_physics)
add_test(NAME physics_world COMMAND ab_physics_tests)

add_library(ab_audio src/audio/audio_mixer.cpp)
target_include_directories(ab_audio PUBLIC src third_party/minimp3)
if(MSVC)
  target_compile_definitions(ab_audio PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
  target_compile_options(ab_audio PRIVATE /wd4267 /wd4244)   # warnings inside the third-party decoder
endif()
add_library(ab_lua src/lua/aes.cpp src/lua/lua_api.cpp src/lua/script_host.cpp src/lua/lua_frame_host.cpp)
target_include_directories(ab_lua PUBLIC src)
target_link_libraries(ab_lua PUBLIC lua51 ab_game)

add_library(ab_engine src/engine/game_platform.cpp src/engine/sprite_sheet.cpp src/engine/png_decoder.cpp src/engine/font.cpp src/engine/render_soft.cpp)
target_include_directories(ab_engine PUBLIC src)
target_link_libraries(ab_engine PUBLIC ab_lua ab_physics ab_audio)

add_executable(ab_script_boot tests/script_boot_test.cpp)
target_link_libraries(ab_script_boot ab_lua)

add_executable(ab_lua_frame_tests tests/lua_frame_host_test.cpp)
target_link_libraries(ab_lua_frame_tests ab_lua)
add_test(NAME lua_frame_host COMMAND ab_lua_frame_tests)

add_executable(ab_level_test tests/level_test.cpp)
target_link_libraries(ab_level_test ab_engine)

add_executable(ab_sprite_sheet_tests tests/sprite_sheet_test.cpp)
target_link_libraries(ab_sprite_sheet_tests ab_engine)
add_test(NAME sprite_sheet COMMAND ab_sprite_sheet_tests ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(ab_png_tests tests/png_decoder_test.cpp)
target_link_libraries(ab_png_tests ab_engine)

add_executable(ab_render_test tests/render_test.cpp)
target_link_libraries(ab_render_test ab_engine)

# The window: SDL2 + OpenGL 1.x. SDL2 is taken from the system if CMake finds it, otherwise it is
# downloaded and built statically (needs internet on the first configure). -DAB_BUILD_WINDOW=OFF skips it.
option(AB_BUILD_WINDOW "Build the angrybirds window executable (needs SDL2)" ON)
if(AB_BUILD_WINDOW)
  find_package(SDL2 CONFIG QUIET)
  if(NOT SDL2_FOUND)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(sdl2 URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
    FetchContent_MakeAvailable(sdl2)
  endif()
  add_library(ab_input src/engine/input.cpp)
  target_include_directories(ab_input PUBLIC src)
  target_link_libraries(ab_input PUBLIC lua51)
  add_executable(angrybirds src/window/main.cpp src/window/gl_backend.cpp)
  target_link_libraries(angrybirds PRIVATE ab_engine ab_input)
  if(TARGET SDL2::SDL2-static)
    target_link_libraries(angrybirds PRIVATE SDL2::SDL2-static)
  elseif(TARGET SDL2-static)
    target_link_libraries(angrybirds PRIVATE SDL2-static)
  else()
    target_link_libraries(angrybirds PRIVATE SDL2::SDL2)
  endif()
endif()
