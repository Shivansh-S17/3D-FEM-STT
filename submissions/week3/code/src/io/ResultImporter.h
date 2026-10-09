#pragma once

#include <QString>

#include "core/ResultSet.h"

class vtkUnstructuredGrid;

namespace femgui {

// Reads and validates the driver's legacy VTK output (section 11.1) before
// it is ever exposed as a successful run's results. An incomplete file
// from this known backend must fail import, not open partially.
class ResultImporter
{
public:
    struct Result
    {
        bool success = false;
        QString error;
        ResultSet resultSet;
    };

    // Expected counts and referenceGrid come from the typed run snapshot and
    // its matching mesh builder. They are used to fully
    // check that node coordinates and connectivity match our own
    // enumeration rather than trusting the file's counts alone.
    static Result importFromFile(const QString &path,
                                  const QString &runId,
                                  int expectedNodeCount,
                                  int expectedElementCount,
                                  vtkUnstructuredGrid *referenceGrid,
                                  ModelKind kind = ModelKind::Plate);
};

} // namespace femgui
