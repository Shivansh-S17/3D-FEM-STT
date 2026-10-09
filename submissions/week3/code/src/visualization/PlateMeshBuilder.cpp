#include "visualization/PlateMeshBuilder.h"

#include "core/PlateDefinition.h"

#include <vtkCellData.h>
#include <vtkIdTypeArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>
#include <cmath>

namespace femgui {

vtkSmartPointer<vtkUnstructuredGrid> PlateMeshBuilder::build(const PlateDefinition &plate)
{
    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();

    const int nx = plate.nodeCountX();
    const int ny = plate.nodeCountY();
    if (nx < 2 || ny < 2)
        return grid; // empty grid; caller checks node/element counts before using it
    // Reject unusable geometry before allocating or handing coordinates to VTK.
    // This also protects callers that preview a project before full solve validation.
    if (plate.nodeCount() > 1000000)
        return grid;
    for (const auto *axis : {&plate.xNodes, &plate.yNodes}) {
        for (qsizetype i = 0; i < axis->size(); ++i) {
            if (!std::isfinite((*axis)[i]) || (i && (*axis)[i] <= (*axis)[i - 1]))
                return grid;
        }
    }

    auto nodeId = [nx](int i, int j) { return static_cast<vtkIdType>(j) * nx + i; };

    vtkNew<vtkPoints> points;
    points->SetDataTypeToDouble();
    points->SetNumberOfPoints(static_cast<vtkIdType>(nx) * ny);
    vtkNew<vtkIdTypeArray> solverNodeIds;
    solverNodeIds->SetName("solver_node_id");
    solverNodeIds->SetNumberOfComponents(1);
    solverNodeIds->SetNumberOfTuples(static_cast<vtkIdType>(nx) * ny);

    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            vtkIdType id = nodeId(i, j);
            points->SetPoint(id, plate.xNodes[i], plate.yNodes[j], 0.0);
            solverNodeIds->SetTypedComponent(id, 0, id);
        }
    }
    grid->SetPoints(points);
    grid->GetPointData()->AddArray(solverNodeIds);

    const int nex = nx - 1;
    const int ney = ny - 1;
    grid->Allocate(static_cast<vtkIdType>(nex) * ney);

    vtkNew<vtkIdTypeArray> solverElementIds;
    solverElementIds->SetName("solver_element_id");
    solverElementIds->SetNumberOfComponents(1);
    solverElementIds->SetNumberOfTuples(static_cast<vtkIdType>(nex) * ney);

    for (int j = 0; j < ney; ++j) {
        for (int i = 0; i < nex; ++i) {
            vtkIdType quad[4] = {
                nodeId(i, j), nodeId(i + 1, j), nodeId(i + 1, j + 1), nodeId(i, j + 1)
            };
            vtkIdType cellId = grid->InsertNextCell(VTK_QUAD, 4, quad);
            vtkIdType elementId = static_cast<vtkIdType>(j) * nex + i;
            solverElementIds->SetTypedComponent(cellId, 0, elementId);
        }
    }
    grid->GetCellData()->AddArray(solverElementIds);

    return grid;
}

} // namespace femgui
