#pragma once

#include <vtkSmartPointer.h>

class vtkUnstructuredGrid;

namespace femgui {

struct PlateDefinition;

// Builds the exact quad mesh the solver enumerates , verified
// against mesh_plate_enumx.getNodesOfElement()/getCoordinatesOfNode() in
// solver/reference/expected_solver_implementation.py:
//   nodeId(i,j) = j*Nx + i;  point = (x[i], y[j], 0)
//   elementId(i,j) = j*(Nx-1) + i
//   connectivity = [nodeId(i,j), nodeId(i+1,j), nodeId(i+1,j+1), nodeId(i,j+1)]
// Adds "solver_node_id" point data and "solver_element_id" cell data so
// picks and result imports never rely on VTK's own local IDs.
class PlateMeshBuilder
{
public:
    static vtkSmartPointer<vtkUnstructuredGrid> build(const PlateDefinition &plate);
};

} // namespace femgui
