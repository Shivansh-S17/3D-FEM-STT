#pragma once

#include "solver/ISolverAdapter.h"

namespace femgui {

// Implements the exact schema read by
// solver/reference/expected_solver_implementation.py::read_unified_agent_input().
class PlateSolverAdapter : public ISolverAdapter
{
public:
    const SolverCapabilities &capabilities() const override;
    ValidationIssues validate(const ModelDefinition &model) const override;
    QJsonObject buildSolverInput(const ModelDefinition &model,
                                  QVector<PatchRowMapping> *outPatchRows = nullptr) const override;

    ValidationIssues validate(const PlateDefinition &plate) const;
    QJsonObject buildSolverInput(const PlateDefinition &plate,
                                 QVector<PatchRowMapping> *outPatchRows = nullptr) const;
};

} // namespace femgui
