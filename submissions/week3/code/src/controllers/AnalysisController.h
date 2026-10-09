#pragma once

#include <QObject>
#include <QString>

#include "core/ValidationIssue.h"
#include "core/ModelDefinition.h"

namespace femgui {

class ProjectDocument;
class SolverJob;
struct RunRecord;

// Wraps the active model's solver adapter + SolverJob into the Run/Stop workflow
// (sections 4.2, 10). Only one job may run at a time in this first
// implementation (section 4.4); physical editors are expected to be locked
// by the caller while isRunning() is true.
class AnalysisController : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisController(ProjectDocument *document, QObject *parent = nullptr);

    QString pythonExecutable() const;
    void setPythonExecutable(const QString &path);
    QString driverPath() const;
    void setDriverPath(const QString &path);

    // Best-effort defaults for a dev checkout; always overridable in Settings.
    static QString autodetectPythonExecutable();
    static QString autodetectDriverPath(ModelKind kind = ModelKind::Plate);

    struct BackendCheck
    {
        bool ok = false;
        QString message;
    };
    void checkBackend();
    BackendCheck backendFilesCheck() const;

    bool isRunning() const { return m_currentJob != nullptr; }

    // Revalidates the exact current snapshot and starts a run if valid.
    // Returns the validation issues either way (empty on a started run).
    ValidationIssues run();
    void stop();

    // MainWindow determines the *true* final status (Completed vs. Failed)
    // only after importing and validating results.vtk, which happens after
    // this controller's own SolverJob::finished handler already wrote
    // run.json once. Call this again once that final status is known so
    // the manifest on disk doesn't freeze on an intermediate state.
    void refreshRunManifest(const QString &runId) const;

signals:
    void backendChecked(bool ok, QString message);
    void backendConfigurationChanged();
    void logLine(QString channel, QString line);
    void phaseChanged(QString phase, int completed, int total);
    void runStarted(QString runId);
    void runFinished(QString runId, bool success, QString errorMessage);

private:
    QString runsRootDirectory() const;
    void writeRunManifest(const QString &runDirectory, const RunRecord &run) const;
    ModelKind activeModelKind() const;
    QString driverSettingKey() const;

    ProjectDocument *m_document;
    SolverJob *m_currentJob = nullptr;
    QString m_currentRunId;
    QString m_pythonExecutable;
    QString m_driverPath;
    bool m_checkingBackend = false;
    bool m_cancelLoading = false;
};

} // namespace femgui
