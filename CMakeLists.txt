cmake_minimum_required(VERSION 3.16)
project(ROISA VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── ITK ───────────────────────────────────────────────────────────────────────
# brew install itk
# cmake -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6);$(brew --prefix itk)" ..
find_package(ITK REQUIRED COMPONENTS
    ITKCommon
    ITKIOImageBase
    ITKIONIFTI
    ITKIOGDCM
    ITKIOMeta
    ITKIONRRD
    ITKIOBioRad
    ITKIOGE
    ITKIOPNG
    ITKIOTIFF
    ITKIOVTK
    ITKImageGrid
    ITKImageIntensity
    ITKBinaryMathematicalMorphology
    ITKMathematicalMorphology
    ITKDistanceMap
    ITKConnectedComponents
    ITKRegionGrowing
    ITKThresholding
    ITKSmoothing
    ITKFastMarching
    ITKLevelSetsv4
    ITKStatistics
    ITKTransform
)
include(${ITK_USE_FILE})

# ── Qt (prefer 6, fall back to 5) ────────────────────────────────────────────
# brew install qt@6
# cmake -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6) ..
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 QUIET COMPONENTS Core Gui Widgets)
if(Qt6_FOUND)
    message(STATUS "Using Qt ${Qt6_VERSION}")
    set(QT_LIBS Qt6::Core Qt6::Gui Qt6::Widgets)
else()
    find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)
    message(STATUS "Using Qt ${Qt5_VERSION}")
    set(QT_LIBS Qt5::Core Qt5::Gui Qt5::Widgets)
endif()

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/core/ROIVolume.cpp
    src/core/ROIAlgorithms.cpp
    src/gui/MainWindow.cpp
    src/gui/OrthoViewer.cpp
    src/gui/SliceView.cpp
    src/gui/ToolPanel.cpp
)

set(HEADERS
    src/core/ROIVolume.h
    src/core/ROIAlgorithms.h
    src/gui/MainWindow.h
    src/gui/OrthoViewer.h
    src/gui/SliceView.h
    src/gui/ToolPanel.h
)

add_executable(roisa ${SOURCES} ${HEADERS})

target_include_directories(roisa PRIVATE src)

target_link_libraries(roisa
    PRIVATE
        ${ITK_LIBRARIES}
        ${QT_LIBS}
)

if(APPLE)
    set_target_properties(roisa PROPERTIES MACOSX_BUNDLE TRUE)
endif()

# ── Build ─────────────────────────────────────────────────────────────────────
# mkdir build && cd build
# cmake -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6);$(brew --prefix itk)" ..
# cmake --build . -j$(sysctl -n hw.physicalcpu)
# ./roisa.app/Contents/MacOS/roisa          (macOS bundle)
