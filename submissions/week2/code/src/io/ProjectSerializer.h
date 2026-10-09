#pragma once

#include <QString>

namespace femgui {

class ProjectDocument;

// Reads/writes the GUI's own ".femproject.json" format --
// distinct from the solver's exact input schema, which PlateSolverAdapter
// produces separately via Export Solver JSON.
class ProjectSerializer
{
public:
    struct Result
    {
        bool success = false;
        QString error;
        QString activeRunId;
    };

    static Result save(const ProjectDocument &document, const QString &path);
    // Replaces document's plate/name/description/runs in place on success;
    // document is left untouched on failure.
    static Result load(ProjectDocument &document, const QString &path);
};

} // namespace femgui
