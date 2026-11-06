cmake_minimum_required(VERSION 3.19)

project(PandocUi
    VERSION 0.1.0
    DESCRIPTION "Grafische Oberfläche für pandoc"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/utf-8 /W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Kernbibliothek: wird von der Anwendung und den Tests gemeinsam genutzt
qt_add_library(pandocui_core STATIC
    src/pandocformats.h        src/pandocformats.cpp
    src/pandocinfo.h           src/pandocinfo.cpp
    src/conversionrunner.h     src/conversionrunner.cpp
    src/formatoptionswidget.h  src/formatoptionswidget.cpp
    src/mainwindow.h           src/mainwindow.cpp
)
target_include_directories(pandocui_core PUBLIC src)
target_link_libraries(pandocui_core PUBLIC Qt6::Widgets)
target_compile_definitions(pandocui_core PUBLIC PANDOCUI_VERSION="${PROJECT_VERSION}")

qt_add_executable(PandocUi src/main.cpp)
target_link_libraries(PandocUi PRIVATE pandocui_core)
set_target_properties(PandocUi PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

include(GNUInstallDirs)
install(TARGETS PandocUi
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Tests
option(PANDOCUI_BUILD_TESTS "Tests bauen" ON)
if(PANDOCUI_BUILD_TESTS)
    find_package(Qt6 COMPONENTS Test)
    if(Qt6Test_FOUND)
        enable_testing()
        qt_add_executable(tst_pandocui tests/tst_pandocui.cpp)
        target_link_libraries(tst_pandocui PRIVATE pandocui_core Qt6::Test)
        target_compile_definitions(tst_pandocui PRIVATE
            PANDOCUI_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
        add_test(NAME tst_pandocui COMMAND tst_pandocui)
        set_tests_properties(tst_pandocui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
