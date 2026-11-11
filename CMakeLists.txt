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
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)   # Tidy's own catalog (s004)

# ── latch -- the shared door (scott8420/latch) ───────────────────────────────
# A workspace dependency, NOT part of this repo: cloned beside tidy/ at
# <workspace>/deps/latch, and `git pull --ff-only` at every configure, then
# built from source with Tidy. The pull never discards anything: local edits
# or no network -> a warning, and the build uses the clone as it stands.
#   -DLATCH_PULL=OFF          use deps/latch as it is (no network)
#   -DLATCH_DIR=<folder>      use that latch source folder instead
#   -DLATCH_INSTALLED=ON      use the installed latch (find_package)
get_filename_component(TIDY_WORKSPACE "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(LATCH_GIT_URL "https://github.com/scott8420/latch.git" CACHE STRING "latch's git remote")
set(LATCH_GIT_BRANCH "main" CACHE STRING "latch branch to clone")
set(LATCH_DIR "" CACHE PATH "latch's source folder (default: <workspace>/deps/latch)")
option(LATCH_PULL "git pull deps/latch at configure" ON)
option(LATCH_INSTALLED "use the installed latch (find_package) instead of deps/latch" OFF)

if(NOT LATCH_INSTALLED AND NOT LATCH_DIR)
    find_package(Git REQUIRED)
    set(LATCH_DIR "${TIDY_WORKSPACE}/deps/latch")
    if(NOT EXISTS "${LATCH_DIR}/.git")
        message(STATUS "latch: cloning ${LATCH_GIT_URL} -> ${LATCH_DIR}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" clone --branch "${LATCH_GIT_BRANCH}"
                    "${LATCH_GIT_URL}" "${LATCH_DIR}"
            RESULT_VARIABLE _latch_rc)
        if(NOT _latch_rc EQUAL 0)
            message(FATAL_ERROR "latch: clone failed -- check the network, or use "
                                "-DLATCH_DIR=<folder> / -DLATCH_INSTALLED=ON")
        endif()
    elseif(LATCH_PULL)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${LATCH_DIR}" pull --ff-only
            RESULT_VARIABLE _latch_rc
            OUTPUT_VARIABLE _latch_out ERROR_VARIABLE _latch_out
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
        if(_latch_rc EQUAL 0)
            message(STATUS "latch: git pull -- ${_latch_out}")
        else()
            message(WARNING "latch: git pull failed; building deps/latch as it stands.\n${_latch_out}")
        endif()
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${LATCH_DIR}" log -1 --format=%h\ %s
                    OUTPUT_VARIABLE _latch_head OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "latch: ${LATCH_DIR} @ ${_latch_head}")
endif()

if(LATCH_DIR AND NOT LATCH_INSTALLED)
    # EXCLUDE_FROM_ALL: latch's own install() rules stay out of Tidy's.
    add_subdirectory(${LATCH_DIR} ${CMAKE_BINARY_DIR}/latch EXCLUDE_FROM_ALL)
else()
    find_package(latch 0.2 QUIET)   # 0.2: the Tag verb
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
    src/core/Facts.cpp       # statx times + user.tidy.* tags, read (s004)
    src/core/Scan.cpp        # walk folders, read-only: never Protected, packages whole (s004)
    src/core/Catalog.cpp     # Tidy's own SQLite record -- the ONE source that writes SQLite (s004)
    src/core/Tagging.cpp     # stamp / reconcile as door plans; the catalog kept in step (s004)
    src/core/Retention.cpp   # the seven retention levels, as data (s005)
    src/core/Birth.cpp       # birth rules: landing places -> proposed levels, REPORT ONLY (s005)
    src/core/Closet.cpp      # the Closet: view + accept / decide / Junk Drawer as door plans (s006)
    src/core/Settings.cpp    # what the user sets: aging days, the junk list (s006a)
    src/core/Own.cpp         # the ONE writer of Tidy's own files -- gate-listed (s006a)
    src/core/Purposes.cpp    # purposes + the learned hints; assign / close as tag plans (s009)
    src/core/Projects.cpp    # projects: found, categorized; place / fix as door plans (s008)
    src/core/Templates.cpp   # templates: the tree, roles, routes; lift = proposal; drift; builder undo (s007)
    src/core/Starters.cpp    # the built-in templates: the creative base + its kinds (s010a)
    src/core/FileLists.cpp   # CVS-style .tidy-files per folder: write, re-find lost ids (s010f2)
    src/core/Rescan.cpp      # what a look tells the catalog: moved by id, gone, copies (s010f1)
    src/core/Due.cpp         # the trigger: what is due; the headless look; attention.ini (s012a)
    src/core/Background.cpp  # the systemd user timer: units as door plans; systemctl lines (s012b)
    src/core/Sha256.cpp      # SHA-256, no library: the archive's fingerprint (s013a)
    src/core/Help.cpp        # "How to use Tidy": the process, step by step (s017)
    src/core/Removal.cpp     # removal: to the Trash, archived ones only when proved NOW on media (s014)
    src/core/Backup.cpp      # the backup: copies of ~/Archive on media, read back + proved (s013b)
    src/core/Archive.cpp     # the archive: ~/Archive/<year>/<its folder>; tag + move plans; fingerprints (s013a)
    src/core/Homing.cpp      # files home: by type (subfolder required) or into a project; "name (2)" (s011)
)
target_include_directories(tidy_core PUBLIC include)
target_link_libraries(tidy_core PUBLIC latch::latch PkgConfig::SQLITE)

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
    src/SettingsWindow.cpp
    src/IdListsWindow.cpp
    src/TemplatesPage.cpp
    src/ProjectsPage.cpp
    src/PurposesPage.cpp
    src/HomePage.cpp
    src/DuePage.cpp
    src/ArchivePage.cpp
    src/HelpWindow.cpp
    src/BackgroundWindow.cpp
    src/ShortcutsDialog.cpp
    src/Registry.cpp
    src/Log.cpp
)
target_include_directories(tidy PRIVATE include ${GTKMM_INCLUDE_DIRS})
target_link_libraries(tidy     PRIVATE ${GTKMM_LIBRARIES} tidy_core spdlog::spdlog)
target_link_directories(tidy   PRIVATE ${GTKMM_LIBRARY_DIRS})
target_compile_options(tidy    PRIVATE ${GTKMM_CFLAGS_OTHER})
target_compile_definitions(tidy PRIVATE TIDY_VERSION="${PROJECT_VERSION}")
