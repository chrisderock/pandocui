cmake_minimum_required(VERSION 3.19)

# Version: Standardwert, beim Release-Build aus dem Git-Tag über die
# Umgebungsvariable PANDOCUI_RELEASE_VERSION gesetzt (z. B. Tag v0.2.0 → 0.2.0).
set(PANDOCUI_PROJECT_VERSION 0.1.1)
if(NOT "$ENV{PANDOCUI_RELEASE_VERSION}" STREQUAL "")
    set(PANDOCUI_PROJECT_VERSION "$ENV{PANDOCUI_RELEASE_VERSION}")
endif()

project(PandocUi
    VERSION ${PANDOCUI_PROJECT_VERSION}
    DESCRIPTION "Grafische Oberfläche für pandoc"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets LinguistTools)
qt_standard_project_setup(I18N_SOURCE_LANGUAGE de I18N_TRANSLATED_LANGUAGES en)

# Statisch gebautes Qt (Windows-Release, siehe ci/build-qt-static.ps1): Die Anwendung wird
# dann zu einer einzelnen .exe ohne Qt-DLLs gelinkt.
get_target_property(_pandocui_qt_core_type Qt6::Core TYPE)
if(_pandocui_qt_core_type STREQUAL "STATIC_LIBRARY")
    set(PANDOCUI_STATIC_QT ON)
    message(STATUS "Qt ist statisch gebaut: Anwendung wird statisch gelinkt")
else()
    set(PANDOCUI_STATIC_QT OFF)
endif()
if(PANDOCUI_STATIC_QT AND MSVC)
    # Laufzeitbibliothek passend zu Qt wählen (-static-runtime → /MT), sonst LNK2038.
    # Muss vor dem Anlegen der Targets gesetzt werden.
    if(NOT DEFINED QT_FEATURE_static_runtime OR QT_FEATURE_static_runtime)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

if(MSVC)
    add_compile_options(/utf-8 /W4)
    # Debug-Informationen (PDB) auch im Release-Build, für Stacktraces bei Abstürzen.
    # Die PDB-Dateien werden nicht mit ausgeliefert.
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Kernbibliothek: wird von der Anwendung und den Tests gemeinsam genutzt
set(PANDOCUI_CORE_SOURCES
    src/pandocformats.h        src/pandocformats.cpp
    src/pandocinfo.h           src/pandocinfo.cpp
    src/conversionrunner.h     src/conversionrunner.cpp
    src/formatoptionswidget.h  src/formatoptionswidget.cpp
    src/presetstore.h          src/presetstore.cpp
    src/translation.h          src/translation.cpp
    src/stepnavigator.h        src/stepnavigator.cpp
    src/pandocmissingdialog.h  src/pandocmissingdialog.cpp
    src/mainwindow.h           src/mainwindow.cpp
)
qt_add_library(pandocui_core STATIC ${PANDOCUI_CORE_SOURCES})
target_include_directories(pandocui_core PUBLIC src)
target_link_libraries(pandocui_core PUBLIC Qt6::Widgets)
target_compile_definitions(pandocui_core PUBLIC PANDOCUI_VERSION="${PROJECT_VERSION}")

# Übersetzungen: Quellsprache ist Deutsch. Die .qm-Dateien werden unter :/i18n eingebettet.
# Texte aktualisieren:  cmake --build <build> --target update_translations
qt_add_translations(pandocui_core
    TS_FILES translations/pandocui_en.ts
    SOURCES ${PANDOCUI_CORE_SOURCES} src/main.cpp
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations none
)

