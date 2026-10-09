#pragma once

#include "core/ModelDefinition.h"

namespace femgui {

class ISolverAdapter;

class SolverAdapterRegistry
{
public:
    static const ISolverAdapter &forKind(ModelKind kind);
    static const ISolverAdapter &forModel(const ModelDefinition &model) { return forKind(modelKind(model)); }
};

} // namespace femgui
