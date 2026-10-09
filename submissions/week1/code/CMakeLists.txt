cmake_minimum_required(VERSION 3.21)
project(FemGui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# The installed VTK build ships only its release (/MD) runtime variant.
# A true CMAKE_BUILD_TYPE=Debug app links Qt's debug import libraries
# (built /MDd) alongside VTK's release ones, mixing two CRT modules in one
# process and corrupting the heap at the app/library boundary. RelWithDebInfo
# gives debug symbols with the /MD runtime, which matches VTK and (via
# CMake's config fallback) resolves Qt6's imported targets to their release
# binaries, so use it as the default "debug" configuration.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(VTK REQUIRED COMPONENTS
    CommonCore
    CommonColor
    CommonDataModel
    CommonExecutionModel
    FiltersCore
    FiltersGeometry
    FiltersSources
    InteractionStyle
    RenderingCore
    RenderingOpenGL2
    RenderingAnnotation
    RenderingFreeType
    GUISupportQt
)

qt_standard_project_setup()

add_executable(FemGui
    src/app/main.cpp
    src/app/Application.h
    src/app/Application.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/ProjectPanel.h
    src/ui/ProjectPanel.cpp
    src/ui/ViewportWidget.h
    src/ui/ViewportWidget.cpp
    src/ui/InspectorPanel.h
    src/ui/InspectorPanel.cpp
    src/ui/ProblemsPanel.h
    src/ui/ProblemsPanel.cpp
)

target_include_directories(FemGui PRIVATE src)

target_link_libraries(FemGui PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    ${VTK_LIBRARIES}
)

vtk_module_autoinit(
    TARGETS FemGui
    MODULES ${VTK_LIBRARIES}
)

set_target_properties(FemGui PROPERTIES
    WIN32_EXECUTABLE TRUE
)
