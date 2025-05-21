cmake_minimum_required(VERSION 3.16)

project(ShareSelector VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Quick Sql QuickControls2 Concurrent)

qt_standard_project_setup()

# Policy QTP0001 setzen
qt_policy(SET QTP0001 NEW)

qt_add_executable(appShareSelector
    main.cpp
)

qt_add_qml_module(appShareSelector
    URI ShareSelector
    VERSION 1.0
    QML_FILES
        Main.qml  # Groß-/Kleinschreibung prüfen!
    SOURCES
        databasemanager.cpp
        databasemanager.h
        SOURCES polygonstockapi.h polygonstockapi.cpp
        SOURCES sharedata.h sharedata.cpp
        QML_FILES ExportAktienManager.qml
        SOURCES stockexportcontroller.h stockexportcontroller.cpp
        SOURCES alphavantageclient.h alphavantageclient.cpp
        SOURCES marketstackclient.h marketstackclient.cpp
        SOURCES clipboardhelper.h
        SOURCES isinfetcher.h isinfetcher.cpp
)

set_target_properties(appShareSelector PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appShareSelector
    PRIVATE Qt6::Quick Qt6::Sql  Qt6::QuickControls2 Qt6::Concurrent
)

include(GNUInstallDirs)
install(TARGETS appShareSelector
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

message(STATUS "QML_FILES: ${QML_FILES}")
