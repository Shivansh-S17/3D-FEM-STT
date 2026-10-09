#include "solver/C3D8SolverAdapter.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <limits>

namespace femgui {
namespace {

bool finite(double value) { return std::isfinite(value); }

QJsonArray numbers(const QVector<double> &values)
{
    QJsonArray result;
    for (double value : values) result.append(value);
    return result;
}

bool safeCounts(const SolidDefinition &solid)
{
    const qint64 maximum = std::numeric_limits<qint64>::max();
    qint64 value = solid.nodeCountX();
    if (solid.nodeCountY() && value > maximum / solid.nodeCountY()) return false;
    value *= solid.nodeCountY();
    if (solid.nodeCountZ() && value > maximum / solid.nodeCountZ()) return false;
    value *= solid.nodeCountZ();
    return value <= maximum / SolidDefinition::dofsPerNode;
}

} // namespace

const SolverCapabilities &C3D8SolverAdapter::capabilities() const
{
    static const SolverCapabilities caps{"c3d8-python-v1", "structured_solid_c3d8",
        "Structured C3D8 solid (Python reference solver)", false, true, false, false, 3, 8};
    return caps;
}

ValidationIssues C3D8SolverAdapter::validate(const ModelDefinition &model) const
{
    if (const auto *solid = std::get_if<SolidDefinition>(&model)) return validate(*solid);
    return {{Severity::Error, "The C3D8 backend cannot analyze a plate model.", "backend", {}, {}}};
}

ValidationIssues C3D8SolverAdapter::validate(const SolidDefinition &solid) const
{
    ValidationIssues issues;
    auto axis = [&](const QVector<double> &values, const QString &key, const QString &label) {
        if (values.size() < 2) {
            issues.append({Severity::Error, label + " coordinates need at least two values.", key, {}, {}});
            return;
        }
        for (int i = 0; i < values.size(); ++i) {
            if (!finite(values[i])) {
                issues.append({Severity::Error, QString("%1 coordinate at row %2 is not finite.").arg(label).arg(i + 1), key, {}, {}});
                return;
            }
            if (i && (!(values[i] > values[i - 1]) || !finite(values[i] - values[i - 1]))) {
                issues.append({Severity::Error, label + " coordinates must be strictly increasing with finite spacing.", key, {}, {}});
                return;
            }
        }
    };
    axis(solid.xNodes, "coordinates.x", "X");
    axis(solid.yNodes, "coordinates.y", "Y");
    axis(solid.zNodes, "coordinates.z", "Z");
    if (!safeCounts(solid))
        issues.append({Severity::Error, "Solid node/DOF counts exceed safe 64-bit limits.", "mesh.size", {}, {}});
    else if (solid.nodeCount() > 1000000)
        issues.append({Severity::Error, "This version supports C3D8 previews up to one million nodes.", "mesh.size", {}, {}});
    if (!finite(solid.material.emod) || solid.material.emod <= 0)
        issues.append({Severity::Error, "Young's modulus must be finite and greater than zero.", "material.emod", {}, {}});
    if (!finite(solid.material.nu) || solid.material.nu <= -1 || solid.material.nu >= 0.5)
        issues.append({Severity::Error, "Poisson's ratio must be finite and strictly between -1 and 0.5.", "material.nu", {}, {}});
    for (const auto &entry : {std::pair{"Kx", solid.foundation.kx}, {"Ky", solid.foundation.ky}, {"Kz", solid.foundation.kz}})
        if (!finite(entry.second) || entry.second <= 0)
            issues.append({Severity::Error, QString("%1 must be finite and greater than zero for the qualified free solid.").arg(entry.first),
                           "foundation." + QString(entry.first).toLower(), {}, {}});
    if (solid.reducedIntegration)
        issues.append({Severity::Error, "Reduced integration is unsupported: the received formulation has unresolved scaling and zero-energy modes.",
                       "options.reducedIntegration", {}, "Use full integration."});
    if (solid.coordinateConvention != "source-z-positive-toward-foundation")
        issues.append({Severity::Error, "Unsupported C3D8 coordinate convention.", "coordinates.convention", {}, {}});

    const bool boundsAvailable = solid.xNodes.size() >= 2 && solid.yNodes.size() >= 2;
    for (int i = 0; i < solid.patches.size(); ++i) {
        const auto &patch = solid.patches[i];
        if (!finite(patch.x1) || !finite(patch.x2) || !finite(patch.y1) || !finite(patch.y2) || !finite(patch.q) ||
            !(patch.x2 > patch.x1) || !(patch.y2 > patch.y1) || patch.q < 0) {
            issues.append({Severity::Error, QString("Pressure patch %1 requires finite ordered bounds and nonnegative pressure.").arg(i + 1),
                           "loads", patch.id, {}});
        } else if (boundsAvailable && (patch.x1 < solid.xNodes.first() || patch.x2 > solid.xNodes.last() ||
                                       patch.y1 < solid.yNodes.first() || patch.y2 > solid.yNodes.last())) {
            issues.append({Severity::Error, QString("Pressure patch %1 lies outside the solid X/Y domain.").arg(i + 1),
                           "loads", patch.id, {}});
        }
    }
    return issues;
}

QJsonObject C3D8SolverAdapter::buildSolverInput(const ModelDefinition &model, QVector<PatchRowMapping> *rows) const
{
    if (const auto *solid = std::get_if<SolidDefinition>(&model)) return buildSolverInput(*solid, rows);
    return {};
}

QJsonObject C3D8SolverAdapter::buildSolverInput(const SolidDefinition &solid, QVector<PatchRowMapping> *rows) const
{
    if (rows) rows->clear();
    QJsonArray loads;
    for (int i = 0; i < solid.patches.size(); ++i) {
        const auto &patch = solid.patches[i];
        if (!patch.enabledForAnalysis || patch.q == 0) continue;
        loads.append(QJsonObject{{"q", patch.q}, {"x1", patch.x1}, {"x2", patch.x2},
                                 {"y1", patch.y1}, {"y2", patch.y2}});
        if (rows) rows->append({patch.id, static_cast<int>(loads.size() - 1)});
    }
    return {
        {"nodes", QJsonObject{{"x", numbers(solid.xNodes)}, {"y", numbers(solid.yNodes)}, {"z", numbers(solid.zNodes)}}},
        {"material", QJsonObject{{"Emod", solid.material.emod}, {"nu", solid.material.nu}}},
        {"subgrade", QJsonObject{{"Kx", solid.foundation.kx}, {"Ky", solid.foundation.ky}, {"Kz", solid.foundation.kz}}},
        {"loads", loads},
        {"options", QJsonObject{{"reduced_integration", solid.reducedIntegration}}}
    };
}

} // namespace femgui
