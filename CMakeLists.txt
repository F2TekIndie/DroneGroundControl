cmake_minimum_required(VERSION 3.16)

project(DroneGroundContorl VERSION 0.1 LANGUAGES CXX)

#
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(THIRD_PARTY_ROOT "/home/f2tek/SwDev" CACHE PATH "Root of third-party libraries")

#
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)

#
find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.22)

qt_standard_project_setup(REQUIRES 6.11)

qt_add_executable(dgc
    src/main.cpp
    src/video/receiver.h src/video/receiver.cpp
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(dgc PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.rosvis
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(dgc PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Boost::system
    Threads::Threads
    PkgConfig::GST
)

target_include_directories(dgc PRIVATE
    ui
    src/video
)

qt_add_qml_module(dgc
    URI DroneGroundControl
    VERSION 1.0
    QML_FILES ui/Main.qml ui/droneList.qml ui/cameraView.qml ui/droneMap.qml
    RESOURCE_PREFIX /dgc
    NO_RESOURCE_TARGET_PATH
    SOURCES src/video/receiver.h
)

install(TARGETS dgc
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_qml_app_script(
    TARGET dgc
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
)
install(SCRIPT ${deploy_script})
