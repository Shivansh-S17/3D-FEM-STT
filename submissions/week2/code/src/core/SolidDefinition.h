#pragma once

#include "core/PlateDefinition.h"

namespace femgui {

// Structured, axis-aligned C3D8 solid in the received solver's source frame.
// X varies fastest, then Z, then Y. Positive source Z runs from the loaded
// z-min face toward the foundation z-max face until author conventions are
// confirmed against the received solver conventions.
struct SolidMaterialProperties
{
    double emod = 24000.0; // proposed MPa
    double nu = 0.3;
};

struct SolidDefinition
{
    QVector<double> xNodes;
    QVector<double> yNodes;
    QVector<double> zNodes;
    SolidMaterialProperties material;
    FoundationProperties foundation = {50.0, 50.0, 50.0};
    QVector<PressurePatch> patches;
    bool reducedIntegration = false; // stored for fidelity; adapter rejects true
    QString coordinateConvention = "source-z-positive-toward-foundation";

    int nodeCountX() const { return xNodes.size(); }
    int nodeCountY() const { return yNodes.size(); }
    int nodeCountZ() const { return zNodes.size(); }
    qint64 nodeCount() const
    {
        return static_cast<qint64>(nodeCountX()) * nodeCountY() * nodeCountZ();
    }
    qint64 elementCount() const
    {
        if (nodeCountX() < 2 || nodeCountY() < 2 || nodeCountZ() < 2) return 0;
        return static_cast<qint64>(nodeCountX() - 1) * (nodeCountY() - 1) * (nodeCountZ() - 1);
    }
    static constexpr int dofsPerNode = 3;
    qint64 dofCount() const { return nodeCount() * dofsPerNode; }
};

} // namespace femgui
