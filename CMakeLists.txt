cmake_minimum_required(VERSION 3.24)

project(LumenPDF
    VERSION 0.1.0
    DESCRIPTION "A fast, elegant PDF viewer and editor"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
# PDFium is fetched separately (see scripts/fetch-pdfium.ps1). Until it is
# present the app builds against a stub engine so the UI can be developed
# independently of the render backend.
option(LUMEN_WITH_PDFIUM "Build against PDFium (requires third_party/pdfium)" ON)

# ---------------------------------------------------------------------------
# Qt
# ---------------------------------------------------------------------------
find_package(Qt6 6.7 REQUIRED COMPONENTS
    Core Gui Quick QuickControls2 Concurrent Network ShaderTools)

qt_standard_project_setup(REQUIRES 6.7)

# ---------------------------------------------------------------------------
# PDFium
# ---------------------------------------------------------------------------
set(LUMEN_PDFIUM_DIR "${CMAKE_SOURCE_DIR}/third_party/pdfium")
set(LUMEN_PDFIUM_FOUND OFF)

if(LUMEN_WITH_PDFIUM AND EXISTS "${LUMEN_PDFIUM_DIR}/include/fpdfview.h")
    add_library(pdfium SHARED IMPORTED GLOBAL)
    set_target_properties(pdfium PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${LUMEN_PDFIUM_DIR}/include"
    )
    if(WIN32)
        set_target_properties(pdfium PROPERTIES
            IMPORTED_LOCATION "${LUMEN_PDFIUM_DIR}/bin/pdfium.dll"
            IMPORTED_IMPLIB   "${LUMEN_PDFIUM_DIR}/lib/pdfium.dll.lib"
        )
    elseif(APPLE)
        set_target_properties(pdfium PROPERTIES
            IMPORTED_LOCATION "${LUMEN_PDFIUM_DIR}/lib/libpdfium.dylib")
    else()
        set_target_properties(pdfium PROPERTIES
            IMPORTED_LOCATION "${LUMEN_PDFIUM_DIR}/lib/libpdfium.so")
    endif()
    set(LUMEN_PDFIUM_FOUND ON)
    message(STATUS "LumenPDF: PDFium found at ${LUMEN_PDFIUM_DIR}")
else()
    message(WARNING
        "LumenPDF: PDFium not found -- building with the stub engine.\n"
        "  Run scripts/fetch-pdfium.ps1 to download it.")
endif()

# ---------------------------------------------------------------------------
# Application target
# ---------------------------------------------------------------------------
qt_add_executable(lumenpdf
    src/app/main.cpp
    src/app/TestFixtures.h
    src/app/TestFixtures.cpp
    src/app/StateReport.h
    src/app/StateReport.cpp
    src/app/Timing.h
    src/app/Timing.cpp

    src/core/PdfTypes.h
    src/core/PdfDocument.h
    src/core/PdfDocument.cpp
    src/core/PdfEngine.h
    src/core/PdfEngine.cpp

    src/render/PageRenderCache.h
    src/render/PageRenderCache.cpp
    src/render/PageImageProvider.h
    src/render/PageImageProvider.cpp

    src/bridge/DocumentController.h
    src/bridge/DocumentController.cpp
    src/bridge/PageListModel.h
    src/bridge/PageListModel.cpp
    src/bridge/OutlineModel.h
    src/bridge/OutlineModel.cpp
    src/bridge/SearchController.h
    src/bridge/SearchController.cpp
    src/bridge/SelectionController.h
    src/bridge/SelectionController.cpp
    src/bridge/AnnotationController.h
    src/bridge/AnnotationController.cpp
    src/bridge/PageOperations.h
    src/bridge/PageOperations.cpp
    src/bridge/ExportController.h
    src/bridge/ExportController.cpp
    src/bridge/RedactionController.h
    src/bridge/RedactionController.cpp
    src/bridge/FormController.h
    src/bridge/FormController.cpp
    src/bridge/OcrController.h
    src/bridge/OcrController.cpp

    src/ocr/OcrEngine.h
    src/ocr/OcrEngine.cpp

    src/cloud/GoogleAuth.h
    src/cloud/GoogleAuth.cpp
    src/cloud/GoogleDrive.h
    src/cloud/GoogleDrive.cpp

    src/platform/PlatformWindow.h
    src/platform/PlatformWindow.cpp
)

target_include_directories(lumenpdf PRIVATE src)

target_compile_definitions(lumenpdf PRIVATE
    LUMEN_VERSION="${PROJECT_VERSION}"
    QT_DISABLE_DEPRECATED_UP_TO=0x060700
)

target_link_libraries(lumenpdf PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Concurrent
    Qt6::Network
)

if(LUMEN_PDFIUM_FOUND)
    target_link_libraries(lumenpdf PRIVATE pdfium)
    target_compile_definitions(lumenpdf PRIVATE LUMEN_HAS_PDFIUM=1)
endif()

if(WIN32)
    # -- OCR ----------------------------------------------------------------
    #
    # Windows ships an OCR engine, and using it means the installer stays at
    # forty megabytes instead of carrying a hundred megabytes of Tesseract
    # language data for languages most users will never open.
    #
    # C++/WinRT headers live in the Windows SDK next to the usual um/shared
    # includes, and CMake does not add that directory itself.
    get_filename_component(_win_sdk_um "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(LUMEN_CPPWINRT_DIR "")

    foreach(_inc IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
        if(_inc MATCHES "Windows Kits/10/[Ii]nclude/([0-9.]+)/um$")
            string(REGEX REPLACE "/um$" "/cppwinrt" _candidate "${_inc}")
            if(EXISTS "${_candidate}/winrt/base.h")
                set(LUMEN_CPPWINRT_DIR "${_candidate}")
            endif()
        endif()
    endforeach()

    # The implicit include list is not always populated; fall back to looking
    # in the standard install location for the newest SDK that has the headers.
    if(NOT LUMEN_CPPWINRT_DIR)
        file(GLOB _sdk_versions
             "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/Include/*/cppwinrt")
        list(SORT _sdk_versions)
        list(REVERSE _sdk_versions)
        foreach(_candidate IN LISTS _sdk_versions)
            if(EXISTS "${_candidate}/winrt/base.h")
                set(LUMEN_CPPWINRT_DIR "${_candidate}")
                break()
            endif()
        endforeach()
    endif()

    if(LUMEN_CPPWINRT_DIR)
        target_include_directories(lumenpdf PRIVATE "${LUMEN_CPPWINRT_DIR}")
        target_compile_definitions(lumenpdf PRIVATE LUMEN_HAS_WINDOWS_OCR=1)
        target_link_libraries(lumenpdf PRIVATE windowsapp)
        message(STATUS "LumenPDF: Windows OCR enabled (${LUMEN_CPPWINRT_DIR})")
    else()
        message(WARNING
            "LumenPDF: no C++/WinRT headers found -- building without OCR.\n"
            "  Install a Windows 10/11 SDK to enable it.")
    endif()

    # crypt32 provides the Data Protection API, which encrypts the stored Google
    # refresh token against the Windows account rather than leaving a standing
    # credential to someone's Drive sitting in a readable file.
    target_link_libraries(lumenpdf PRIVATE dwmapi crypt32)
    set_target_properties(lumenpdf PROPERTIES WIN32_EXECUTABLE ON)

    # windows.h defines min/max as macros, which breaks std::numeric_limits<T>::max()
    # and std::min/std::max wherever it is pulled in -- and PDFium's headers pull
    # it in. WIN32_LEAN_AND_MEAN also keeps the include cheap.
    target_compile_definitions(lumenpdf PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------------------
# QML modules
# ---------------------------------------------------------------------------
add_subdirectory(qml/Lumen)
add_subdirectory(qml/App)

target_link_libraries(lumenpdf PRIVATE Lumenplugin Appplugin)

# ---------------------------------------------------------------------------
# Runtime deployment
# ---------------------------------------------------------------------------
if(WIN32 AND LUMEN_PDFIUM_FOUND)
    add_custom_command(TARGET lumenpdf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LUMEN_PDFIUM_DIR}/bin/pdfium.dll"
            "$<TARGET_FILE_DIR:lumenpdf>"
        COMMENT "Copying pdfium.dll next to lumenpdf.exe"
    )
endif()

install(TARGETS lumenpdf
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
)

qt_generate_deploy_qml_app_script(
    TARGET lumenpdf
    OUTPUT_SCRIPT deploy_script
    MACOS_BUNDLE_POST_BUILD
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
)
install(SCRIPT ${deploy_script})
