#include "io/ResultImporter.h"

#include <vtkCallbackCommand.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnstructuredGridReader.h>

#include <QFileInfo>

#include <cmath>

#include "core/ResultSet.h"

namespace femgui {

namespace {

void captureVtkError(vtkObject *, unsigned long, void *clientData, void *callData)
{
    auto *out = static_cast<QString *>(clientData);
    if (callData)
        *out += QString::fromUtf8(static_cast<const char *>(callData)) + "\n";
}

bool arrayIsFinite(vtkDataArray *array)
{
    vtkIdType n = array->GetNumberOfTuples();
    int comps = array->GetNumberOfComponents();
    for (vtkIdType i = 0; i < n; ++i) {
        for (int c = 0; c < comps; ++c) {
            if (!std::isfinite(array->GetComponent(i, c)))
                return false;
        }
    }
    return true;
}

bool connectivityMatches(vtkUnstructuredGrid *a, vtkUnstructuredGrid *b)
{
    if (!a || !b)
        return false;
    if (a->GetNumberOfCells() != b->GetNumberOfCells())
        return false;
    if (a->GetNumberOfPoints() != b->GetNumberOfPoints()) return false;
    for (vtkIdType i = 0; i < a->GetNumberOfPoints(); ++i) {
        double pa[3], pb[3];
        a->GetPoint(i, pa);
        b->GetPoint(i, pb);
        for (int d = 0; d < 3; ++d)
            if (!std::isfinite(pa[d]) || std::abs(pa[d] - pb[d]) >
                1e-6 * std::max(1.0, std::abs(pb[d])) + 1e-8) return false;
    }
    vtkIdType numCells = a->GetNumberOfCells();
    for (vtkIdType c = 0; c < numCells; ++c) {
        vtkIdList *pointsA = a->GetCell(c)->GetPointIds();
        vtkIdList *pointsB = b->GetCell(c)->GetPointIds();
        if (pointsA->GetNumberOfIds() != pointsB->GetNumberOfIds())
            return false;
        for (vtkIdType k = 0; k < pointsA->GetNumberOfIds(); ++k) {
            if (pointsA->GetId(k) != pointsB->GetId(k)) return false;
            double pa[3], pb[3];
            a->GetPoint(pointsA->GetId(k), pa);
            b->GetPoint(pointsB->GetId(k), pb);
            for (int d = 0; d < 3; ++d) {
                if (std::abs(pa[d] - pb[d]) > 1e-6 * std::max(1.0, std::abs(pb[d])) + 1e-9)
                    return false;
            }
        }
    }
    return true;
}

} // namespace

ResultImporter::Result ResultImporter::importFromFile(const QString &path,
                                                        const QString &runId,
                                                        int expectedNodeCount,
                                                        int expectedElementCount,
                                                        vtkUnstructuredGrid *referenceGrid,
                                                        ModelKind kind)
{
    Result result;

    if (!QFileInfo::exists(path)) {
        result.error = QString("Result file does not exist: %1").arg(path);
        return result;
    }

    QString vtkErrors;
    vtkNew<vtkCallbackCommand> errorCallback;
    errorCallback->SetClientData(&vtkErrors);
    errorCallback->SetCallback(captureVtkError);

    vtkNew<vtkUnstructuredGridReader> reader;
    reader->AddObserver(vtkCommand::ErrorEvent, errorCallback);
    reader->SetFileName(path.toUtf8().constData());
    reader->ReadAllScalarsOn();
    reader->ReadAllVectorsOn();
    reader->Update();

    if (!vtkErrors.isEmpty()) {
        result.error = QString("VTK reported an error reading '%1': %2").arg(path, vtkErrors);
        return result;
    }

    vtkUnstructuredGrid *grid = reader->GetOutput();
    if (!grid || grid->GetNumberOfPoints() == 0 || grid->GetNumberOfCells() == 0) {
        result.error = QString("'%1' parsed but contains no points or cells.").arg(path);
        return result;
    }
    for (vtkIdType p = 0; p < grid->GetNumberOfPoints(); ++p) {
        double xyz[3];
        grid->GetPoint(p, xyz);
        if (!std::isfinite(xyz[0]) || !std::isfinite(xyz[1]) || !std::isfinite(xyz[2])) {
            result.error = "Result geometry contains non-finite coordinates.";
            return result;
        }
    }

    if (expectedNodeCount > 0 && grid->GetNumberOfPoints() != expectedNodeCount) {
        result.error = QString("'%1' has %2 nodes; expected %3 for this run's mesh.")
                            .arg(path).arg(grid->GetNumberOfPoints()).arg(expectedNodeCount);
        return result;
    }
    if (expectedElementCount > 0 && grid->GetNumberOfCells() != expectedElementCount) {
        result.error = QString("'%1' has %2 cells; expected %3 for this run's mesh.")
                            .arg(path).arg(grid->GetNumberOfCells()).arg(expectedElementCount);
        return result;
    }

    for (vtkIdType c = 0; c < grid->GetNumberOfCells(); ++c) {
        const int requiredType = kind == ModelKind::Plate ? VTK_QUAD : VTK_HEXAHEDRON;
        const int requiredNodes = kind == ModelKind::Plate ? 4 : 8;
        if (grid->GetCellType(c) != requiredType) {
            result.error = QString("'%1' contains the wrong cell type at index %2 for this backend.").arg(path).arg(c);
            return result;
        }
        vtkNew<vtkIdList> ids;
        grid->GetCellPoints(c, ids);
        if (ids->GetNumberOfIds() != requiredNodes) {
            result.error = QString("Result cell must contain exactly %1 node IDs.").arg(requiredNodes);
            return result;
        }
        for (vtkIdType i = 0; i < requiredNodes; ++i) {
            if (ids->GetId(i) < 0 || ids->GetId(i) >= grid->GetNumberOfPoints()) {
                result.error = "Result connectivity contains an invalid node ID.";
                return result;
            }
            for (vtkIdType j = 0; j < i; ++j)
                if (ids->GetId(i) == ids->GetId(j)) {
                    result.error = "Result quad contains repeated node IDs.";
                    return result;
                }
        }
    }

    if (referenceGrid && !connectivityMatches(grid, referenceGrid)) {
        result.error = QString("'%1' geometry does not match the mesh built from this run's input snapshot "
                                "(coordinates or connectivity differ). Refusing to associate it with this run.")
                            .arg(path);
        return result;
    }

    // Every field required by the selected backend must have its declared
    // component count and one tuple per node.
    vtkPointData *pointData = grid->GetPointData();
    vtkIdType numPoints = grid->GetNumberOfPoints();
    for (const FieldDescriptor &field : fieldCatalog(kind)) {
        if (field.isDerived)
            continue;
        vtkDataArray *array = pointData->GetArray(field.arrayName.toUtf8().constData());
        if (!array) {
            result.error = QString("Required result array '%1' is missing.").arg(field.arrayName);
            return result;
        }
        if (array->GetNumberOfComponents() != field.componentCount) {
            result.error = QString("Array '%1' in '%2' has %3 components; expected %4.")
                                .arg(field.arrayName, path).arg(array->GetNumberOfComponents()).arg(field.componentCount);
            return result;
        }
        if (array->GetNumberOfTuples() != numPoints) {
            result.error = QString("Array '%1' in '%2' has %3 tuples; expected one per node (%4).")
                                .arg(field.arrayName, path).arg(array->GetNumberOfTuples()).arg(numPoints);
            return result;
        }
        if (!arrayIsFinite(array)) {
            result.error = QString("Array '%1' in '%2' contains a non-finite value.").arg(field.arrayName, path);
            return result;
        }
    }
    vtkDataArray *vectors = pointData->GetArray("displacement");
    const char *components[3] = {"u", "v", "w"};
    for (vtkIdType i = 0; i < numPoints; ++i) {
        for (int c = 0; c < 3; ++c) {
            double expected = pointData->GetArray(components[c])->GetTuple1(i);
            if (std::abs(vectors->GetComponent(i, c) - expected) > 1e-6 * std::abs(expected) + 1e-10) {
                result.error = "Displacement vector does not match scalar u/v/w components.";
                return result;
            }
        }
    }
    for (auto association : {0, 1}) {
        auto *attributes = association == 0 ? static_cast<vtkDataSetAttributes *>(grid->GetPointData())
                                            : static_cast<vtkDataSetAttributes *>(grid->GetCellData());
        const auto count = association == 0 ? numPoints : grid->GetNumberOfCells();
        if (auto *ids = attributes->GetArray(association == 0 ? "solver_node_id" : "solver_element_id")) {
            if (ids->GetNumberOfComponents() != 1 || ids->GetNumberOfTuples() != count) {
                result.error = "Malformed solver identity array.";
                return result;
            }
            for (vtkIdType i = 0; i < count; ++i)
                if (ids->GetTuple1(i) != static_cast<double>(i)) {
                    result.error = "Solver IDs do not match this backend's enumeration.";
                    return result;
                }
        }
    }

    // The driver's own writer enumerates points/cells 0..N-1 in exactly the
    // order mesh_plate_enumx does (verified against PlateMeshBuilder above
    // via connectivityMatches), so the dataset-local index *is* the solver
    // id here. Synthesize the same identity arrays PlateMeshBuilder adds,
    // so picking never has to special-case where a mesh came from.
    if (!grid->GetPointData()->GetArray("solver_node_id")) {
        vtkNew<vtkIdTypeArray> nodeIds;
        nodeIds->SetName("solver_node_id");
        nodeIds->SetNumberOfComponents(1);
        nodeIds->SetNumberOfTuples(numPoints);
        for (vtkIdType i = 0; i < numPoints; ++i)
            nodeIds->SetTypedComponent(i, 0, i);
        grid->GetPointData()->AddArray(nodeIds);
    }
    if (!grid->GetCellData()->GetArray("solver_element_id")) {
        vtkIdType numCells = grid->GetNumberOfCells();
        vtkNew<vtkIdTypeArray> elementIds;
        elementIds->SetName("solver_element_id");
        elementIds->SetNumberOfComponents(1);
        elementIds->SetNumberOfTuples(numCells);
        for (vtkIdType i = 0; i < numCells; ++i)
            elementIds->SetTypedComponent(i, 0, i);
        grid->GetCellData()->AddArray(elementIds);
    }

    result.success = true;
    result.resultSet = ResultSet(vtkSmartPointer<vtkUnstructuredGrid>(grid), runId, path, kind);
    return result;
}

} // namespace femgui
