#pragma once

#include <QString>

namespace femgui {

class ProjectDocument;
struct SolidDefinition;

// Reads/writes the GUI's own ".femproject.json" format (section 4.3) --
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
    // Reads the strict C3D8 solver input schema without changing a project.
    static Result loadC3D8SolverInput(SolidDefinition &solid, const QString &path);
};

} // namespace femgui
