cmake_minimum_required(VERSION 3.16)
project(rossat-swarm VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(ROSSAT_BUILD_GUI "Build the GTK3 GUI" ON)
option(ROSSAT_BUILD_CLI "Build the command line tool" ON)
option(ROSSAT_BUILD_TESTS "Build offline unit tests" ON)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw>=0.14)

# ---- library -------------------------------------------------------------
find_package(Threads REQUIRED)
add_library(rossat SHARED
  src/lib/rossat.cpp
  src/lib/names.cpp
  src/lib/engine.cpp
  src/lib/config.cpp)
target_include_directories(rossat PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(rossat PRIVATE ROSSAT_BUILDING_SHARED)
target_compile_options(rossat PRIVATE -Wall -Wextra)
set_target_properties(rossat PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
target_link_libraries(rossat PRIVATE PkgConfig::HIDAPI Threads::Threads)

install(TARGETS rossat EXPORT rossatTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/rossat DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT rossatTargets NAMESPACE rossat:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rossat)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/rossatConfig.cmake
  "include(\"\${CMAKE_CURRENT_LIST_DIR}/rossatTargets.cmake\")\n")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rossatConfig.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rossat)
configure_file(data/rossat.pc.in rossat.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rossat.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
install(FILES data/70-rossat.rules DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d)
configure_file(data/rossat-engine.service.in rossat-engine.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rossat-engine.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)

# ---- CLI -----------------------------------------------------------------
if(ROSSAT_BUILD_CLI)
  add_executable(rossat-cli src/cli/main.cpp)
  target_link_libraries(rossat-cli PRIVATE rossat)
  target_compile_options(rossat-cli PRIVATE -Wall -Wextra)
  install(TARGETS rossat-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ---- GUI -----------------------------------------------------------------
if(ROSSAT_BUILD_GUI)
  pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
  find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
  set(GRESOURCE_XML ${CMAKE_CURRENT_SOURCE_DIR}/data/rossat.gresource.xml)
  set(GRESOURCE_C ${CMAKE_CURRENT_BINARY_DIR}/rossat-resources.c)
  add_custom_command(
    OUTPUT ${GRESOURCE_C}
    COMMAND ${GLIB_COMPILE_RESOURCES} --target=${GRESOURCE_C} --generate-source
            --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/data ${GRESOURCE_XML}
    DEPENDS ${GRESOURCE_XML} data/rossat.css data/rossat-swarm.svg
    VERBATIM)
  add_executable(rossat-swarm
    src/gui/main.cpp
    src/gui/window.cpp
    ${GRESOURCE_C})
  target_link_libraries(rossat-swarm PRIVATE rossat PkgConfig::GTK3)
  # System tray via StatusNotifierItem (works on Wayland bars like waybar)
  pkg_check_modules(APPINDICATOR IMPORTED_TARGET ayatana-appindicator3-0.1)
  if(APPINDICATOR_FOUND)
    target_link_libraries(rossat-swarm PRIVATE PkgConfig::APPINDICATOR)
    target_compile_definitions(rossat-swarm PRIVATE ROSSAT_HAVE_TRAY)
  else()
    message(STATUS "ayatana-appindicator3 not found - building without system tray support")
  endif()
  target_compile_options(rossat-swarm PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
  install(TARGETS rossat-swarm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES data/rossat-swarm.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES data/rossat-swarm.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

# ---- tests (no hardware needed) -------------------------------------------
if(ROSSAT_BUILD_TESTS)
  enable_testing()
  add_executable(rossat-test tests/test_codec.cpp)
  target_link_libraries(rossat-test PRIVATE rossat)
  add_test(NAME codec COMMAND rossat-test)
endif()
