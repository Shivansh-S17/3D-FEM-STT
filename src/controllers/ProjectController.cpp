#include "controllers/ProjectController.h"

#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>
#include "io/ResultImporter.h"
#include "visualization/PlateMeshBuilder.h"
#include "visualization/C3D8MeshBuilder.h"

#include "controllers/AnalysisController.h"
#include "core/ProjectDocument.h"
#include "io/ProjectSerializer.h"
#include "solver/SolverAdapterRegistry.h"
#include "solver/ISolverAdapter.h"
#include "solver/SolverJob.h"

namespace femgui {

ProjectController::ProjectController(QObject *parent)
    : QObject(parent)
{
    createFreshPair(ModelKind::SolidC3D8);
}

void ProjectController::createFreshPair(ModelKind kind)
{
    m_analysis.reset(); // release process owner before its document
    m_document = std::make_unique<ProjectDocument>();
    if (kind == ModelKind::SolidC3D8) {
        SolidDefinition solid;
        solid.xNodes = {0.0, 500.0, 1000.0};
        solid.yNodes = {0.0, 500.0, 1000.0};
        solid.zNodes = {0.0, 100.0, 200.0};
        solid.material = {24000.0, 0.3};
        solid.foundation = {50.0, 50.0, 50.0};
        PressurePatch patch;
        patch.name = "Patch 1";
        patch.x1 = 0.0; patch.x2 = 1000.0;
        patch.y1 = 0.0; patch.y2 = 1000.0;
        patch.q = 0.8;
        solid.patches.append(std::move(patch));
        m_document->replaceModelForLoad(std::move(solid));
        m_document->restoreModelRevision(0);
        m_document->markClean();
    }
    m_analysis = std::make_unique<AnalysisController>(m_document.get());
}

ProjectController::~ProjectController()
{
    // QUndoStack::clear() emits state signals from its destructor. Disconnect
    // them before tearing down this controller pair so a window cannot query
    // an analysis/document pointer while the pair is being destroyed.
    if (m_document)
        m_document->undoStack()->disconnect();
    m_analysis.reset();
    m_document.reset();
}

void ProjectController::newProject()
{
    newProject(ModelKind::SolidC3D8);
}

void ProjectController::newProject(ModelKind kind)
{
    if (m_analysis && m_analysis->isRunning()) return;
    createFreshPair(kind);
    emit projectReplaced();
}

ProjectController::Result ProjectController::importC3D8SolverInput(const QString &path)
{
    if (m_analysis && m_analysis->isRunning())
        return {false, "Stop the active run before importing another project."};
    SolidDefinition solid;
    const auto parsed = ProjectSerializer::loadC3D8SolverInput(solid, path);
    if (!parsed.success) return {false, parsed.error};
    const ModelDefinition model = solid;
    const auto issues = SolverAdapterRegistry::forModel(model).validate(model);
    if (hasErrors(issues)) {
        QStringList errors;
        for (const auto &issue : issues)
            if (issue.severity == Severity::Error) errors.append(issue.message);
        return {false, "The imported C3D8 input is outside the supported envelope:\n" + errors.join("\n")};
    }

    auto newDocument = std::make_unique<ProjectDocument>();
    newDocument->replaceModelForLoad(solid);
    newDocument->restoreModelRevision(0);
    newDocument->setName(QFileInfo(path).completeBaseName());
    newDocument->setDescription("Imported from C3D8 solver input: " + QFileInfo(path).absoluteFilePath());
    m_analysis.reset();
    m_document = std::move(newDocument);
    m_analysis = std::make_unique<AnalysisController>(m_document.get());
    emit projectReplaced();
    return {true, {}};
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
    const auto kind = modelKind(run.inputSnapshot);
    const auto outputDir = QFileInfo(run.resultVtkPath).absoluteDir();
    const auto driverName = QFileInfo(run.driverPath).fileName();
    const bool production = kind == ModelKind::SolidC3D8 || driverName == "plate_solver.py" ||
        QFileInfo(run.driverPath).suffix().compare("exe", Qt::CaseInsensitive) == 0;
    if (production || QFileInfo::exists(outputDir.filePath("run-result.json"))) {
        QString error;
        const bool solid = kind == ModelKind::SolidC3D8;
        if (!SolverJob::validateResultManifest(run.resultVtkPath, run.inputSha256,
                solid ? run.backendId : QString(), solid ? run.modelKind : QString(), &error))
            return {false, "Saved result verification failed: " + error};
        QFile input(outputDir.filePath("input.json"));
        if (!input.open(QIODevice::ReadOnly))
            return {false, "Saved result verification failed: archived input is missing."};
        const auto bytes = input.readAll();
        QJsonParseError parseError;
        const auto inputDocument = QJsonDocument::fromJson(bytes, &parseError);
        if (QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != run.inputSha256 ||
            parseError.error != QJsonParseError::NoError || !inputDocument.isObject() ||
            inputDocument.object() != adapter.buildSolverInput(run.inputSnapshot))
            return {false, "Saved result verification failed: archived input does not match its hash and snapshot."};
    }
    vtkSmartPointer<vtkUnstructuredGrid> reference;
    if (kind == ModelKind::Plate) reference = PlateMeshBuilder::build(std::get<PlateDefinition>(run.inputSnapshot));
    else reference = C3D8MeshBuilder::build(std::get<SolidDefinition>(run.inputSnapshot));
    auto imported = ResultImporter::importFromFile(run.resultVtkPath, run.runId,
        static_cast<int>(modelNodeCount(run.inputSnapshot)), static_cast<int>(modelElementCount(run.inputSnapshot)), reference, kind);
    if (!imported.success) return {false, imported.error};
    m_document->setResultSet(std::move(imported.resultSet), run.modelRevisionAtLaunch);
    return {true, {}};
}

} // namespace femgui
