#pragma once

#include "solver/ISolverAdapter.h"

namespace femgui {

class C3D8SolverAdapter : public ISolverAdapter
{
public:
    const SolverCapabilities &capabilities() const override;
    ValidationIssues validate(const ModelDefinition &model) const override;
    QJsonObject buildSolverInput(const ModelDefinition &model,
                                 QVector<PatchRowMapping> *outPatchRows = nullptr) const override;

    ValidationIssues validate(const SolidDefinition &solid) const;
    QJsonObject buildSolverInput(const SolidDefinition &solid,
                                 QVector<PatchRowMapping> *outPatchRows = nullptr) const;
};

} // namespace femgui
