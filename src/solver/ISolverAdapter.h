#pragma once

#include <QJsonObject>
#include <QString>

#include "core/ModelDefinition.h"
#include "core/ValidationIssue.h"
#include "solver/SolverCapabilities.h"

namespace femgui {

// One row of the patch->solver-array mapping, so a run can be traced back
// to the GUI patch that produced it (section 4.3).
struct PatchRowMapping
{
    QUuid patchId;
    int row = -1;
};

// Adapter boundary between the editable project model and one solver's
// exact input/output schema (section 4.2). A different final solver means
// a new adapter, not a new UI.
class ISolverAdapter
{
public:
    virtual ~ISolverAdapter() = default;

    virtual const SolverCapabilities &capabilities() const = 0;

    // Local validation against this backend's contract (section 9). Does
    // not (and cannot) prove the assembled system is solvable.
    virtual ValidationIssues validate(const ModelDefinition &model) const = 0;

    // Exact backend JSON, with no GUI metadata.
    // Only enabledForAnalysis patches are included; row order is recorded
    // in outPatchRows for provenance.
    virtual QJsonObject buildSolverInput(const ModelDefinition &model,
                                          QVector<PatchRowMapping> *outPatchRows = nullptr) const = 0;
};

} // namespace femgui