# Bei statischem Qt gibt es keinen Ordner mit Qt-Übersetzungen neben der Anwendung:
# die Texte der Qt-eigenen Dialoge (qtbase_<sprache>.qm) ebenfalls unter :/i18n einbetten.
if(PANDOCUI_STATIC_QT)
    set(_pandocui_qt_qm_dir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
    set(_pandocui_qt_qm_files)
    foreach(_lang IN ITEMS de en)
        if(EXISTS "${_pandocui_qt_qm_dir}/qtbase_${_lang}.qm")
            list(APPEND _pandocui_qt_qm_files "${_pandocui_qt_qm_dir}/qtbase_${_lang}.qm")
        else()
            message(WARNING "qtbase_${_lang}.qm fehlt in ${_pandocui_qt_qm_dir} (qttranslations nicht gebaut?)")
        endif()
    endforeach()
    if(_pandocui_qt_qm_files)
        qt_add_resources(pandocui_core "pandocui_qt_translations"
            PREFIX "/i18n"
            BASE "${_pandocui_qt_qm_dir}"
            FILES ${_pandocui_qt_qm_files}
        )
    endif()
endif()

qt_add_executable(PandocUi src/main.cpp)
target_link_libraries(PandocUi PRIVATE pandocui_core)
set_target_properties(PandocUi PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    # Angaben für Info.plist des macOS-App-Bundles
    MACOSX_BUNDLE_BUNDLE_NAME "PandocUi"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.chrisderock.pandocui"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_INFO_STRING "PandocUi ${PROJECT_VERSION}"
)

include(GNUInstallDirs)
install(TARGETS PandocUi
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(UNIX AND NOT APPLE)
    install(FILES packaging/pandocui.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

# Pakete für Linux (cpack -G DEB bzw. cpack -G RPM)
set(CPACK_PACKAGE_NAME "pandocui")
set(CPACK_PACKAGE_VENDOR "PandocUi")
set(CPACK_PACKAGE_CONTACT "chrisderock <chrisderock@users.noreply.github.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/chrisderock/pandocui")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Graphical user interface for pandoc")
set(CPACK_PACKAGE_DESCRIPTION
    "PandocUi converts documents with pandoc using a step-by-step wizard: choose an \
input file, one or more output formats with per-format options and an output folder.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "text")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)           # Abhängigkeiten aus den Bibliotheken ermitteln
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pandoc")

set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-only")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Publishing")
set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)           # Dist-Tag, z. B. .el10
# pandoc ist bei Rocky/RHEL nur über EPEL verfügbar und wird daher nicht als harte
# Abhängigkeit eingetragen (weiche Abhängigkeiten unterstützt CPackRPM dort nicht).
include(CPack)

# Hilfsprogramm für die Bildschirmfotos der README (siehe tools/make-screenshots.sh)
option(PANDOCUI_BUILD_TOOLS "Hilfsprogramme bauen" OFF)
if(PANDOCUI_BUILD_TOOLS)
    qt_add_executable(pandocui_screenshots tools/screenshots.cpp)
    target_link_libraries(pandocui_screenshots PRIVATE pandocui_core)
    set_target_properties(pandocui_screenshots PROPERTIES WIN32_EXECUTABLE OFF MACOSX_BUNDLE OFF)
    if(PANDOCUI_STATIC_QT)
        qt_import_plugins(pandocui_screenshots INCLUDE Qt6::QOffscreenIntegrationPlugin)
    endif()
endif()

# Tests
option(PANDOCUI_BUILD_TESTS "Tests bauen" ON)
if(PANDOCUI_BUILD_TESTS)
    find_package(Qt6 COMPONENTS Test)
    if(Qt6Test_FOUND)
        enable_testing()
        qt_add_executable(tst_pandocui tests/tst_pandocui.cpp)
        target_link_libraries(tst_pandocui PRIVATE pandocui_core Qt6::Test)
        # qt_standard_project_setup() macht Programme unter Windows zu GUI-Programmen;
        # das Testprogramm braucht aber eine Konsole für seine Ausgabe.
        set_target_properties(tst_pandocui PROPERTIES WIN32_EXECUTABLE OFF MACOSX_BUNDLE OFF)
        if(PANDOCUI_STATIC_QT)
            # Statisch wird nur das Standard-Plattform-Plugin eingebunden; die Tests laufen offscreen.
            qt_import_plugins(tst_pandocui INCLUDE Qt6::QOffscreenIntegrationPlugin)
        endif()
        target_compile_definitions(tst_pandocui PRIVATE
            PANDOCUI_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
        add_test(NAME tst_pandocui COMMAND tst_pandocui)
        set_tests_properties(tst_pandocui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
