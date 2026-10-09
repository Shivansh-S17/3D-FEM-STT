#include "visualization/C3D8MeshBuilder.h"

#include "core/SolidDefinition.h"

#include <vtkCellData.h>
#include <vtkIdTypeArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkUnsignedCharArray.h>
#include <vtkUnstructuredGrid.h>

#include <cmath>

namespace femgui {

vtkSmartPointer<vtkUnstructuredGrid> C3D8MeshBuilder::build(const SolidDefinition &solid)
{
    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    const int nx = solid.nodeCountX(), ny = solid.nodeCountY(), nz = solid.nodeCountZ();
    if (nx < 2 || ny < 2 || nz < 2 || solid.nodeCount() > 1000000) return grid;
    for (const auto *axis : {&solid.xNodes, &solid.yNodes, &solid.zNodes})
        for (qsizetype i = 0; i < axis->size(); ++i)
            if (!std::isfinite((*axis)[i]) || (i && (*axis)[i] <= (*axis)[i - 1])) return grid;

    // X varies fastest, then Z, then Y: node = j*(nx*nz) + k*nx + i.
    auto nodeId = [=](int i, int j, int k) {
        return static_cast<vtkIdType>(j) * nx * nz + static_cast<vtkIdType>(k) * nx + i;
    };
    const vtkIdType nodeCount = static_cast<vtkIdType>(nx) * ny * nz;
    vtkNew<vtkPoints> points; points->SetDataTypeToDouble(); points->SetNumberOfPoints(nodeCount);
    vtkNew<vtkIdTypeArray> nodeIds; nodeIds->SetName("solver_node_id"); nodeIds->SetNumberOfTuples(nodeCount);
    vtkNew<vtkUnsignedCharArray> loadedNodes; loadedNodes->SetName("loaded_face_node"); loadedNodes->SetNumberOfTuples(nodeCount);
    vtkNew<vtkUnsignedCharArray> foundationNodes; foundationNodes->SetName("foundation_face_node"); foundationNodes->SetNumberOfTuples(nodeCount);
    for (int j = 0; j < ny; ++j) for (int k = 0; k < nz; ++k) for (int i = 0; i < nx; ++i) {
        const auto id = nodeId(i, j, k);
        points->SetPoint(id, solid.xNodes[i], solid.yNodes[j], solid.zNodes[k]);
        nodeIds->SetValue(id, id);
        loadedNodes->SetValue(id, k == 0);
        foundationNodes->SetValue(id, k == nz - 1);
    }
    grid->SetPoints(points);
    grid->GetPointData()->AddArray(nodeIds);
    grid->GetPointData()->AddArray(loadedNodes);
    grid->GetPointData()->AddArray(foundationNodes);

    const int nex = nx - 1, ney = ny - 1, nez = nz - 1;
    const vtkIdType elementCount = static_cast<vtkIdType>(nex) * ney * nez;
    grid->Allocate(elementCount);
    vtkNew<vtkIdTypeArray> elementIds; elementIds->SetName("solver_element_id"); elementIds->SetNumberOfTuples(elementCount);
    vtkNew<vtkUnsignedCharArray> loadedElements; loadedElements->SetName("loaded_face_element"); loadedElements->SetNumberOfTuples(elementCount);
    vtkNew<vtkUnsignedCharArray> foundationElements; foundationElements->SetName("foundation_face_element"); foundationElements->SetNumberOfTuples(elementCount);

    // Elements use the same X-Z-Y progression and local order as getNodesOfElement().
    for (int j = 0; j < ney; ++j) for (int k = 0; k < nez; ++k) for (int i = 0; i < nex; ++i) {
        const vtkIdType hex[8] = {
            nodeId(i, j, k), nodeId(i + 1, j, k), nodeId(i + 1, j + 1, k), nodeId(i, j + 1, k),
            nodeId(i, j, k + 1), nodeId(i + 1, j, k + 1), nodeId(i + 1, j + 1, k + 1), nodeId(i, j + 1, k + 1)
        };
        const auto cellId = grid->InsertNextCell(VTK_HEXAHEDRON, 8, hex);
        const vtkIdType elementId = static_cast<vtkIdType>(j) * nex * nez + static_cast<vtkIdType>(k) * nex + i;
        elementIds->SetValue(cellId, elementId);
        loadedElements->SetValue(cellId, k == 0);
        foundationElements->SetValue(cellId, k == nez - 1);
    }
    grid->GetCellData()->AddArray(elementIds);
    grid->GetCellData()->AddArray(loadedElements);
    grid->GetCellData()->AddArray(foundationElements);
    return grid;
}

} // namespace femgui
