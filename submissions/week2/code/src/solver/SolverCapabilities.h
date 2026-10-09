#pragma once

#include <QString>

namespace femgui {

// Static capability profile for one registered solver backend .
// Kept separate from what a particular result file actually contains --
// see ResultSet::availableFields() for that.
struct SolverCapabilities
{
    QString backendId;
    QString modelKind;
    QString displayName;

    bool supportsSupports = false;   // bc_dofs is hardcoded empty in this backend
    bool supportsStrain = false;
    bool supportsReactions = false;
    bool supportsTransient = false;  // "Time Step" text does not imply a transient formulation
    int dofsPerNode = 0;
    int nodesPerElement = 0;
};

} // namespace femgui
