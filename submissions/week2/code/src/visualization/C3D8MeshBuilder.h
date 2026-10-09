#pragma once

#include <vtkSmartPointer.h>

class vtkUnstructuredGrid;

namespace femgui {

struct SolidDefinition;

// Builds the structured brick grid in the received solver's exact X-Z-Y
// enumeration. The z-min face is loaded and the z-max face carries foundation.
class C3D8MeshBuilder
{
public:
    static vtkSmartPointer<vtkUnstructuredGrid> build(const SolidDefinition &solid);
};

} // namespace femgui
