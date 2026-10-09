#include "solver/SolverJob.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace femgui {

namespace {
constexpr int kKillGraceMs = 3000;
}

SolverJob::SolverJob(QObject *parent)
    : QObject(parent)
{
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::started, this, [this] {
        if (m_cancelRequested) { m_process->kill(); return; }
        setStatus(RunStatus::Running);
    });
    connect(m_process, &QProcess::readyReadStandardOutput, this, &SolverJob::handleReadyReadStdOut);
    connect(m_process, &QProcess::readyReadStandardError, this, &SolverJob::handleReadyReadStdErr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SolverJob::handleProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &SolverJob::handleProcessErrorOccurred);

    m_killGraceTimer = new QTimer(this);
    m_killGraceTimer->setSingleShot(true);
    m_killGraceTimer->setInterval(kKillGraceMs);
    connect(m_killGraceTimer, &QTimer::timeout, this, [this] {
        if (m_process->state() != QProcess::NotRunning)
            m_process->kill();
    });
}

SolverJob::~SolverJob()
{
    // Normal close waits asynchronously; this is a last-resort ownership guard.
    m_process->disconnect(this);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

void SolverJob::setStatus(RunStatus status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged(status);
}

void SolverJob::start(const QString &pythonExecutable, const QString &driverPath,
                       const QString &runDirectory, const QString &inputJsonPath)
{
    if (m_status != RunStatus::Idle) return; // jobs are single-use
    m_runDirectory = runDirectory;
    m_cancelRequested = false;
    const bool executable = QFileInfo(driverPath).suffix().compare("exe", Qt::CaseInsensitive) == 0;
    m_structured = executable || QFileInfo(driverPath).fileName() == "plate_solver.py";

    QDir().mkpath(runDirectory);

    // If these fail to open, logLine() output still reaches the Problems
    // panel live; only the on-disk copy is lost, so it's not fatal to the run.
    m_stdoutLog.setFileName(QDir(runDirectory).filePath("stdout.log"));
    static_cast<void>(m_stdoutLog.open(QIODevice::WriteOnly | QIODevice::Text));
    m_stderrLog.setFileName(QDir(runDirectory).filePath("stderr.log"));
    static_cast<void>(m_stderrLog.open(QIODevice::WriteOnly | QIODevice::Text));

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Without these, the reference driver crashes with UnicodeEncodeError
    // the moment it tries to print a non-ASCII character (e.g. the '->'
    // arrow in partitionLoadToElements) while stdout is redirected on
    // Windows, and matplotlib may try to open an interactive window.
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("MPLBACKEND", "Agg");
    m_process->setProcessEnvironment(env);
    m_process->setWorkingDirectory(runDirectory);

    QStringList args = {"-u", driverPath, inputJsonPath};
    if (m_structured) {
        QFile input(inputJsonPath);
        if (input.open(QIODevice::ReadOnly))
            m_inputSha256 = QString::fromLatin1(QCryptographicHash::hash(input.readAll(), QCryptographicHash::Sha256).toHex());
        args = {"-u", driverPath, "--input", inputJsonPath,
                "--output-dir", QDir(runDirectory).filePath("output_python_square"),
                "--events-jsonl", "--diagnostics", "summary",
                "--cancel-file", QDir(runDirectory).filePath("cancel.request")};
    }
    if (executable) { args.removeFirst(); args.removeFirst(); }
    setStatus(RunStatus::Starting);
    m_process->start(executable ? driverPath : pythonExecutable, args);
}

void SolverJob::cancel()
{
    if (m_process->state() == QProcess::NotRunning)
        return;
    m_cancelRequested = true;
    setStatus(RunStatus::Cancelling);
    if (m_structured) {
        QSaveFile marker(QDir(m_runDirectory).filePath("cancel.request"));
        if (!marker.open(QIODevice::WriteOnly) || marker.write("cancel\n") != 7 || !marker.commit())
            emit logLine("stderr", "Could not write cancellation request; process will be killed after the grace period.");
    } else {
        m_process->terminate();
    }
    m_killGraceTimer->start();
}

QString SolverJob::resultVtkPath() const
{
    return QDir(m_runDirectory).filePath("output_python_square/results.vtk");
}

void SolverJob::handleReadyReadStdOut()
{
    const QByteArray raw = m_process->readAllStandardOutput();
    if (m_stdoutLog.isOpen()) {
        m_stdoutLog.write(raw);
        m_stdoutLog.flush();
    }
    m_stdoutBuffer += raw;
    emitBufferedLines(m_stdoutBuffer, "stdout");
}

void SolverJob::handleReadyReadStdErr()
{
    const QByteArray raw = m_process->readAllStandardError();
    if (m_stderrLog.isOpen()) {
        m_stderrLog.write(raw);
        m_stderrLog.flush();
    }
    m_stderrBuffer += raw;
    emitBufferedLines(m_stderrBuffer, "stderr");
}

void SolverJob::emitBufferedLines(QByteArray &buffer, const QString &channel, bool flush)
{
    qsizetype end;
    while ((end = buffer.indexOf('\n')) >= 0) {
        consumeLine(buffer.left(end), channel);
        buffer.remove(0, end + 1);
    }
    if (flush && !buffer.isEmpty()) {
        consumeLine(buffer, channel);
        buffer.clear();
    } else if (buffer.size() > 65536) {
        if (m_structured && channel == "stdout") m_protocolError = "Oversized structured event.";
        // Disk retains the complete stream; bound an unterminated diagnostic line.
        emit logLine(channel, "[Long diagnostic line omitted from display; see log file]");
        buffer.clear();
    }
}

void SolverJob::consumeLine(const QByteArray &line, const QString &channel)
{
    if (!m_structured || channel != "stdout") {
        emit logLine(channel, QString::fromUtf8(line).trimmed());
        return;
    }
    if (line.size() > 65536) { m_protocolError = "Oversized structured event."; return; }
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(line, &error);
    const auto event = json.object();
    if (error.error != QJsonParseError::NoError || !json.isObject() || event["contractVersion"].toInt() != 1) {
        m_protocolError = "Invalid solver event or unsupported contract version.";
        return;
    }
    const auto type = event["type"].toString();
    if (m_completionReceived) { m_protocolError = "Unexpected event after completion."; return; }
    if (type == "phase") {
        const auto phase = event["phase"].toString();
        const int completed = event["completed"].toInt(-1), total = event["total"].toInt(-1);
        if (phase.isEmpty() || (completed >= 0 && (total < completed || total <= 0))) {
            m_protocolError = "Invalid phase progress.";
            return;
        }
        emit phaseChanged(phase, completed, total);
        emit logLine("stdout", completed >= 0 ? QString("%1: %2/%3").arg(phase).arg(completed).arg(total) : phase);
    } else if (type == "warning" || type == "error" || type == "cancelled") {
        const auto message = event["message"].toString();
        if (type != "warning") m_backendError = message.isEmpty() ? "Solver reported failure." : message;
        emit logLine(type == "warning" ? "stdout" : "stderr", type + ": " + message);
    } else if (type == "completed" && event["manifest"].toString() == "run-result.json") {
        m_completionReceived = true;
    } else {
        m_protocolError = "Unknown or malformed solver event.";
    }
}

bool SolverJob::validateManifest() const
{
    QFile file(QDir(m_runDirectory).filePath("output_python_square/run-result.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return false;
    const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
    if (manifest["contractVersion"].toInt() != 1 || manifest["status"].toString() != "Completed" ||
        manifest["unitSystem"].toString() != "mm-N-MPa" || manifest["result"].toString() != "results.vtk" ||
        m_inputSha256.isEmpty() || manifest["inputSha256"].toString() != m_inputSha256) return false;
    QFile vtk(resultVtkPath());
    if (!vtk.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&vtk)) return false;
    return manifest["resultSha256"].toString() == QString::fromLatin1(hash.result().toHex());
}

void SolverJob::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_finished) return;
    m_finished = true;
    m_exitCode = exitCode;
    m_killGraceTimer->stop();
    handleReadyReadStdOut();
    handleReadyReadStdErr();
    emitBufferedLines(m_stdoutBuffer, "stdout", true);
    emitBufferedLines(m_stderrBuffer, "stderr", true);
    m_stdoutLog.close();
    m_stderrLog.close();

    if (m_cancelRequested) {
        setStatus(RunStatus::Cancelled);
        emit finished(false, {}, "Cancelled by user.");
        return;
    }

    if (exitStatus == QProcess::CrashExit) {
        setStatus(RunStatus::Failed);
        emit finished(false, {}, QString("Solver process crashed (exit code %1).").arg(exitCode));
        return;
    }

    if (exitCode != 0) {
        setStatus(RunStatus::Failed);
        emit finished(false, {}, QString("Solver process exited with code %1. %2").arg(exitCode).arg(m_backendError));
        return;
    }

    if (m_structured && (!m_protocolError.isEmpty() || !m_backendError.isEmpty() ||
                         !m_completionReceived || !validateManifest())) {
        setStatus(RunStatus::Failed);
        const QString message = !m_protocolError.isEmpty() ? m_protocolError : !m_backendError.isEmpty() ? m_backendError
            : "Solver did not provide a valid completion event and matching result manifest.";
        emit finished(false, {}, message);
        return;
    }

    // The driver prints a normal exit even on some caught failures and its
    // finally-block save messages run regardless : exit code
    // zero is necessary but not sufficient. Require the artifact to exist.
    const QString vtkPath = resultVtkPath();
    if (!QFileInfo::exists(vtkPath)) {
        setStatus(RunStatus::Failed);
        emit finished(false, {}, QString("Solver exited successfully but did not produce %1. "
                                          "Check the Solver Log for the actual failure.").arg(vtkPath));
        return;
    }

    setStatus(RunStatus::LoadingResults);
    emit finished(true, vtkPath, {});
}

void SolverJob::handleProcessErrorOccurred(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        if (m_finished) return;
        m_finished = true;
        m_killGraceTimer->stop();
        m_stdoutLog.close();
        m_stderrLog.close();
        setStatus(RunStatus::Failed);
        emit finished(false, {}, QString("Failed to start the solver process: %1").arg(m_process->errorString()));
    }
    // Other errors (Crashed, Timedout, ...) are followed by a finished()
    // signal from Qt, which handleProcessFinished() already handles.
}

} // namespace femgui
