cmake_minimum_required(VERSION 3.16)
project(ROISA VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── ITK ───────────────────────────────────────────────────────────────────────
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
    ITKRegistrationMethodsv4
    ITKOptimizersv4
    ITKRegistrationCommon
)
include(${ITK_USE_FILE})

# ── VTK (optional) ───────────────────────────────────────────────────────────
# Build VTK 9.3.1 from source before enabling:
#
#   git clone --depth=1 --branch v9.3.1 https://github.com/Kitware/VTK.git ~/vtk-src
#
#   cmake -B ~/vtk-build -S ~/vtk-src                         \
#     -DCMAKE_BUILD_TYPE=Release                              \
#     -DCMAKE_INSTALL_PREFIX=$HOME/vtk                        \
#     -DVTK_GROUP_ENABLE_Qt=YES                               \
#     -DVTK_QT_VERSION=6                                      \
#     -DCMAKE_PREFIX_PATH="$HOME/Qt/6.7.3/macos"             \
#     -DVTK_BUILD_TESTING=OFF                                 \
#     -DVTK_BUILD_EXAMPLES=OFF                                \
#     -DVTK_MODULE_ENABLE_VTK_GUISupportQt=YES               \
#     -DVTK_MODULE_ENABLE_VTK_RenderingOpenGL2=YES            \
#     -DVTK_MODULE_ENABLE_VTK_RenderingVolumeOpenGL2=YES      \
#     -DVTK_MODULE_ENABLE_VTK_FiltersCore=YES                 \
#     -DVTK_MODULE_ENABLE_VTK_InteractionStyle=YES
#
#   cmake --build ~/vtk-build -j$(sysctl -n hw.logicalcpu)
#   cmake --install ~/vtk-build
#
# Then reconfigure ROISA adding $HOME/vtk to CMAKE_PREFIX_PATH:
#   cmake -B build -S . \
#     -DCMAKE_PREFIX_PATH="$HOME/itk;$HOME/Qt/6.7.3/macos;$HOME/vtk"

option(ROISA_USE_VTK "Enable VTK 3-D volume/surface rendering panel" ON)

set(VTK_FOUND FALSE)
if(ROISA_USE_VTK)
    find_package(VTK QUIET COMPONENTS
        CommonCore
        CommonDataModel
        RenderingCore
        RenderingOpenGL2
        RenderingVolumeOpenGL2
        InteractionStyle
        FiltersCore
        GUISupportQt
    )
    if(VTK_FOUND)
        message(STATUS "VTK ${VTK_VERSION} found — 3-D panel ENABLED")
        add_compile_definitions(ROISA_USE_VTK)
    else()
        message(STATUS "VTK not found — 3-D panel will show a placeholder. "
                       "See CMakeLists.txt for build instructions.")
    endif()
endif()

# ── Qt (prefer 6, fall back to 5) ────────────────────────────────────────────
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 QUIET COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
if(Qt6_FOUND)
    message(STATUS "Using Qt ${Qt6_VERSION}")
    set(QT_LIBS Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
else()
    find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)
    message(STATUS "Using Qt ${Qt5_VERSION}")
    set(QT_LIBS Qt5::Core Qt5::Gui Qt5::Widgets)
endif()

# OpenGL (still needed for SurfaceView & SliceView)
find_package(OpenGL REQUIRED)

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/core/ROIVolume.cpp
    src/core/ROIAlgorithms.cpp
    src/core/SUV.cpp
    src/gui/MainWindow.cpp
    src/gui/OrthoViewer.cpp
    src/gui/SliceView.cpp
    src/gui/ToolPanel.cpp
    src/gui/SurfaceView.cpp
    src/gui/HistogramWidget.cpp
    src/gui/VtkView.cpp
    src/gui/SeriesBrowser.cpp
    src/gui/DicomTagWidget.cpp
    src/gui/ImageListWidget.cpp
    src/gui/TacWidget.cpp
)

set(HEADERS
    src/core/ROIVolume.h
    src/core/ROIAlgorithms.h
    src/core/SUV.h
    src/gui/MainWindow.h
    src/gui/OrthoViewer.h
    src/gui/SliceView.h
    src/gui/ToolPanel.h
    src/gui/SurfaceView.h
    src/gui/HistogramWidget.h
    src/gui/VtkView.h
    src/gui/SeriesBrowser.h
    src/gui/DicomTagWidget.h
    src/gui/ImageListWidget.h
    src/gui/TacWidget.h
    src/gui/BgWorker.h
)

add_executable(roisa ${SOURCES} ${HEADERS})

target_include_directories(roisa PRIVATE src)

target_link_libraries(roisa
    PRIVATE
        ${ITK_LIBRARIES}
        ${QT_LIBS}
        OpenGL::GL
)

if(VTK_FOUND)
    target_link_libraries(roisa PRIVATE ${VTK_LIBRARIES})
    vtk_module_autoinit(TARGETS roisa MODULES ${VTK_LIBRARIES})
endif()

if(APPLE)
    set_target_properties(roisa PROPERTIES MACOSX_BUNDLE TRUE)
    target_link_libraries(roisa PRIVATE "-framework OpenGL")
endif()

# ── Quick-start build commands ────────────────────────────────────────────────
# Without VTK (placeholder panel):
#   cmake -B build -S . -DCMAKE_PREFIX_PATH="$HOME/itk;$HOME/Qt/6.7.3/macos"
#
# With VTK (full 3-D rendering):
#   cmake -B build -S . \
#     -DCMAKE_PREFIX_PATH="$HOME/itk;$HOME/Qt/6.7.3/macos;$HOME/vtk"
#
#   cmake --build build -j$(sysctl -n hw.physicalcpu)
#   open build/roisa.app
