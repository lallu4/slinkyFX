cmake_minimum_required(VERSION 3.20)

# When this directory is configured standalone (Windows engine build), compute
# the project root so the bundled LLVM toolchain can be found.  When included
# from the top-level CMakeLists.txt (Linux payload build), the top-level has
# already selected the compilers and this path is simply unused.
get_filename_component(SLINKY_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
set(SLINKY_LLVM_DIR "${SLINKY_PROJECT_ROOT}/tools/llvm/clang+llvm-23.1.4-x86_64-pc-windows-msvc" CACHE PATH "Bundled LLVM")

project(SlinkyNative LANGUAGES C CXX)

# ------------------------------------------------------------------
# Linux: legacy JNI/JVMTI payload library with embedded Java jars.
# ------------------------------------------------------------------
if(NOT WIN32)
    find_package(Threads REQUIRED)

    set(EMBED_DIR ${CMAKE_CURRENT_BINARY_DIR}/embed)
    file(MAKE_DIRECTORY ${EMBED_DIR})
    set(EMBED_OBJS "")
    # No resource 104: the detector maps intermediary onto the official jar (103).
    foreach(pair "101:slinky-named" "102:slinky-srg" "103:slinky-official")
        string(REPLACE ":" ";" parts "${pair}")
        list(GET parts 0 rid)
        list(GET parts 1 rname)
        set(jar ${CMAKE_CURRENT_SOURCE_DIR}/payload/${rname}.jar)
        set(bin ${EMBED_DIR}/res${rid}.bin)
        set(obj ${EMBED_DIR}/res${rid}.o)
        add_custom_command(
            OUTPUT ${obj}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${EMBED_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${jar} ${bin}
            COMMAND ${CMAKE_OBJCOPY} -I binary -O elf64-x86-64 -B i386:x86-64 res${rid}.bin res${rid}.o
            WORKING_DIRECTORY ${EMBED_DIR}
            DEPENDS ${jar}
            COMMENT "Embedding payload ${rname}.jar as resource ${rid}"
            VERBATIM)
        list(APPEND EMBED_OBJS ${obj})
    endforeach()

    add_library(SlinkyNative SHARED
        src/SoMain.cpp
        src/Logger.cpp
        src/JvmManager.cpp
        src/MinecraftDetector.cpp
        src/NativeBridge.cpp
        src/PayloadLoader.cpp
        ${EMBED_OBJS}
    )

    target_include_directories(SlinkyNative PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/jdk
        ${CMAKE_CURRENT_SOURCE_DIR}/include/jdk/linux
        ${CMAKE_CURRENT_SOURCE_DIR}/include/w32
    )
    target_compile_definitions(SlinkyNative PRIVATE EMBED_PAYLOAD_JARS)
    target_link_libraries(SlinkyNative PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

    set_target_properties(SlinkyNative PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        OUTPUT_NAME "slinky-native"
    )
    return()
endif()

# ------------------------------------------------------------------
# Windows: slinkyFX native engine. Builds as slinky-native.dll and is
# injected by the loader directly into the Minecraft JVM.
# ------------------------------------------------------------------

# clang-cl + lld-link without a Windows SDK: CMake still wants a manifest tool
# for the DLL link step. LLVM ships llvm-mt, which takes the same arguments as
# mt.exe; without it the build fails after all objects are compiled.
if(WIN32 AND NOT CMAKE_MT)
    find_program(SLINKY_MT NAMES llvm-mt
        PATHS "${SLINKY_LLVM_DIR}/bin" NO_DEFAULT_PATH)
    if(SLINKY_MT)
        set(CMAKE_MT "${SLINKY_MT}")
    endif()
endif()

set(CMAKE_CXX_FLAGS "" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS "" CACHE STRING "" FORCE)

# ---- sources ----
file(GLOB_RECURSE ENGINE_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/*.cpp)
file(GLOB_RECURSE LOGIC_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/logic/*.cpp)
file(GLOB_RECURSE MODULE_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/*.cpp)
file(GLOB PLATFORM_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win/*.cpp)
# stack probe for large frames (clang-cl would take it from the C runtime library)
set(CHKSTK_OBJ ${CMAKE_CURRENT_BINARY_DIR}/chkstk.obj)
# NanoVG core (+ fontstash, stb_truetype, stb_image) and its GL3 backend
set(NANOVG_SRC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanovg/src/nanovg.c ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/nanovg_impl.c)

# ---- the original's data, assembled (GNU syntax) ----
# Prefer the bundled LLVM toolchain, but fall back to a system clang.
find_program(SLINKY_CLANG clang
    PATHS "${SLINKY_LLVM_DIR}/bin" NO_DEFAULT_PATH)
if(NOT SLINKY_CLANG)
    find_program(SLINKY_CLANG clang REQUIRED)
endif()

find_program(SLINKY_DLLTOOL NAMES llvm-dlltool
    PATHS "${SLINKY_LLVM_DIR}/bin" NO_DEFAULT_PATH)
if(NOT SLINKY_DLLTOOL)
    find_program(SLINKY_DLLTOOL NAMES llvm-dlltool REQUIRED)
endif()

set(DATA_OBJ)
foreach(part rdata data iat)
    set(obj ${CMAKE_CURRENT_BINARY_DIR}/data_${part}.obj)
    add_custom_command(OUTPUT ${obj}
        COMMAND ${SLINKY_CLANG} --target=x86_64-pc-windows-msvc -c ${CMAKE_CURRENT_SOURCE_DIR}/src/data/${part}.s -o ${obj}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/data/${part}.s VERBATIM)
    list(APPEND DATA_OBJ ${obj})
endforeach()

add_custom_command(OUTPUT ${CHKSTK_OBJ}
    COMMAND ${SLINKY_CLANG} --target=x86_64-pc-windows-msvc -c ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win/chkstk.s -o ${CHKSTK_OBJ}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win/chkstk.s VERBATIM)
list(APPEND DATA_OBJ ${CHKSTK_OBJ})

# ---- import libraries from the .def files (no Windows SDK needed) ----
set(IMPORT_LIBS)
foreach(dll kernel32 user32 shell32 ucrtbase)
    set(lib ${CMAKE_CURRENT_BINARY_DIR}/imports/${dll}.lib)
    add_custom_command(OUTPUT ${lib}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/imports
        COMMAND ${SLINKY_DLLTOOL} -m i386:x86-64 -d ${CMAKE_CURRENT_SOURCE_DIR}/cmake/imports/${dll}.def -l ${lib}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/imports/${dll}.def VERBATIM)
    list(APPEND IMPORT_LIBS ${lib})
endforeach()

add_custom_target(slinky_imports DEPENDS ${IMPORT_LIBS})
add_library(slinky SHARED ${ENGINE_SRC} ${LOGIC_SRC} ${MODULE_SRC} ${PLATFORM_SRC} ${NANOVG_SRC} ${DATA_OBJ})
add_dependencies(slinky slinky_imports)
target_link_libraries(slinky PRIVATE ${IMPORT_LIBS})
set_target_properties(slinky PROPERTIES PREFIX "" LINKER_LANGUAGE CXX OUTPUT_NAME "slinky-native")
set_source_files_properties(${DATA_OBJ} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

# The code generation is the one the sources were verified against (tools/asmdiff.py CFLAGS): same optimiser
# settings, frame pointers, no stack cookies, no COMDAT per function, the old compiler's inc/dec and nop choices.
target_compile_options(slinky PRIVATE
    -O2 -GS- /Gy-
    "SHELL:-Xclang -mframe-pointer=all"
    "SHELL:-Xclang -target-feature -Xclang +slow-incdec"
    "SHELL:-Xclang -target-feature -Xclang -fast-11bytenop"
    "SHELL:-Xclang -target-feature -Xclang -fast-15bytenop"
    -w)
target_include_directories(slinky PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanovg/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/gl)
# the bundled C libraries (NanoVG, stb) get a minimal libc declaration set instead of the Windows SDK's headers
set_source_files_properties(${NANOVG_SRC} PROPERTIES COMPILE_OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}/third_party/minicrt/include")
# glcorearb.h would include <windows.h> (not available in the cross build) just to define APIENTRY
target_compile_definitions(slinky PRIVATE APIENTRY=)

# some files need extra flags (`// cflags: ...` in their first lines): the compiler version that built the
# original lowered a few constructs differently
foreach(f ${ENGINE_SRC} ${LOGIC_SRC} ${MODULE_SRC})
    file(STRINGS ${f} line LIMIT_COUNT 1 REGEX "^// cflags:")
    if(line)
        string(REGEX REPLACE "^// cflags:[ ]*" "" extra "${line}")
        separate_arguments(extra_list NATIVE_COMMAND "${extra}")
        set_source_files_properties(${f} PROPERTIES COMPILE_OPTIONS "${extra_list}")
    endif()
endforeach()

# Reached from the DLL's own entry point; no C runtime start-up code is linked (the UCRT in the process is used)
target_link_options(slinky PRIVATE /NODEFAULTLIB /ENTRY:DllMain /SUBSYSTEM:WINDOWS /OPT:NOREF /OPT:NOICF /ERRORLIMIT:0 /MAP)
