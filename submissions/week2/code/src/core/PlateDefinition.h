#pragma once

#include <QString>
#include <QVector>
#include <QUuid>

namespace femgui {

// Physical inputs for the plate-on-elastic-foundation solver. Coordinates
// are the authoritative geometry (mm); a uniform-grid generator only ever
// fills these arrays, it is never a second geometry representation.
// Solver input mapping is defined by PlateSolverAdapter.
struct SlabProperties
{
    double emod = 30000.0; // MPa
    double nu = 0.2;       // dimensionless
    double thickness = 200.0; // mm
};

struct FoundationProperties
{
    double kx = 0.0;
    double ky = 0.0;
    double kz = 0.0;
};

// One axis-aligned uniform pressure rectangle. All enabled patches enter
// the same force vector (they are not independent load cases).
struct PressurePatch
{
    QUuid id = QUuid::createUuid();
    QString name;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    double q = 0.0; // MPa, must be >= 0 for this backend
    bool enabledForAnalysis = true;
    bool visible = true;

    double width() const { return x2 - x1; }
    double height() const { return y2 - y1; }
    double area() const { return width() * height(); }
    double equivalentForce() const { return q * area(); }
};

// Ordered, strictly increasing grid-line coordinates plus uniform slab and
// foundation properties. modelKind is fixed to "rectangular_plate_5dof" for
// this backend .
struct PlateDefinition
{
    QVector<double> xNodes; // mm, strictly increasing
    QVector<double> yNodes; // mm, strictly increasing
    SlabProperties slab;
    FoundationProperties foundation;
    QVector<PressurePatch> patches;

    int nodeCountX() const { return xNodes.size(); }
    int nodeCountY() const { return yNodes.size(); }
    qint64 nodeCount() const { return static_cast<qint64>(nodeCountX()) * nodeCountY(); }
    qint64 elementCount() const
    {
        if (nodeCountX() < 2 || nodeCountY() < 2)
            return 0;
        return static_cast<qint64>(nodeCountX() - 1) * (nodeCountY() - 1);
    }
    static constexpr int dofsPerNode = 5;
    long long dofCount() const { return static_cast<long long>(nodeCount()) * dofsPerNode; }
};

} // namespace femgui
