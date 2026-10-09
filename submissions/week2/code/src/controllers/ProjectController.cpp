#include "controllers/ProjectController.h"

#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QSaveFile>
#include <QCryptographicHash>
#include "io/ResultImporter.h"
#include "visualization/PlateMeshBuilder.h"

#include "controllers/AnalysisController.h"
#include "core/ProjectDocument.h"
#include "io/ProjectSerializer.h"
#include "solver/SolverAdapterRegistry.h"
#include "solver/ISolverAdapter.h"

namespace femgui {

ProjectController::ProjectController(QObject *parent)
    : QObject(parent)
{
    createFreshPair();
}

void ProjectController::createFreshPair()
{
    m_analysis.reset(); // release process owner before its document
    m_document = std::make_unique<ProjectDocument>();
    m_analysis = std::make_unique<AnalysisController>(m_document.get());
}

ProjectController::~ProjectController() = default;

void ProjectController::newProject()
{
    if (m_analysis && m_analysis->isRunning()) return;
    createFreshPair();
    emit projectReplaced();
}

ProjectController::Result ProjectController::open(const QString &path)
{
    if (m_analysis && m_analysis->isRunning()) return {false, "Stop the active run before opening another project."};
    auto newDocument = std::make_unique<ProjectDocument>();
    ProjectSerializer::Result loadResult = ProjectSerializer::load(*newDocument, path);
    if (!loadResult.success)
        return {false, loadResult.error};

    m_analysis.reset();
    m_document = std::move(newDocument);
    m_analysis = std::make_unique<AnalysisController>(m_document.get());
    emit projectReplaced();
    if (!loadResult.activeRunId.isEmpty()) {
        auto restore = loadRunResults(loadResult.activeRunId);
        if (!restore.success) return {true, "Project opened; saved results could not be restored: " + restore.error};
    }
    return {true, {}};
}

ProjectController::Result ProjectController::save()
{
    if (m_document->filePath().isEmpty())
        return {false, "Project has no file path yet; use Save As."};
    return saveAs(m_document->filePath());
}

ProjectController::Result ProjectController::saveAs(const QString &path)
{
    ProjectSerializer::Result saveResult = ProjectSerializer::save(*m_document, path);
    if (!saveResult.success)
        return {false, saveResult.error};
    m_document->setFilePath(QFileInfo(path).absoluteFilePath());
    m_document->markClean();
    return {true, {}};
}

ProjectController::Result ProjectController::exportSolverJson(const QString &path) const
{
    const auto &adapter = SolverAdapterRegistry::forModel(m_document->model());
    ValidationIssues issues = adapter.validate(m_document->model());
    if (hasErrors(issues))
        return {false, "Cannot export: the current model has validation errors. Run Validate for details."};

    QJsonObject json = adapter.buildSolverInput(m_document->model());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {false, QString("Could not open '%1' for writing: %2").arg(path, file.errorString())};
    const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) return {false, "Failed to save solver JSON: " + file.errorString()};
    return {true, {}};
}

ValidationIssues ProjectController::validate() const
{
    return SolverAdapterRegistry::forModel(m_document->model()).validate(m_document->model());
}

ProjectController::Result ProjectController::loadRunResults(const QString &runId)
{
    if (m_analysis->isRunning()) return {false, "Wait for the active run to finish."};
    const auto *record = m_document->findRun(runId);
    if (!record || record->status != RunStatus::Completed) return {false, "Select a completed run."};
    if (!record->hasInputSnapshot) return {false, "This older run has no saved input snapshot; its geometry cannot be safely associated."};
    const auto run = *record;
    if (modelKind(run.inputSnapshot) != m_document->modelKind())
        return {false, "The saved run belongs to a different model kind than the open project."};
    const auto &adapter = SolverAdapterRegistry::forModel(run.inputSnapshot);
    if (run.backendId != adapter.capabilities().backendId || run.modelKind != adapter.capabilities().modelKind)
        return {false, "The saved run backend/model identity does not match its input snapshot."};
    if (!std::holds_alternative<PlateDefinition>(run.inputSnapshot))
        return {false, "C3D8 result import arrives in phase C5."};
    const bool production = QFileInfo(run.driverPath).fileName() == "plate_solver.py" ||
        QFileInfo(run.driverPath).suffix().compare("exe", Qt::CaseInsensitive) == 0;
    const auto outputDir = QFileInfo(run.resultVtkPath).absoluteDir();
    QFile manifestFile(outputDir.filePath("run-result.json"));
    if (production || manifestFile.exists()) {
        if (!manifestFile.open(QIODevice::ReadOnly) || manifestFile.size() > 1024 * 1024)
            return {false, "Saved results are missing a readable completion manifest."};
        const auto manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
        auto hashFile = [](const QString &path) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) return QString();
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(&file)) return QString();
            return QString::fromLatin1(hash.result().toHex());
        };
        QFile input(outputDir.filePath("input.json"));
        if (manifest.value("contractVersion").toDouble() != 1 ||
            manifest.value("status").toString() != "Completed" ||
            manifest.value("unitSystem").toString() != "mm-N-MPa" ||
            manifest.value("result").toString() != "results.vtk" ||
            run.inputSha256.isEmpty() ||
            manifest.value("inputSha256").toString() != run.inputSha256 ||
            hashFile(input.fileName()) != run.inputSha256 ||
            manifest.value("resultSha256").toString().isEmpty() ||
            hashFile(run.resultVtkPath) != manifest.value("resultSha256").toString() ||
            !input.open(QIODevice::ReadOnly) ||
            QJsonDocument::fromJson(input.readAll()).object() != adapter.buildSolverInput(run.inputSnapshot))
            return {false, "Saved result provenance does not match its input snapshot or artifact hashes."};
    }
    const auto &plate = std::get<PlateDefinition>(run.inputSnapshot);
    auto reference = PlateMeshBuilder::build(plate);
    auto imported = ResultImporter::importFromFile(run.resultVtkPath, run.runId,
        static_cast<int>(plate.nodeCount()), static_cast<int>(plate.elementCount()), reference);
    if (!imported.success) return {false, imported.error};
    m_document->setResultSet(std::move(imported.resultSet), run.modelRevisionAtLaunch);
    return {true, {}};
}

} // namespace femgui
