#pragma once

#include <QString>

#include "core/ResultSet.h"

class vtkUnstructuredGrid;

namespace femgui {

// Reads and validates the driver's legacy VTK output before
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

    // expectedNodeCount/expectedElementCount come from the PlateDefinition
    // snapshot the run was launched with; referenceGrid (from
    // PlateMeshBuilder, built from that same snapshot) is used to fully
    // check that node coordinates and connectivity match our own
    // enumeration rather than trusting the file's counts alone.
    static Result importFromFile(const QString &path,
                                  const QString &runId,
                                  int expectedNodeCount,
                                  int expectedElementCount,
                                  vtkUnstructuredGrid *referenceGrid);
};

} // namespace femgui
