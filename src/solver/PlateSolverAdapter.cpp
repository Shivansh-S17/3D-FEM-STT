#include "solver/PlateSolverAdapter.h"

#include <QJsonArray>

#include <cmath>
#include <limits>

namespace femgui {

namespace {

bool isFinite(double value)
{
    return std::isfinite(value);
}

// Dense solve memory floor from section 15.2: one double matrix alone is
// 8*N*N bytes, excluding factorization workspace and copies.
constexpr qint64 kLargestMeasuredNodeCount = 1089; // 33 x 33; see numerical qualification report
constexpr double kDenseErrorBytes = 1.0 * 1024 * 1024 * 1024; // workspace/copies need additional memory

} // namespace

const SolverCapabilities &PlateSolverAdapter::capabilities() const
{
    static const SolverCapabilities caps{"plate-python-v1", "rectangular_plate_5dof",
        "Plate on elastic foundation (Python reference solver)", false, false, false, false, 5, 4};
    return caps;
}

ValidationIssues PlateSolverAdapter::validate(const ModelDefinition &model) const
{
    if (const auto *plate = std::get_if<PlateDefinition>(&model)) return validate(*plate);
    return {{Severity::Error, "The plate backend cannot analyze a C3D8 solid model.", "backend", {}, {}}};
}

QJsonObject PlateSolverAdapter::buildSolverInput(const ModelDefinition &model, QVector<PatchRowMapping> *rows) const
{
    if (const auto *plate = std::get_if<PlateDefinition>(&model)) return buildSolverInput(*plate, rows);
    return {};
}

ValidationIssues PlateSolverAdapter::validate(const PlateDefinition &plate) const
{
    ValidationIssues issues;

    // --- Coordinates ---
    auto checkAxis = [&](const QVector<double> &values, const QString &axisKey, const QString &axisName) {
        if (values.size() < 2) {
            issues.append({Severity::Error,
                            QString("%1 coordinates need at least two values.").arg(axisName),
                            axisKey, {}, "Add another grid line."});
            return;
        }
        for (int i = 0; i < values.size(); ++i) {
            if (!isFinite(values[i])) {
                issues.append({Severity::Error,
                                QString("%1 coordinate at row %2 is not a finite number.").arg(axisName).arg(i + 1),
                                axisKey, {}, {}});
                return;
            }
        }
        for (int i = 1; i < values.size(); ++i) {
            if (values[i] <= values[i - 1]) {
                issues.append({Severity::Error,
                                QString("%1 coordinates must be strictly increasing; row %2 (%3) is not greater than row %4 (%5).")
                                    .arg(axisName).arg(i + 1).arg(values[i]).arg(i).arg(values[i - 1]),
                                axisKey, {}, "Sort and de-duplicate the coordinate list."});
                return;
            }
        }
    };
    checkAxis(plate.xNodes, "coordinates.x", "X");
    checkAxis(plate.yNodes, "coordinates.y", "Y");

    // --- Mesh size (only meaningful once coordinates are valid) ---
    if (plate.xNodes.size() >= 2 && plate.yNodes.size() >= 2) {
        double dofs = static_cast<double>(plate.dofCount());
        double denseBytes = 8.0 * dofs * dofs;
        if (denseBytes > kDenseErrorBytes) {
            issues.append({Severity::Error,
                            QString("This mesh needs an estimated %1 GiB for one dense stiffness matrix, "
                                    "which exceeds the conservative allocation limit for this backend's dense solver (not a benchmarked limit).")
                                .arg(denseBytes / (1024.0 * 1024 * 1024), 0, 'f', 1),
                            "mesh.size", {}, "Use a coarser grid."});
        } else if (plate.nodeCount() > kLargestMeasuredNodeCount) {
            issues.append({Severity::Warning,
                            QString("This mesh has %1 nodes, exceeding the largest measured case (1089 nodes). "
                                    "Runtime and memory use at this size are unqualified.").arg(plate.nodeCount()),
                            "mesh.size", {}, {}});
        }
    }

    // --- Material ---
    if (!isFinite(plate.slab.emod) || plate.slab.emod <= 0.0)
        issues.append({Severity::Error, "Young's modulus (Emod) must be a finite number greater than zero.", "slab.emod", {}, {}});
    if (!isFinite(plate.slab.thickness) || plate.slab.thickness <= 0.0)
        issues.append({Severity::Error, "Thickness (t) must be a finite number greater than zero.", "slab.t", {}, {}});
    if (!isFinite(plate.slab.nu) || plate.slab.nu <= -1.0 || plate.slab.nu >= 0.5) {
        issues.append({Severity::Error,
                        "Poisson's ratio (nu) must be a finite number strictly between -1 and 0.5 "
                        "(provisional isotropic bound; refine once the element's validity range is confirmed).",
                        "slab.nu", {}, {}});
    }

    // --- Foundation ---
    bool foundationFinite = isFinite(plate.foundation.kx) && isFinite(plate.foundation.ky) && isFinite(plate.foundation.kz);
    if (!foundationFinite) {
        issues.append({Severity::Error, "Foundation coefficients (Kx, Ky, Kz) must be finite numbers.", "foundation", {}, {}});
    } else if (plate.foundation.kx == 0.0 && plate.foundation.ky == 0.0 && plate.foundation.kz == 0.0) {
        issues.append({Severity::Error,
                        "Zero foundation without supports is unsupported. This backend defines no support "
                        "constraints; configure the foundation before running.",
                        "foundation", {}, {}});
    }

    // --- Pressure patches ---
    const auto enabledPatches = [&plate] {
        QVector<const PressurePatch *> out;
        for (const auto &patch : plate.patches)
            if (patch.enabledForAnalysis)
                out.append(&patch);
        return out;
    }();

    if (enabledPatches.isEmpty()) {
        issues.append({Severity::Error, "At least one pressure patch must be enabled for analysis.", "loads", {}, {}});
    }

    bool allZeroQ = !enabledPatches.isEmpty();
    double domainXMin = plate.xNodes.isEmpty() ? 0.0 : plate.xNodes.first();
    double domainXMax = plate.xNodes.isEmpty() ? 0.0 : plate.xNodes.last();
    double domainYMin = plate.yNodes.isEmpty() ? 0.0 : plate.yNodes.first();
    double domainYMax = plate.yNodes.isEmpty() ? 0.0 : plate.yNodes.last();

    for (const PressurePatch *patch : enabledPatches) {
        const QString fieldPrefix = QString("loads.%1").arg(patch->id.toString());
        if (!isFinite(patch->x1) || !isFinite(patch->x2) || !isFinite(patch->y1) || !isFinite(patch->y2) || !isFinite(patch->q)) {
            issues.append({Severity::Error, QString("Patch '%1' has a non-finite bound or pressure.").arg(patch->name),
                            fieldPrefix, patch->id, {}});
            continue;
        }
        if (patch->x2 <= patch->x1)
            issues.append({Severity::Error, QString("Patch '%1': x2 must be greater than x1.").arg(patch->name), fieldPrefix + ".x2", patch->id, {}});
        if (patch->y2 <= patch->y1)
            issues.append({Severity::Error, QString("Patch '%1': y2 must be greater than y1.").arg(patch->name), fieldPrefix + ".y2", patch->id, {}});
        if (patch->q < 0.0)
            issues.append({Severity::Error, QString("Patch '%1': pressure q must be non-negative for this backend.").arg(patch->name), fieldPrefix + ".q", patch->id, {}});
        if (patch->q != 0.0)
            allZeroQ = false;

        if (!plate.xNodes.isEmpty() && !plate.yNodes.isEmpty() &&
            (patch->x1 < domainXMin || patch->x2 > domainXMax || patch->y1 < domainYMin || patch->y2 > domainYMax)) {
            issues.append({Severity::Error,
                            QString("Patch '%1' extends outside the model bounds [%2, %3] x [%4, %5]; "
                                    "it will not be silently clamped.")
                                .arg(patch->name).arg(domainXMin).arg(domainXMax).arg(domainYMin).arg(domainYMax),
                            fieldPrefix, patch->id, {}});
        }
        if (!isFinite(patch->area()) || !isFinite(patch->equivalentForce()))
            issues.append({Severity::Error, QString("Patch '%1' area or resultant overflows.").arg(patch->name), fieldPrefix, patch->id, {}});
    }

    if (allZeroQ) {
        issues.append({Severity::Warning, "All enabled patches have pressure q = 0; the analysis will solve for zero load.",
                        "loads", {}, {}});
    }

    return issues;
}

QJsonObject PlateSolverAdapter::buildSolverInput(const PlateDefinition &plate, QVector<PatchRowMapping> *outPatchRows) const
{
    QJsonArray xArray, yArray;
    for (double x : plate.xNodes)
        xArray.append(x);
    for (double y : plate.yNodes)
        yArray.append(y);

    QJsonObject nodes;
    nodes["x"] = xArray;
    nodes["y"] = yArray;

    QJsonObject slab;
    slab["Emod"] = plate.slab.emod;
    slab["nu"] = plate.slab.nu;
    slab["t"] = plate.slab.thickness;

    QJsonObject subgrade;
    subgrade["Kx"] = plate.foundation.kx;
    subgrade["Ky"] = plate.foundation.ky;
    subgrade["Kz"] = plate.foundation.kz;

    QJsonArray x1, x2, y1, y2, q;
    if (outPatchRows)
        outPatchRows->clear();
    int row = 0;
    for (const PressurePatch &patch : plate.patches) {
        if (!patch.enabledForAnalysis)
            continue;
        x1.append(patch.x1);
        x2.append(patch.x2);
        y1.append(patch.y1);
        y2.append(patch.y2);
        q.append(patch.q);
        if (outPatchRows)
            outPatchRows->append({patch.id, row});
        ++row;
    }
    QJsonObject loads;
    loads["x1"] = x1;
    loads["x2"] = x2;
    loads["y1"] = y1;
    loads["y2"] = y2;
    loads["q"] = q;

    QJsonObject root;
    root["nodes"] = nodes;
    root["slab"] = slab;
    root["subgrade"] = subgrade;
    root["loads"] = loads;
    return root;
}

} // namespace femgui
