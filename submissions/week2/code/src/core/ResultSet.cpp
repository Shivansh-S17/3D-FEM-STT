#include "core/ResultSet.h"

#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include <cmath>

namespace femgui {

const QVector<FieldDescriptor> &plateFieldCatalog()
{
    static const QVector<FieldDescriptor> catalog = {
        {"w", "Out-of-plane displacement (w)", "mm", "Node", "", 1, false},
        {"u", "In-plane displacement (u)", "mm", "Node", "", 1, false},
        {"v", "In-plane displacement (v)", "mm", "Node", "", 1, false},
        {"theta_x", "Rotation: theta_x", "rad", "Node", "", 1, false},
        {"theta_y", "Rotation: theta_y", "rad", "Node", "", 1, false},
        {"sxx_top", "Bending stress at top: XX", "MPa", "Node", "Arithmetic average over adjacent elements", 1, false},
        {"syy_top", "Bending stress at top: YY", "MPa", "Node", "Arithmetic average over adjacent elements", 1, false},
        {"sxy_top", "Bending stress at top: XY", "MPa", "Node", "Arithmetic average over adjacent elements", 1, false},
        {"sxx_bottom", "Bending stress at bottom: XX", "MPa", "Node", "Arithmetic average over adjacent elements", 1, false},
        {"syy_bottom", "Bending stress at bottom: YY", "MPa", "Node", "Arithmetic average over adjacent elements", 1, false},
        {"sxy_bottom", "Bending stress at bottom: XY", "MPa", "Node", "Arithmetic average over adjacent elements", 1, false},
        {"sxx_membrane", "Membrane stress: XX", "MPa", "Node", "Arithmetic average over adjacent elements", 1, false},
        {"syy_membrane", "Membrane stress: YY", "MPa", "Node", "Arithmetic average over adjacent elements", 1, false},
        {"sxy_membrane", "Membrane stress: XY", "MPa", "Node", "Arithmetic average over adjacent elements", 1, false},
        {"displacement", "Displacement vector (u, v, w)", "mm", "Node", "", 3, false},
        {"__displacement_magnitude", "Displacement magnitude (derived)", "mm", "Node", "GUI-derived: sqrt(u^2+v^2+w^2)", 1, true},
    };
    return catalog;
}

ResultSet::ResultSet(vtkSmartPointer<vtkUnstructuredGrid> grid, QString runId, QString sourcePath)
    : m_grid(std::move(grid))
    , m_runId(std::move(runId))
    , m_sourcePath(std::move(sourcePath))
{
}

QVector<FieldDescriptor> ResultSet::availableFields() const
{
    QVector<FieldDescriptor> result;
    if (!m_grid)
        return result;

    vtkIdType numPoints = m_grid->GetNumberOfPoints();
    vtkPointData *pointData = m_grid->GetPointData();

    for (const FieldDescriptor &field : plateFieldCatalog()) {
        if (field.isDerived) {
            // Available only if u, v, and w are all present.
            if (computeDisplacementMagnitude().size() == numPoints)
                result.append(field);
            continue;
        }
        vtkDataArray *array = pointData->GetArray(field.arrayName.toUtf8().constData());
        if (!array)
            continue;
        if (array->GetNumberOfComponents() != field.componentCount)
            continue;
        if (array->GetNumberOfTuples() != numPoints)
            continue;
        result.append(field);
    }
    return result;
}

ResultSet::Extrema ResultSet::computeExtrema(const QString &arrayName) const
{
    Extrema extrema;
    if (!m_grid)
        return extrema;

    const auto magnitude = arrayName == "__displacement_magnitude" ? computeDisplacementMagnitude() : QVector<double>();
    vtkDataArray *array = m_grid->GetPointData()->GetArray(arrayName.toUtf8().constData());
    const vtkIdType count = m_grid->GetNumberOfPoints();
    if (count == 0 || (magnitude.isEmpty() && (!array || array->GetNumberOfComponents() != 1 || array->GetNumberOfTuples() != count)))
        return extrema;
    auto valueAt = [&](vtkIdType i) { return magnitude.isEmpty() ? array->GetTuple1(i) : magnitude[static_cast<int>(i)]; };
    double signedMin = valueAt(0), signedMax = signedMin, maxAbs = std::abs(signedMin);
    vtkIdType maxAbsId = 0;
    extrema.signedMinNodeId = extrema.signedMaxNodeId = 0;
    for (vtkIdType i = 0; i < count; ++i) {
        const double value = valueAt(i);
        if (!std::isfinite(value)) return Extrema();
        if (value < signedMin) { signedMin = value; extrema.signedMinNodeId = i; }
        if (value > signedMax) { signedMax = value; extrema.signedMaxNodeId = i; }
        if (std::abs(value) > maxAbs) { maxAbs = std::abs(value); maxAbsId = i; }
    }
    extrema.signedMin = signedMin;
    extrema.signedMax = signedMax;
    extrema.maxAbsolute = maxAbs;
    extrema.maxAbsoluteNodeId = maxAbsId;
    extrema.valid = true;
    return extrema;
}

QVector<double> ResultSet::computeDisplacementMagnitude() const
{
    QVector<double> result;
    if (!m_grid)
        return result;

    vtkPointData *pointData = m_grid->GetPointData();
    vtkDataArray *u = pointData->GetArray("u");
    vtkDataArray *v = pointData->GetArray("v");
    vtkDataArray *w = pointData->GetArray("w");
    if (!u || !v || !w)
        return result;
    for (auto *array : {u, v, w})
        if (array->GetNumberOfComponents() != 1 || array->GetNumberOfTuples() != m_grid->GetNumberOfPoints())
            return result;

    vtkIdType count = u->GetNumberOfTuples();
    result.resize(static_cast<int>(count));
    for (vtkIdType i = 0; i < count; ++i) {
        double uu = u->GetTuple1(i);
        double vv = v->GetTuple1(i);
        double ww = w->GetTuple1(i);
        result[static_cast<int>(i)] = std::hypot(uu, vv, ww);
    }
    return result;
}

} // namespace femgui
