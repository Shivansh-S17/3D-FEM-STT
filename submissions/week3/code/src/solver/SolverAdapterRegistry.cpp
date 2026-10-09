#include "solver/SolverAdapterRegistry.h"

#include "solver/C3D8SolverAdapter.h"
#include "solver/PlateSolverAdapter.h"

namespace femgui {

const ISolverAdapter &SolverAdapterRegistry::forKind(ModelKind kind)
{
    static const PlateSolverAdapter plate;
    static const C3D8SolverAdapter solid;
    return kind == ModelKind::Plate ? static_cast<const ISolverAdapter &>(plate)
                                    : static_cast<const ISolverAdapter &>(solid);
}

} // namespace femgui
