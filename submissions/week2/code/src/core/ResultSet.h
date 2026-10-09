#pragma once

#include <QString>
#include <QVector>

#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
// vtkSmartPointer<T>'s implicit conversion operator requires T complete
// wherever ResultSet's inline accessors are compiled (MSVC instantiates
// them as soon as the class body is parsed, not deferred to first call),
// so this needs the full header rather than a forward declaration.

namespace femgui {

// Describes one result array as it should be presented, independent of
// whether a particular file actually contains it. Backend capability and
// observed output availability are kept separate.
struct FieldDescriptor
{
    QString arrayName;   // raw VTK array name, e.g. "sxx_top"
    QString label;       // display label, e.g. "Bending stress at top: XX"
    QString unit;        // "mm", "rad", "MPa"
    QString association; // "Node" (all arrays from this backend are point data)
    QString averagingNote; // "" for raw nodal DOFs, else e.g. "Arithmetic average over adjacent elements"
    int componentCount = 1;
    bool isDerived = false; // true for GUI-computed fields like displacement magnitude
};

// The fixed list of fields this backend can ever produce,
// labeled with their units. A loaded file may be
// missing some of these; ResultSet::availableFields() reports what is
// actually present.
const QVector<FieldDescriptor> &plateFieldCatalog();

// An immutable, validated numerical dataset from one completed run.
// Never mutated after import; a new run produces a new ResultSet.
class ResultSet
{
public:
    ResultSet() = default;
    ResultSet(vtkSmartPointer<vtkUnstructuredGrid> grid, QString runId, QString sourcePath);

    bool isValid() const { return m_grid != nullptr; }
    vtkUnstructuredGrid *grid() const { return m_grid; }
    const QString &runId() const { return m_runId; }
    const QString &sourcePath() const { return m_sourcePath; }

    // Fields from plateFieldCatalog() that are actually present with the
    // expected component count and one tuple per node.
    QVector<FieldDescriptor> availableFields() const;

    // Signed min, signed max, and the node id/location of the maximum
    // absolute value (computed independently; do not
    // reuse the driver's own summary_results.txt numbers).
    struct Extrema
    {
        double signedMin = 0.0;
        double signedMax = 0.0;
        double maxAbsolute = 0.0;
        vtkIdType maxAbsoluteNodeId = -1;
        vtkIdType signedMinNodeId = -1;
        vtkIdType signedMaxNodeId = -1;
        bool valid = false;
    };
    Extrema computeExtrema(const QString &arrayName) const;

    // sqrt(u*u+v*v+w*w) per node; empty if u/v/w are not all present.
    QVector<double> computeDisplacementMagnitude() const;

private:
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;
    QString m_runId;
    QString m_sourcePath;
};

} // namespace femgui
