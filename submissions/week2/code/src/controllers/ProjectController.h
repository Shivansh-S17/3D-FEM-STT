#pragma once

#include <QObject>
#include <QString>
#include <memory>

#include "core/ValidationIssue.h"

namespace femgui {

class ProjectDocument;
class AnalysisController;

// New/open/save and validation entry points . Owns the
// document and its analysis controller together and recreates both as a
// pair on New/Open, so nothing in the UI ever holds a stale document
// pointer across a project switch -- listen for projectReplaced().
class ProjectController : public QObject
{
    Q_OBJECT

public:
    struct Result
    {
        bool success = false;
        QString error;
    };

    explicit ProjectController(QObject *parent = nullptr);
    ~ProjectController() override;

    ProjectDocument *document() const { return m_document.get(); }
    AnalysisController *analysis() const { return m_analysis.get(); }

    void newProject();
    Result open(const QString &path);
    Result save();
    Result saveAs(const QString &path);
    Result exportSolverJson(const QString &path) const;
    Result loadRunResults(const QString &runId);

    ValidationIssues validate() const;

signals:
    void projectReplaced();

private:
    void createFreshPair();

    std::unique_ptr<ProjectDocument> m_document;
    std::unique_ptr<AnalysisController> m_analysis;
};

} // namespace femgui
