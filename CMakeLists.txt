cmake_minimum_required(VERSION 3.16)

project(ShareSelector VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Quick Sql QuickControls2 Network)

if(WIN32)
    set(TWS_API_ROOT "" CACHE PATH "Path to the installed IBKR TWS API")
    set(TWS_API_PROJECT "source/CSharpClient/client/CSharpAPI.csproj")
    if(NOT EXISTS "${TWS_API_ROOT}/${TWS_API_PROJECT}")
        foreach(TWS_API_CANDIDATE "C:/TWS API" "K:/TWS API")
            if(EXISTS "${TWS_API_CANDIDATE}/${TWS_API_PROJECT}")
                set(TWS_API_ROOT "${TWS_API_CANDIDATE}"
                    CACHE PATH "Path to the installed IBKR TWS API" FORCE)
                break()
            endif()
        endforeach()
    endif()
    if(NOT EXISTS "${TWS_API_ROOT}/${TWS_API_PROJECT}")
        message(FATAL_ERROR "IBKR TWS API not found. Set TWS_API_ROOT to its installation directory.")
    endif()

    find_program(DOTNET_EXECUTABLE dotnet REQUIRED)
    set(IBKR_HELPER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/ibkr-helper")
    set(IBKR_HELPER_EXECUTABLE "${IBKR_HELPER_OUTPUT_DIR}/IbkrHelper.exe")
    add_custom_command(
        OUTPUT "${IBKR_HELPER_EXECUTABLE}"
        COMMAND "${DOTNET_EXECUTABLE}" publish
            "${CMAKE_CURRENT_SOURCE_DIR}/ibkr-helper/IbkrHelper.csproj"
            --configuration Release
            --framework net8.0
            --output "${IBKR_HELPER_OUTPUT_DIR}"
            --nologo
            -p:TwsApiRoot=${TWS_API_ROOT}
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/ibkr-helper/IbkrHelper.csproj"
            "${CMAKE_CURRENT_SOURCE_DIR}/ibkr-helper/Program.cs"
        VERBATIM
    )
    add_custom_target(ibkr_helper ALL DEPENDS "${IBKR_HELPER_EXECUTABLE}")
endif()

qt_standard_project_setup()

# Policy QTP0001 setzen
qt_policy(SET QTP0001 NEW)

qt_add_executable(appShareSelector
    main.cpp
)

if(WIN32)
    add_dependencies(appShareSelector ibkr_helper)
endif()

qt_add_qml_module(appShareSelector
    URI ShareSelector
    VERSION 1.0
    QML_FILES
        Main.qml  # Groß-/Kleinschreibung prüfen!
        PortfolioChartWindow.qml
        PortfolioWindow.qml
        StockAnalysisConfigPanel.qml
        StockAnalysisChartPanel.qml
        StockAnalysisBuyDialog.qml
        PositionManagementDialog.qml
        PortfolioBatchWindow.qml
    SOURCES
        databasemanager.cpp
        databasemanager_state.cpp
        databasemanager_ibkr.cpp
        databasemanager_marketstack.cpp
        databasemanager_fundamentals.cpp
        databasemanager_portfolio.cpp
        databasemanager_stockanalysis.cpp
        databasemanager_quoteimport.cpp
        databasemanager.h
        databasemanager_internal.h
        alphavantageclient.h
        alphavantageclient.cpp
        yahoofinanceclient.h
        yahoofinanceclient.cpp
        marketstackclient.h
        marketstackclient.cpp
)

set_target_properties(appShareSelector PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appShareSelector
    PRIVATE Qt6::Quick Qt6::Sql Qt6::QuickControls2 Qt6::Network
)

include(GNUInstallDirs)
install(TARGETS appShareSelector
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(WIN32)
    install(DIRECTORY "${IBKR_HELPER_OUTPUT_DIR}/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/ibkr-helper")
endif()

message(STATUS "QML_FILES: ${QML_FILES}")
