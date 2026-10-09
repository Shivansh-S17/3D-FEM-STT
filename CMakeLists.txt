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

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Concurrent)
find_package(VTK REQUIRED COMPONENTS
    CommonCore
    CommonColor
    CommonDataModel
    CommonExecutionModel
    FiltersCore
    FiltersGeneral
    FiltersGeometry
    FiltersSources
    InteractionStyle
    IOLegacy
    RenderingCore
    RenderingOpenGL2
    RenderingAnnotation
    RenderingFreeType
    GUISupportQt
)

qt_standard_project_setup()

add_library(FemGuiLib STATIC
    src/ui/UiStyle.h
    src/ui/UiStyle.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/ProjectPanel.h
    src/ui/ProjectPanel.cpp
    src/ui/ScientificNumberEdit.h
    src/ui/GeometryEditorDialog.h
    src/ui/GeometryEditorDialog.cpp
    src/ui/PressurePatchDialog.h
    src/ui/PressurePatchDialog.cpp
    src/ui/ViewportWidget.h
    src/ui/ViewportWidget.cpp
    src/ui/InspectorPanel.h
    src/ui/InspectorPanel.cpp
    src/ui/ProblemsPanel.h
    src/ui/ProblemsPanel.cpp
    src/core/PlateDefinition.h
    src/core/SolidDefinition.h
    src/core/ModelDefinition.h
    src/core/ValidationIssue.h
    src/core/RunRecord.h
    src/core/ResultSet.h
    src/core/ResultSet.cpp
    src/core/ProjectDocument.h
    src/core/ProjectDocument.cpp
    src/controllers/ProjectController.h
    src/controllers/ProjectController.cpp
    src/controllers/AnalysisController.h
    src/controllers/AnalysisController.cpp
    src/io/ProjectSerializer.h
    src/io/ProjectSerializer.cpp
    src/io/ResultImporter.h
    src/io/ResultImporter.cpp
    src/io/ResultExporter.h
    src/io/ResultExporter.cpp
    src/solver/SolverCapabilities.h
    src/solver/ISolverAdapter.h
    src/solver/PlateSolverAdapter.h
    src/solver/PlateSolverAdapter.cpp
    src/solver/C3D8SolverAdapter.h
    src/solver/C3D8SolverAdapter.cpp
    src/solver/SolverAdapterRegistry.h
    src/solver/SolverAdapterRegistry.cpp
    src/solver/SolverJob.h
    src/solver/SolverJob.cpp
    src/visualization/PlateMeshBuilder.h
    src/visualization/PlateMeshBuilder.cpp
    src/visualization/C3D8MeshBuilder.h
    src/visualization/C3D8MeshBuilder.cpp
)

target_include_directories(FemGuiLib PUBLIC src)

target_link_libraries(FemGuiLib PUBLIC
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Concurrent
    ${VTK_LIBRARIES}
)

add_executable(FemGui src/app/main.cpp src/app/Application.cpp)
target_link_libraries(FemGui PRIVATE FemGuiLib)

vtk_module_autoinit(
    TARGETS FemGui
    MODULES ${VTK_LIBRARIES}
)

set_target_properties(FemGui PROPERTIES
    WIN32_EXECUTABLE TRUE
)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(FemGuiTests tests/IntegrationTests.cpp)
    target_link_libraries(FemGuiTests PRIVATE FemGuiLib Qt6::Test)
    target_compile_definitions(FemGuiTests PRIVATE
        FEM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        FEM_TEST_PYTHON="${Python3_EXECUTABLE}")
    vtk_module_autoinit(TARGETS FemGuiTests MODULES ${VTK_LIBRARIES})
    add_test(NAME Integration COMMAND FemGuiTests -o regression-results.xml,junitxml)
    add_test(NAME SolverContract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_solver_contract.py")
    add_test(NAME NumericalQualification COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_numerical_qualification.py")
    add_test(NAME C3D8Reference COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_c3d8_reference.py")
    add_test(NAME C3D8Production COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_c3d8_production.py")
    add_test(NAME C3D8Qualification COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_c3d8_qualification.py")
    set_tests_properties(C3D8Reference PROPERTIES TIMEOUT 60 ENVIRONMENT "PYTHONUTF8=1;MPLBACKEND=Agg")
    set_tests_properties(C3D8Production PROPERTIES TIMEOUT 120 ENVIRONMENT "PYTHONUTF8=1;PYTHONDONTWRITEBYTECODE=1")
    set_tests_properties(C3D8Qualification PROPERTIES TIMEOUT 180 ENVIRONMENT "PYTHONUTF8=1;PYTHONDONTWRITEBYTECODE=1")
    set(C3D8_REFERENCE_PYTHON "" CACHE FILEPATH "Optional isolated Python with the original C3D8 runtime dependencies")
    if(C3D8_REFERENCE_PYTHON)
        add_test(NAME C3D8ReferenceRuntime COMMAND "${C3D8_REFERENCE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_c3d8_reference_runtime.py")
        set_tests_properties(C3D8ReferenceRuntime PROPERTIES TIMEOUT 120 ENVIRONMENT "PYTHONUTF8=1;PYTHONDONTWRITEBYTECODE=1;MPLBACKEND=Agg")
    endif()
    set_tests_properties(NumericalQualification PROPERTIES TIMEOUT 180 ENVIRONMENT "PYTHONUTF8=1;MPLBACKEND=Agg")
    set_tests_properties(SolverContract PROPERTIES TIMEOUT 180 ENVIRONMENT "PYTHONUTF8=1;MPLBACKEND=Agg")
    set_tests_properties(Integration PROPERTIES
        TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:VTK::CommonCore>")
endif()
