#include "controllers/AnalysisController.h"

#include "core/ProjectDocument.h"
#include "core/RunRecord.h"
#include "solver/SolverJob.h"
#include "io/ResultImporter.h"
#include "io/ProjectSerializer.h"
#include "visualization/PlateMeshBuilder.h"
#include "solver/SolverAdapterRegistry.h"
#include "solver/ISolverAdapter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QUuid>

namespace femgui {

namespace {
constexpr const char *kPythonExecutableKey = "Backend/PythonExecutable";
constexpr const char *kDriverPathKey = "Backend/DriverPath";
bool executableDriver(const QString &path) { return QFileInfo(path).suffix().compare("exe", Qt::CaseInsensitive) == 0; }
QString bundledDriver() { return QDir(QCoreApplication::applicationDirPath()).filePath("solver/PlateSolver/PlateSolver.exe"); }
}

AnalysisController::AnalysisController(ProjectDocument *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
{
    QSettings settings;
    m_pythonExecutable = settings.value(kPythonExecutableKey).toString();
    if (m_pythonExecutable.isEmpty())
        m_pythonExecutable = autodetectPythonExecutable();

    m_driverPath = settings.value(kDriverPathKey).toString();
    if (QFileInfo::exists(bundledDriver()) && (m_driverPath.isEmpty() ||
        QFileInfo(m_driverPath).fileName() == "plate_solver.py" ||
        QFileInfo(m_driverPath).fileName() == "expected_solver_implementation.py" ||
        QFileInfo(m_driverPath).fileName().compare("PlateSolver.exe", Qt::CaseInsensitive) == 0))
        m_driverPath = bundledDriver();
    // Upgrade the former built-in default; explicit custom drivers remain usable.
    if (QFileInfo(m_driverPath).fileName() == "expected_solver_implementation.py") {
        const auto production = QFileInfo(m_driverPath).absoluteDir().filePath("plate_solver.py");
        if (QFileInfo::exists(production)) {
            m_driverPath = production;
            settings.setValue(kDriverPathKey, production);
        }
    }
    if (m_driverPath.isEmpty())
        m_driverPath = autodetectDriverPath();
}

QString AnalysisController::pythonExecutable() const { return m_pythonExecutable; }

void AnalysisController::setPythonExecutable(const QString &path)
{
    if (isRunning()) return;
    m_pythonExecutable = path;
    QSettings().setValue(kPythonExecutableKey, path);
    emit backendConfigurationChanged();
}

QString AnalysisController::driverPath() const { return m_driverPath; }

void AnalysisController::setDriverPath(const QString &path)
{
    if (isRunning()) return;
    m_driverPath = path;
    QSettings().setValue(kDriverPathKey, path);
    emit backendConfigurationChanged();
}

QString AnalysisController::autodetectPythonExecutable()
{
    QString found = QStandardPaths::findExecutable("python");
    if (found.isEmpty())
        found = QStandardPaths::findExecutable("python3");
    return found;
}

QString AnalysisController::autodetectDriverPath()
{
    if (QFileInfo::exists(bundledDriver())) return bundledDriver();
    // Dev-build convenience: walk up from the executable's directory
    // looking for the production entry point. Packaging copies the same
    // solver/reference layout next to the executable.
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        QString candidate = dir.filePath("solver/reference/plate_solver.py");
        if (QFileInfo::exists(candidate))
            return QDir::toNativeSeparators(candidate);
        if (!dir.cdUp())
            break;
    }
    return {};
}

AnalysisController::BackendCheck AnalysisController::backendFilesCheck() const
{
    BackendCheck check;
    if (m_document->isSolid()) {
        check.message = "C3D8 input support is ready; production execution arrives in phase C4.";
        return check;
    }
    if (!executableDriver(m_driverPath) && (m_pythonExecutable.isEmpty() || !QFileInfo::exists(m_pythonExecutable))) {
        check.message = QString("Python executable not found: '%1'").arg(m_pythonExecutable);
        return check;
    }
    if (m_driverPath.isEmpty() || !QFileInfo::exists(m_driverPath)) {
        check.message = QString("Solver driver not found: '%1'").arg(m_driverPath);
        return check;
    }
    if (executableDriver(m_driverPath)) {
        check.ok = true;
        check.message = "Solver executable found; Check Backend verifies bundled dependencies.";
        return check;
    }
    if (QFileInfo(m_driverPath).fileName() == "plate_solver.py" &&
        !QFileInfo::exists(QFileInfo(m_driverPath).absoluteDir().filePath("expected_solver_implementation.py"))) {
        check.message = "The production backend is missing expected_solver_implementation.py.";
        return check;
    }
    QString elementModulePath = QFileInfo(m_driverPath).absolutePath() + "/fem_local_Plate20_DOF5_07_09_2025.py";
    if (!QFileInfo::exists(elementModulePath)) {
        check.message = QString("Element module not found next to the driver: '%1'").arg(elementModulePath);
        return check;
    }

    check.ok = true;
    check.message = "Backend files found; dependency imports are checked by Check Backend.";
    return check;
}

void AnalysisController::checkBackend()
{
    if (m_checkingBackend || isRunning()) return;
    const auto files = backendFilesCheck();
    if (!files.ok) { emit backendChecked(false, files.message); return; }
    m_checkingBackend = true;
    auto *process = new QProcess(this);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    timer->setInterval(15000);
    connect(timer, &QTimer::timeout, process, &QProcess::kill);
    connect(process, &QProcess::errorOccurred, this, [this, process, timer](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        timer->stop();
        m_checkingBackend = false;
        emit backendChecked(false, process->errorString());
        process->deleteLater();
    });
    connect(process, &QProcess::finished, this, [this, process, timer](int code, QProcess::ExitStatus status) {
        timer->stop();
        m_checkingBackend = false;
        bool ok = code == 0 && status == QProcess::NormalExit;
        emit backendChecked(ok, ok ? QString::fromUtf8(process->readAllStandardOutput()).trimmed()
                                   : "Backend dependency check failed or timed out: " + QString::fromUtf8(process->readAllStandardError()));
        process->deleteLater();
    });
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUTF8", "1");
    env.insert("MPLBACKEND", "Agg");
    process->setProcessEnvironment(env);
    QString script = "import sys; sys.path.insert(0, sys.argv[1]); import numpy, scipy, matplotlib; "
        "from fem_local_Plate20_DOF5_07_09_2025 import Klocal, Flocal, Blocalbending, Dmatbending, Blocalmembrane, Dmatmembrane; ";
    if (QFileInfo(m_driverPath).fileName() == "plate_solver.py")
        script += "from expected_solver_implementation import run_analysis; from plate_solver import validate_input; ";
    script += "print('Backend imports OK: Python '+sys.version.split()[0]+', NumPy '+numpy.__version__+', SciPy '+scipy.__version__+', Matplotlib '+matplotlib.__version__)";
    if (executableDriver(m_driverPath)) process->start(m_driverPath, {"--check-backend"});
    else process->start(m_pythonExecutable, {"-B", "-c", script, QFileInfo(m_driverPath).absolutePath()});
    timer->start();
}

QString AnalysisController::runsRootDirectory() const
{
    if (!m_document->filePath().isEmpty())
        return QFileInfo(m_document->filePath()).absolutePath() + "/runs";
    QString workspace = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/workspace-runs";
    return workspace;
}

void AnalysisController::writeRunManifest(const QString &runDirectory, const RunRecord &run) const
{
    QJsonObject obj;
    obj["runId"] = run.runId;
    obj["startedAt"] = run.startedAt.toString(Qt::ISODate);
    obj["finishedAt"] = run.finishedAt.isValid() ? run.finishedAt.toString(Qt::ISODate) : QJsonValue();
    obj["status"] = toString(run.status);
    obj["modelRevisionAtLaunch"] = run.modelRevisionAtLaunch;
    obj["exitCode"] = run.exitCode;
    obj["errorMessage"] = run.errorMessage;
    obj["pythonExecutable"] = run.pythonExecutable;
    obj["driverPath"] = run.driverPath;
    obj["inputSha256"] = run.inputSha256;
    obj["driverSha256"] = run.driverSha256;
    obj["elementSha256"] = run.elementSha256;
    obj["backendId"] = run.backendId;
    obj["modelKind"] = run.modelKind;
    obj["resultVtkPath"] = run.resultVtkPath;

    QSaveFile file(QDir(runDirectory).filePath("run.json"));
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        emit const_cast<AnalysisController *>(this)->logLine("stderr", "Could not save run manifest: " + file.errorString());
}

ValidationIssues AnalysisController::run()
{
    const auto &adapter = SolverAdapterRegistry::forModel(m_document->model());
    ValidationIssues issues = adapter.validate(m_document->model());
    if (hasErrors(issues) || isRunning())
        return issues;
    const auto files = backendFilesCheck();
    if (!files.ok) {
        issues.append({Severity::Error, files.message, "backend", {}, {}});
        return issues;
    }

    QVector<PatchRowMapping> patchRows;
    QJsonObject inputJson = adapter.buildSolverInput(m_document->model(), &patchRows);

    QString runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString runDirectory = QDir(runsRootDirectory()).filePath(runId);
    if (!QDir().mkpath(runsRootDirectory()) || !QDir(runsRootDirectory()).mkdir(runId)) {
        issues.append({Severity::Error, "Could not create a fresh run directory.", "run", {}, {}});
        return issues;
    }

    QFile inputFile(QDir(runDirectory).filePath("input.json"));
    if (!inputFile.open(QIODevice::WriteOnly)) {
        issues.append({Severity::Error, QString("Could not write run input to '%1'.").arg(inputFile.fileName()), "run", {}, {}});
        return issues;
    }
    const QByteArray inputBytes = QJsonDocument(inputJson).toJson(QJsonDocument::Indented);
    if (inputFile.write(inputBytes) != inputBytes.size() || !inputFile.flush()) {
        issues.append({Severity::Error, "Could not write the complete solver input.", "run", {}, {}});
        return issues;
    }
    inputFile.close();

    RunRecord run;
    run.runId = runId;
    run.startedAt = QDateTime::currentDateTime();
    run.status = RunStatus::Starting;
    run.runDirectory = runDirectory;
    run.modelRevisionAtLaunch = m_document->modelRevision();
    run.backendId = m_document->backendId();
    run.modelKind = m_document->modelKindId();
    run.inputSnapshot = m_document->model();
    run.hasInputSnapshot = true;
    run.pythonExecutable = m_pythonExecutable;
    run.driverPath = m_driverPath;
    run.inputSha256 = QString::fromLatin1(QCryptographicHash::hash(inputBytes, QCryptographicHash::Sha256).toHex());
    auto fileHash = [](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QString();
        return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
    };
    run.driverSha256 = fileHash(m_driverPath);
    run.elementSha256 = fileHash(QFileInfo(m_driverPath).absoluteDir().filePath(
        executableDriver(m_driverPath) ? "_internal/fem_local_Plate20_DOF5_07_09_2025.py" : "fem_local_Plate20_DOF5_07_09_2025.py"));
    const auto snapshotSave = ProjectSerializer::save(*m_document, QDir(runDirectory).filePath("input-project-snapshot.json"));
    if (!snapshotSave.success) {
        issues.append({Severity::Error, snapshotSave.error, "run", {}, {}});
        return issues;
    }
    m_document->addRun(run);
    writeRunManifest(runDirectory, run);

    m_currentRunId = runId;
    m_currentJob = new SolverJob(this);
    m_cancelLoading = false;
    connect(m_currentJob, &SolverJob::logLine, this, &AnalysisController::logLine);
    connect(m_currentJob, &SolverJob::phaseChanged, this, &AnalysisController::phaseChanged);
    connect(m_currentJob, &SolverJob::statusChanged, this, [this, runId](RunStatus status) {
        m_document->updateRun(runId, [status](RunRecord &r) { r.status = status; });
    });
    connect(m_currentJob, &SolverJob::finished, this, [this, runId, runDirectory, snapshot = std::get<PlateDefinition>(run.inputSnapshot)](bool success, QString resultVtkPath, QString errorMessage) {
        m_document->updateRun(runId, [&](RunRecord &r) {
            r.finishedAt = QDateTime::currentDateTime();
            r.resultVtkPath = resultVtkPath;
            r.errorMessage = errorMessage;
            r.exitCode = m_currentJob->exitCode();
            if (!success && r.status != RunStatus::Cancelled)
                r.status = RunStatus::Failed;
            writeRunManifest(r.runDirectory, r);
        });
        auto finish = [this, runId](bool ok, const QString &error) {
            auto *job = m_currentJob;
            m_currentJob = nullptr;
            job->deleteLater();
            refreshRunManifest(runId);
            emit runFinished(runId, ok, error);
        };
        if (!success) { finish(false, errorMessage); return; }
        auto *watcher = new QFutureWatcher<ResultImporter::Result>(this);
        connect(watcher, &QFutureWatcher<ResultImporter::Result>::finished, this, [this, watcher, runId, finish] {
            auto imported = watcher->result();
            watcher->deleteLater();
            if (m_cancelLoading) { imported.success = false; imported.error = "Cancelled while loading results."; }
            m_document->updateRun(runId, [&](RunRecord &r) {
                r.status = m_cancelLoading ? RunStatus::Cancelled : imported.success ? RunStatus::Completed : RunStatus::Failed;
                r.errorMessage = imported.error;
                r.finishedAt = QDateTime::currentDateTime();
            });
            if (imported.success)
                m_document->setResultSet(std::move(imported.resultSet), m_document->findRun(runId)->modelRevisionAtLaunch);
            finish(imported.success, imported.error);
        });
        watcher->setFuture(QtConcurrent::run([resultVtkPath, runId, snapshot] {
            auto reference = PlateMeshBuilder::build(snapshot);
            return ResultImporter::importFromFile(resultVtkPath, runId,
                static_cast<int>(snapshot.nodeCount()), static_cast<int>(snapshot.elementCount()), reference);
        }));
    });

    emit runStarted(runId);
    m_currentJob->start(m_pythonExecutable, m_driverPath, runDirectory, inputFile.fileName());
    return {};
}

void AnalysisController::stop()
{
    if (m_currentJob)
        if (m_currentJob->status() == RunStatus::LoadingResults) m_cancelLoading = true;
        else m_currentJob->cancel();
}

void AnalysisController::refreshRunManifest(const QString &runId) const
{
    if (const RunRecord *run = m_document->findRun(runId))
        writeRunManifest(run->runDirectory, *run);
}

} // namespace femgui
