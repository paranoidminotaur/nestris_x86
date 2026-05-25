cmake_minimum_required(VERSION 3.10)
project(nestris_x86)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
endif(MSVC)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

INCLUDE_DIRECTORIES(olcPixelGameEngine)
INCLUDE_DIRECTORIES(olcPGEX_Gamepad)
INCLUDE_DIRECTORIES(include)

if(WIN32)
    cmake_policy(SET CMP0015 NEW)

    INCLUDE_DIRECTORIES(SDL-win32/include)
    LINK_DIRECTORIES(SDL-win32/lib)

    SET(TETRIS_LIBS SDL SDL_mixer)

else()
    FIND_PACKAGE(PNG REQUIRED)
    FIND_PACKAGE(OpenGL REQUIRED)

    #MacOS
    #brew install libpng sdl2 sdl2_mixer yaml-cpp
    if(APPLE)
        MESSAGE("Building for MacOS...")

        find_package(SDL2 REQUIRED)
        find_package(SDL2_mixer REQUIRED)
        find_package(yaml-cpp REQUIRED)

        SET(TETRIS_LIBS SDL2_mixer::SDL2_mixer)

        SET(OLC_LIBS
                SDL2::SDL2
                PNG::PNG
                yaml-cpp::yaml-cpp
                "-framework OpenGL"
            )

    #LINUX
    #sudo apt install libpng-dev libsdl2-dev libsdl2-mixer-dev libyaml-cpp-dev libgl-dev
    else()
        MESSAGE("Building for linux...")

        find_package(SDL2 REQUIRED)
        find_path(SDL_MIXER_INCLUDE_DIR SDL_mixer.h PATH_SUFFIXES SDL2)
        find_library(SDL_MIXER_LIBRARY NAMES SDL2_mixer)

        if(NOT SDL_MIXER_LIBRARY)
            message(FATAL_ERROR "SDL2_mixer not found. Install: sudo apt install libsdl2-mixer-dev")
        endif()

        INCLUDE_DIRECTORIES(${SDL_MIXER_INCLUDE_DIR})

        SET(TETRIS_LIBS ${SDL_MIXER_LIBRARY})

        SET(OLC_LIBS
            SDL2::SDL2
            X11
            pthread
            PNG::PNG
            stdc++fs
            yaml-cpp
            ${OPENGL_LIBRARIES}
            )
    endif()
endif()

SET(DATA_ENCODING_SOURCES
  src/data_encoders/data_encoder_factory.cpp
  src/data_encoders/data_to_string_encoder.cpp
  src/data_encoders/olc_sprite_encoder.cpp
  src/data_encoders/sdl_mix_chunk_encoder.cpp
)

add_library(olc src/olc/olcPixelGameEngine.cpp)
target_link_libraries(olc ${OLC_LIBS})

add_library(assets_lib src/assets_cpp/images.cpp src/assets_cpp/sounds.cpp)

add_executable(nestris_x86
        ${DATA_ENCODING_SOURCES}
        src/assets.cpp
        src/drawing_utils.cpp
        src/drawers/olc_drawer.cpp
        src/frame_processors/game_processor.cpp
        src/frame_processors/level_screen_processor.cpp
        src/frame_processors/option_screen_processor.cpp
        src/frame_processors/keyboard_config_processor.cpp
        #src/frame_processors/gamepad_config_processor.cpp
        src/game_logic.cpp
        src/game_renderer.cpp
        src/input_devices/olc_keyboard.cpp
        src/input_devices/sdl_gamepad.cpp
        src/main.cpp
        src/sound.cpp
        src/statistics.cpp
        src/nestris_x86.cpp
        src/tetromino_rng.cpp
        )

target_link_libraries(nestris_x86 olc assets_lib ${TETRIS_LIBS})

add_executable(asset_cpp_gen src/tools/asset_cpp_gen.cpp ${DATA_ENCODING_SOURCES})
target_link_libraries(asset_cpp_gen olc ${TETRIS_LIBS})

if(NOT APPLE)
  add_executable(sdl_gamepad src/tools/sdl_gamepad.cpp)
  target_link_libraries(sdl_gamepad ${TETRIS_LIBS})
endif()

if(NOT APPLE)
  add_executable(testbed src/tools/button_tester.cpp)
  target_link_libraries(testbed olc ${OLC_LIBS})
endif()
