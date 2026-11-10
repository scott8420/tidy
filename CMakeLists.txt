cmake_minimum_required(VERSION 3.20)
project(tidy VERSION 0.1.0 LANGUAGES C CXX)

# Tidy -- knows how long each file in your home folder should live, and where
# it belongs; acts only with your say-so, through latch's one door. Started
# from Cairn (s003, the spine); wears Cairn's look.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-4.0)
find_package(spdlog REQUIRED)

# ── latch -- the shared door (scott8420/latch) ───────────────────────────────
# Installed like a system library (latch's ./install.sh -> /usr/local) and
# found here; or, without installing, -DLATCH_DIR=<latch source folder>.
set(LATCH_DIR "" CACHE PATH "latch's source folder (only when it is not installed)")
if(LATCH_DIR)
    add_subdirectory(${LATCH_DIR} ${CMAKE_BINARY_DIR}/latch)
else()
    find_package(latch 0.1 QUIET)
    if(NOT latch_FOUND)
        message(FATAL_ERROR "latch not found -- run ./install.sh in latch's folder, "
                            "or configure with -DLATCH_DIR=<latch source>")
    endif()
endif()

# ── tidy_core -- GTK-free (the model side of the seam) ───────────────────────
# No gtkmm on this link line, by design: a stray UI include under core/ fails
# to compile, and the core stays headless-testable.
add_library(tidy_core STATIC
    src/core/Look.cpp        # Cairn's look: page states, the sidebar as data, tints
    src/core/Pages.cpp       # what each page promises; which milestone brings it
    src/core/Paths.cpp       # where Tidy's records live (resolved, not created)
    src/core/Shortcuts.cpp   # one shortcut list, two consumers (Cairn)
)
target_include_directories(tidy_core PUBLIC include)
target_link_libraries(tidy_core PUBLIC latch::latch)

# ── Headless selftest ─────────────────────────────────────────────────────────
add_executable(tidy_selftest src/selftest.cpp)
target_link_libraries(tidy_selftest PRIVATE tidy_core)
target_compile_definitions(tidy_selftest PRIVATE
    TIDY_LOOK_CSS="${CMAKE_CURRENT_SOURCE_DIR}/resources/look.css"
    TIDY_ICON_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/scalable/apps"
    TIDY_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# ── Resources (gresource): icons + the look's stylesheet, in the binary ──────
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(TIDY_GRESOURCE_XML "${CMAKE_CURRENT_SOURCE_DIR}/resources/tidy.gresource.xml")
set(TIDY_RESOURCES_C   "${CMAKE_CURRENT_BINARY_DIR}/tidy-resources.c")
# CONFIGURE_DEPENDS: an icon added or removed re-globs on the next build.
file(GLOB TIDY_RESOURCE_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/scalable/apps/*.svg"
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.css")
add_custom_command(
    OUTPUT  "${TIDY_RESOURCES_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
            --target "${TIDY_RESOURCES_C}"
            --generate-source
            --c-name tidy_resources
            tidy.gresource.xml
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DEPENDS "${TIDY_GRESOURCE_XML}" ${TIDY_RESOURCE_FILES}
    COMMENT "Compiling gresource bundle (tidy-resources.c)"
    VERBATIM)

# ── tidy -- the GTK app (Shell is a split class across TUs) ──────────────────
add_executable(tidy
    ${TIDY_RESOURCES_C}
    src/main.cpp
    src/App.cpp
    src/Appearance.cpp
    src/Look.cpp
    src/Shell.cpp
    src/Shell_zones.cpp
    src/Shell_bindings.cpp
    src/Shell_handlers.cpp
    src/Shell_helpers.cpp
    src/AboutWindow.cpp
    src/ShortcutsDialog.cpp
    src/Registry.cpp
    src/Log.cpp
)
target_include_directories(tidy PRIVATE include ${GTKMM_INCLUDE_DIRS})
target_link_libraries(tidy     PRIVATE ${GTKMM_LIBRARIES} tidy_core spdlog::spdlog)
target_link_directories(tidy   PRIVATE ${GTKMM_LIBRARY_DIRS})
target_compile_options(tidy    PRIVATE ${GTKMM_CFLAGS_OTHER})
target_compile_definitions(tidy PRIVATE TIDY_VERSION="${PROJECT_VERSION}")
