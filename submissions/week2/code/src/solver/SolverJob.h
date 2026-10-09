#pragma once

#include <QFile>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

#include "core/RunRecord.h"

namespace femgui {

// Owns one solver process. The production entry point uses JSONL events,
// cooperative cancellation, and a verified result manifest; other drivers
// retain the compatibility launch path. Numerical VTK validation follows
// in AnalysisController before any result is attached to the project.
class SolverJob : public QObject
{
    Q_OBJECT

public:
    explicit SolverJob(QObject *parent = nullptr);
    ~SolverJob() override;

    // pythonExecutable: absolute path to python.exe.
    // driverPath: absolute path to plate_solver.py (production) or a legacy driver.
    // runDirectory: fresh, per-run directory; becomes the process's cwd, so
    //   the driver's hardcoded "output_python_square" lands isolated inside it.
    // inputJsonPath: absolute path to this run's input.json.
    void start(const QString &pythonExecutable, const QString &driverPath,
               const QString &runDirectory, const QString &inputJsonPath);

    // Cooperative-then-forceful stop : terminate(), then
    // kill() after a grace period if the process hasn't exited. On Windows,
    // QProcess::terminate() may not stop a console program, so the kill
    // fallback is not optional.
    void cancel();

    RunStatus status() const { return m_status; }
    int exitCode() const { return m_exitCode; }
    QString resultVtkPath() const; // valid only after a successful finish

signals:
    void statusChanged(RunStatus status);
    void logLine(QString channel, QString line); // channel: "stdout" or "stderr"
    void phaseChanged(QString phase, int completed, int total); // -1: indeterminate
    void finished(bool success, QString resultVtkPath, QString errorMessage);

private:
    void setStatus(RunStatus status);
    void handleReadyReadStdOut();
    void handleReadyReadStdErr();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessErrorOccurred(QProcess::ProcessError error);
    void emitBufferedLines(QByteArray &buffer, const QString &channel, bool flush = false);
    void consumeLine(const QByteArray &line, const QString &channel);
    bool validateManifest() const;

    QProcess *m_process = nullptr;
    QTimer *m_killGraceTimer = nullptr;
    RunStatus m_status = RunStatus::Idle;
    QString m_runDirectory;
    bool m_cancelRequested = false;
    QFile m_stdoutLog;
    QFile m_stderrLog;
    QByteArray m_stdoutBuffer, m_stderrBuffer;
    int m_exitCode = -1;
    bool m_finished = false;
    bool m_structured = false;
    bool m_completionReceived = false;
    QString m_protocolError;
    QString m_backendError;
    QString m_inputSha256;
};

} // namespace femgui
