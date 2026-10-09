#include <QtTest>
#include <QApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <type_traits>
#include <QFileDialog>
#include <QTabWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSettings>
#include <QScopeGuard>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QToolBar>
#include <QTreeWidget>
#include <QVTKOpenGLNativeWidget.h>
#include <vtkActor.h>
#include <vtkCell.h>
#include <vtkCamera.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkDoubleArray.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyDataMapper.h>
#include <vtkSphereSource.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkUnstructuredGridWriter.h>
#include <cmath>
#include <limits>

#include "controllers/AnalysisController.h"
#include "controllers/ProjectController.h"
#include "core/ProjectDocument.h"
#include "io/ProjectSerializer.h"
#include "io/ResultImporter.h"
#include "solver/PlateSolverAdapter.h"
#include "solver/C3D8SolverAdapter.h"
#include "solver/SolverAdapterRegistry.h"
#include "solver/SolverJob.h"
#include "ui/MainWindow.h"
#include "ui/GeometryEditorDialog.h"
#include "ui/InspectorPanel.h"
#include "ui/ProjectPanel.h"
#include "ui/ScientificNumberEdit.h"
#include "ui/PressurePatchDialog.h"
#include "ui/UiStyle.h"
#include "io/ResultExporter.h"
#include "ui/ViewportWidget.h"
#include "visualization/PlateMeshBuilder.h"
#include "visualization/C3D8MeshBuilder.h"

namespace femgui {
namespace {
QString fixturePath() { return QString(FEM_SOURCE_DIR) + "/tests/fixtures/tiny_3x3/expected_output/results.vtk"; }
QString driverPath() { return QString(FEM_SOURCE_DIR) + "/solver/reference/plate_solver.py"; }
QString solidResultPath() { return QString(FEM_SOURCE_DIR) + "/tests/fixtures/c3d8/single_brick/expected/production_results.vtk"; }
PlateDefinition fixturePlate()
{
    PlateDefinition plate;
    plate.xNodes = {0, 500, 1000}; plate.yNodes = {0, 500, 1000};
    plate.foundation = {1, 1, 1};
    PressurePatch patch;
    patch.name = "Reference patch";
    patch.x1 = patch.y1 = 250; patch.x2 = patch.y2 = 750; patch.q = 0.7;
    plate.patches.append(patch);
    return plate;
}
SolidDefinition fixtureSolid()
{
    SolidDefinition solid;
    solid.xNodes = {0, 500, 1000}; solid.yNodes = {0, 500, 1000}; solid.zNodes = {0, 150, 300};
    solid.material = {24000, 0.2}; solid.foundation = {1, 2, 3};
    PressurePatch patch;
    patch.name = "Solid reference patch";
    patch.x1 = patch.y1 = 250; patch.x2 = patch.y2 = 750; patch.q = 0.7;
    solid.patches.append(patch);
    return solid;
}
ResultImporter::Result fixtureResult(const QString &run = "fixture")
{
    auto mesh = PlateMeshBuilder::build(fixturePlate());
    return ResultImporter::importFromFile(fixturePath(), run, 9, 4, mesh);
}
bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
void verifySavedResultIntegrity(ProjectController &controller, const RunRecord &run)
{
    const auto output = QFileInfo(run.resultVtkPath).absoluteDir();
    for (const auto &name : {QString("results.vtk"), QString("input.json"), QString("run-result.json")}) {
        QFile file(output.filePath(name));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto original = file.readAll();
        file.close();
        const auto modified = name == "run-result.json" ? QByteArray("{}") : original + "\n";
        QVERIFY(writeBytes(file.fileName(), modified));
        controller.document()->clearResultSet();
        const auto rejected = controller.loadRunResults(run.runId);
        QVERIFY2(!rejected.success, qPrintable(name));
        QVERIFY(!controller.document()->hasResultSet());
        QVERIFY(writeBytes(file.fileName(), original));
        const auto restored = controller.loadRunResults(run.runId);
        QVERIFY2(restored.success, qPrintable(restored.error));
    }
    controller.document()->updateRun(run.runId, [](RunRecord &record) {
        std::visit([](auto &model) {
            if constexpr (std::is_same_v<std::decay_t<decltype(model)>, PlateDefinition>) model.slab.emod *= 2;
            else model.material.emod *= 2;
        }, record.inputSnapshot);
    });
    controller.document()->clearResultSet();
    QVERIFY(!controller.loadRunResults(run.runId).success);
    controller.document()->updateRun(run.runId, [&](RunRecord &record) { record.inputSnapshot = run.inputSnapshot; });
    QVERIFY(controller.loadRunResults(run.runId).success);
}
QString labels(QWidget *widget)
{
    QStringList strings;
    for (auto *label : widget->findChildren<QLabel *>()) strings << label->text();
    return strings.join('\n');
}
}

class IntegrationTests : public QObject
{
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QCoreApplication::setOrganizationName("FemGuiTests");
        QCoreApplication::setApplicationName("Regression");
        QSettings().setValue("Backend/PythonExecutable", FEM_TEST_PYTHON);
        QSettings().setValue("Backend/DriverPath", driverPath());
    }

    void meshEnumerationAndPrecision()
    {
        auto plate = fixturePlate();
        plate.xNodes = {0.1234567890123, 1.1234567890123, 9.1234567890123};
        plate.yNodes = {-2, 7};
        auto mesh = PlateMeshBuilder::build(plate);
        QCOMPARE(mesh->GetNumberOfPoints(), vtkIdType(6));
        QCOMPARE(mesh->GetNumberOfCells(), vtkIdType(2));
        QCOMPARE(plate.dofCount(), 30LL);
        const vtkIdType expected[4] = {1, 2, 5, 4};
        for (int i = 0; i < 4; ++i) QCOMPARE(mesh->GetCell(1)->GetPointId(i), expected[i]);
        double p[3]; mesh->GetPoint(3, p);
        QCOMPARE(p[0], plate.xNodes[0]); QCOMPARE(p[1], 7.0);
    }

    void invalidGeometryIsNotRendered()
    {
        auto plate = fixturePlate(); plate.xNodes[1] = plate.xNodes[0];
        QVERIFY(hasErrors(PlateSolverAdapter().validate(plate)));
        QCOMPARE(PlateMeshBuilder::build(plate)->GetNumberOfPoints(), vtkIdType(0));
        plate.xNodes[1] = std::numeric_limits<double>::quiet_NaN();
        QCOMPARE(PlateMeshBuilder::build(plate)->GetNumberOfPoints(), vtkIdType(0));
    }

    void countsDoNotOverflow()
    {
        PlateDefinition plate;
        plate.xNodes.resize(50000); plate.yNodes.resize(50000);
        QCOMPARE(plate.nodeCount(), qint64(2500000000LL));
        QCOMPARE(plate.dofCount(), 12500000000LL);
        QVERIFY(hasErrors(PlateSolverAdapter().validate(plate)));
        QCOMPARE(PlateMeshBuilder::build(plate)->GetNumberOfPoints(), vtkIdType(0));
    }

    void loadsValidateAndSerialize()
    {
        auto plate = fixturePlate();
        QVERIFY(!hasErrors(PlateSolverAdapter().validate(plate)));
        plate.patches[0].x1 = -1;
        QVERIFY(hasErrors(PlateSolverAdapter().validate(plate)));
        plate = fixturePlate();
        auto disabled = plate.patches[0]; disabled.id = QUuid::createUuid(); disabled.enabledForAnalysis = false;
        plate.patches.append(disabled);
        plate.patches[0].visible = false;
        QVector<PatchRowMapping> mapping;
        auto json = PlateSolverAdapter().buildSolverInput(plate, &mapping);
        QCOMPARE(json.value("loads").toObject().value("q").toArray().size(), 1);
        QCOMPARE(mapping.size(), 1);
        QCOMPARE(json.keys().size(), 4);
    }

    void c3d8AdapterContractAndValidation()
    {
        auto solid = fixtureSolid();
        C3D8SolverAdapter adapter;
        QVERIFY(!hasErrors(adapter.validate(ModelDefinition{solid})));
        QCOMPARE(adapter.capabilities().backendId, QString("c3d8-python-v1"));
        QCOMPARE(adapter.capabilities().modelKind, QString("structured_solid_c3d8"));
        QCOMPARE(adapter.capabilities().dofsPerNode, 3);
        QCOMPARE(adapter.capabilities().nodesPerElement, 8);
        QVERIFY(adapter.capabilities().supportsStrain);
        QCOMPARE(&SolverAdapterRegistry::forModel(ModelDefinition{solid}),
                 &SolverAdapterRegistry::forKind(ModelKind::SolidC3D8));

        auto disabled = solid.patches[0];
        disabled.id = QUuid::createUuid(); disabled.enabledForAnalysis = false;
        solid.patches.append(disabled);
        auto zero = disabled;
        zero.id = QUuid::createUuid(); zero.enabledForAnalysis = true; zero.q = 0;
        solid.patches.append(zero);
        QVector<PatchRowMapping> rows;
        const auto actual = adapter.buildSolverInput(solid, &rows);
        const QJsonObject expected{
            {"nodes", QJsonObject{{"x", QJsonArray{0, 500, 1000}}, {"y", QJsonArray{0, 500, 1000}},
                                   {"z", QJsonArray{0, 150, 300}}}},
            {"material", QJsonObject{{"Emod", 24000}, {"nu", 0.2}}},
            {"subgrade", QJsonObject{{"Kx", 1}, {"Ky", 2}, {"Kz", 3}}},
            {"loads", QJsonArray{QJsonObject{{"q", 0.7}, {"x1", 250}, {"x2", 750},
                                                   {"y1", 250}, {"y2", 750}}}},
            {"options", QJsonObject{{"reduced_integration", false}}}
        };
        QCOMPARE(actual, expected);
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0].patchId, solid.patches[0].id);
        QCOMPARE(rows[0].row, 0);
        QVERIFY(adapter.buildSolverInput(ModelDefinition{fixturePlate()}).isEmpty());
        QVERIFY(hasErrors(adapter.validate(ModelDefinition{fixturePlate()})));

        solid = fixtureSolid(); solid.reducedIntegration = true;
        QVERIFY(hasErrors(adapter.validate(solid)));
        solid = fixtureSolid(); solid.zNodes[1] = solid.zNodes[0];
        QVERIFY(hasErrors(adapter.validate(solid)));
        solid = fixtureSolid(); solid.foundation.kz = 0;
        QVERIFY(hasErrors(adapter.validate(solid)));
        solid = fixtureSolid(); solid.patches[0].x2 = 1001;
        QVERIFY(hasErrors(adapter.validate(solid)));
        solid = fixtureSolid(); solid.coordinateConvention = "unknown";
        QVERIFY(hasErrors(adapter.validate(solid)));
        solid = fixtureSolid();
        solid.xNodes.resize(14); solid.yNodes.resize(14); solid.zNodes.resize(14);
        for (int i = 0; i < solid.xNodes.size(); ++i) solid.xNodes[i] = i * 100.0;
        for (int i = 0; i < solid.yNodes.size(); ++i) solid.yNodes[i] = i * 100.0;
        for (int i = 0; i < solid.zNodes.size(); ++i) solid.zNodes[i] = i * 100.0;
        const auto largerModel = adapter.validate(solid);
        QVERIFY(!hasErrors(largerModel));
        QVERIFY(std::none_of(largerModel.begin(), largerModel.end(), [](const auto &issue) {
            return issue.fieldKey == "mesh.size";
        }));
    }

    void c3d8MeshMatchesSourceEnumeration()
    {
        auto solid = fixtureSolid();
        solid.xNodes = {1, 4, 10}; solid.yNodes = {-2, 9}; solid.zNodes = {3, 8, 20};
        auto mesh = C3D8MeshBuilder::build(solid);
        QCOMPARE(mesh->GetNumberOfPoints(), vtkIdType(18));
        QCOMPARE(mesh->GetNumberOfCells(), vtkIdType(4));
        double xyz[3];
        mesh->GetPoint(0, xyz); QCOMPARE(xyz[0], 1.0); QCOMPARE(xyz[1], -2.0); QCOMPARE(xyz[2], 3.0);
        mesh->GetPoint(5, xyz); QCOMPARE(xyz[0], 10.0); QCOMPARE(xyz[1], -2.0); QCOMPARE(xyz[2], 8.0);
        mesh->GetPoint(10, xyz); QCOMPARE(xyz[0], 4.0); QCOMPARE(xyz[1], 9.0); QCOMPARE(xyz[2], 3.0);
        const vtkIdType expected0[8] = {0, 1, 10, 9, 3, 4, 13, 12};
        const vtkIdType expected2[8] = {3, 4, 13, 12, 6, 7, 16, 15};
        for (int n = 0; n < 8; ++n) {
            QCOMPARE(mesh->GetCell(0)->GetPointId(n), expected0[n]);
            QCOMPARE(mesh->GetCell(2)->GetPointId(n), expected2[n]);
        }
        auto *nodeIds = mesh->GetPointData()->GetArray("solver_node_id");
        auto *elementIds = mesh->GetCellData()->GetArray("solver_element_id");
        auto *loadedNodes = mesh->GetPointData()->GetArray("loaded_face_node");
        auto *foundationNodes = mesh->GetPointData()->GetArray("foundation_face_node");
        auto *loadedElements = mesh->GetCellData()->GetArray("loaded_face_element");
        auto *foundationElements = mesh->GetCellData()->GetArray("foundation_face_element");
        QVERIFY(nodeIds && elementIds && loadedNodes && foundationNodes && loadedElements && foundationElements);
        QCOMPARE(nodeIds->GetTuple1(17), 17.0); QCOMPARE(elementIds->GetTuple1(3), 3.0);
        QCOMPARE(loadedNodes->GetTuple1(0), 1.0); QCOMPARE(loadedNodes->GetTuple1(3), 0.0);
        QCOMPARE(foundationNodes->GetTuple1(6), 1.0); QCOMPARE(foundationNodes->GetTuple1(3), 0.0);
        QCOMPARE(loadedElements->GetTuple1(0), 1.0); QCOMPARE(loadedElements->GetTuple1(2), 0.0);
        QCOMPARE(foundationElements->GetTuple1(0), 0.0); QCOMPARE(foundationElements->GetTuple1(2), 1.0);

        solid.zNodes[1] = solid.zNodes[0];
        QCOMPARE(C3D8MeshBuilder::build(solid)->GetNumberOfPoints(), vtkIdType(0));
    }

    void typedSolidUndoAndRevision()
    {
        ProjectDocument doc;
        doc.replaceModelForLoad(fixtureSolid());
        doc.restoreModelRevision(4);
        doc.markClean();
        QVERIFY(doc.isSolid());
        QCOMPARE(doc.modelKindId(), QString("structured_solid_c3d8"));
        QCOMPARE(doc.backendId(), QString("c3d8-python-v1"));
        QCOMPARE(doc.solid().nodeCount(), 27LL);
        QCOMPARE(doc.solid().elementCount(), 8LL);
        QCOMPARE(doc.solid().dofCount(), 81LL);

        doc.editSolid([](auto &s) { s.patches[0].visible = false; }, "Hide solid patch");
        QVERIFY(doc.isDirty());
        QCOMPARE(doc.modelRevision(), 4);
        QVERIFY(!doc.solid().patches[0].visible);
        doc.undoStack()->undo();
        QVERIFY(doc.solid().patches[0].visible);
        QCOMPARE(doc.modelRevision(), 4);
        QVERIFY(!doc.isDirty());

        doc.editSolid([](auto &s) { s.material.emod = 30000; }, "Edit solid material");
        QCOMPARE(doc.modelRevision(), 5);
        QCOMPARE(doc.solid().material.emod, 30000.0);
        doc.undoStack()->undo();
        QCOMPARE(doc.modelRevision(), 4);
        QCOMPARE(doc.solid().material.emod, 24000.0);
        doc.undoStack()->redo();
        QVERIFY(doc.isDirty());
        QCOMPARE(doc.modelRevision(), 5);
        QCOMPARE(doc.solid().material.emod, 30000.0);
        const int revision = doc.modelRevision();
        doc.editPlate([](auto &p) { p.slab.emod = 1; });
        QCOMPARE(doc.modelRevision(), revision);
        QVERIFY(doc.isSolid());
    }

    void newAndImportedC3D8Projects()
    {
        ProjectController controller;
        QVERIFY(controller.document()->isSolid());
        QVERIFY(!hasErrors(controller.validate()));
        controller.newProject(ModelKind::SolidC3D8);
        QVERIFY(controller.document()->isSolid());
        QVERIFY(!controller.document()->isDirty());
        QVERIFY(!hasErrors(controller.validate()));
        QVERIFY(controller.analysis()->driverPath().endsWith("c3d8_solver.py"));

        const auto *beforeFailure = controller.document();
        QTemporaryDir dir;
        const auto invalid = dir.path() + "/invalid.json";
        QVERIFY(writeBytes(invalid, "{}"));
        QVERIFY(!controller.importC3D8SolverInput(invalid).success);
        QCOMPARE(controller.document(), beforeFailure);

        const auto fixture = QString(FEM_SOURCE_DIR) + "/tests/fixtures/c3d8/single_brick/input.json";
        const auto imported = controller.importC3D8SolverInput(fixture);
        QVERIFY2(imported.success, qPrintable(imported.error));
        QVERIFY(controller.document()->isSolid());
        QVERIFY(controller.document()->isDirty());
        QCOMPARE(controller.document()->solid().xNodes, QVector<double>({0.0, 170.0}));
        QCOMPARE(controller.document()->solid().zNodes, QVector<double>({0.0, 63.0}));
        QCOMPARE(controller.document()->solid().patches.size(), 1);
        QCOMPARE(controller.document()->solid().patches[0].name, QString("Imported load 1"));
        QVERIFY(!hasErrors(controller.validate()));
        QVERIFY(controller.analysis()->driverPath().endsWith("c3d8_solver.py"));

        const auto received = QString(FEM_SOURCE_DIR) + "/C3D8v/python/inputs.json";
        const auto receivedImport = controller.importC3D8SolverInput(received);
        QVERIFY2(receivedImport.success, qPrintable(receivedImport.error));
        QCOMPARE(controller.document()->solid().dofCount(), 6615LL);
        QVERIFY(!hasErrors(controller.validate()));

        const auto userInput = QString(FEM_SOURCE_DIR) + "/tests/fixtures/c3d8/user_21x21x6/input.json";
        const auto userImport = controller.importC3D8SolverInput(userInput);
        QVERIFY2(userImport.success, qPrintable(userImport.error));
        QCOMPARE(controller.document()->solid().dofCount(), 7938LL);
        QCOMPARE(controller.document()->solid().elementCount(), 2000LL);
        QVERIFY(!hasErrors(controller.validate()));
    }

    void zeroFoundationCannotRun()
    {
        auto plate = fixturePlate();
        plate.foundation = {0, 0, 0};
        QVERIFY(hasErrors(PlateSolverAdapter().validate(plate)));
    }

    void projectRevisionAndRunRoundTrip()
    {
        QTemporaryDir dir;
        ProjectDocument source;
        source.editPlate([](auto &p) { p = fixturePlate(); });
        source.editPlate([](auto &p) { p.slab.emod *= 2; });
        RunRecord run; run.runId = "old"; run.status = RunStatus::Completed;
        run.inputSnapshot = fixturePlate(); run.hasInputSnapshot = true; run.modelRevisionAtLaunch = 1;
        run.runDirectory = dir.path() + "/runs/old"; run.resultVtkPath = fixturePath();
        source.markClean(); source.addRun(run); QVERIFY(source.isDirty());
        const auto path = dir.path() + "/project.femproject.json";
        QVERIFY(ProjectSerializer::save(source, path).success);
        ProjectDocument target;
        QVERIFY(ProjectSerializer::load(target, path).success);
        QCOMPARE(target.modelRevision(), source.modelRevision());
        QCOMPARE(target.runs().size(), 1);
        QVERIFY(target.runs()[0].hasInputSnapshot);
        QCOMPARE(std::get<PlateDefinition>(target.runs()[0].inputSnapshot).slab.emod, 30000.0);
        QCOMPARE(target.plate().slab.emod, 60000.0);
        auto imported = fixtureResult("old"); QVERIFY(imported.success);
        target.setResultSet(imported.resultSet, 1); QVERIFY(target.isResultSetStale());
        // Loading into an existing object replaces history rather than duplicating it.
        QVERIFY(ProjectSerializer::load(target, path).success);
        QCOMPARE(target.runs().size(), 1); QVERIFY(!target.hasResultSet());
    }

    void schemaTwoSolidRoundTripAndDispatch()
    {
        QTemporaryDir dir;
        const auto path = dir.path() + "/solid.femproject.json";
        ProjectDocument source;
        auto solid = fixtureSolid(); solid.patches[0].visible = false;
        source.replaceModelForLoad(solid);
        source.restoreModelRevision(7);
        RunRecord run;
        run.runId = "solid-old"; run.status = RunStatus::Completed;
        run.backendId = "c3d8-python-v1"; run.modelKind = "structured_solid_c3d8";
        run.inputSnapshot = solid; run.hasInputSnapshot = true; run.modelRevisionAtLaunch = 7;
        source.addRun(run);
        QVERIFY(ProjectSerializer::save(source, path).success);
        const auto saved = readJson(path);
        QCOMPARE(saved.value("schemaVersion").toInt(), 2);
        QCOMPARE(saved.value("modelKind").toString(), QString("structured_solid_c3d8"));
        QCOMPARE(saved.value("solver").toObject().value("backendId").toString(), QString("c3d8-python-v1"));
        QVERIFY(saved.value("model").toObject().value("zNodes").isArray());
        QVERIFY(!saved.contains("plate"));

        ProjectDocument target;
        QVERIFY(ProjectSerializer::load(target, path).success);
        QVERIFY(target.isSolid());
        QCOMPARE(target.modelRevision(), 7);
        QCOMPARE(target.solid().zNodes, solid.zNodes);
        QCOMPARE(target.solid().material.nu, 0.2);
        QVERIFY(!target.solid().patches[0].visible);
        QCOMPARE(target.runs().size(), 1);
        QVERIFY(std::holds_alternative<SolidDefinition>(target.runs()[0].inputSnapshot));
        QCOMPARE(target.runs()[0].backendId, QString("c3d8-python-v1"));

        ProjectController controller;
        const auto openResult = controller.open(path);
        QVERIFY2(openResult.success, qPrintable(openResult.error));
        QVERIFY(controller.document()->isSolid());
        const auto inputPath = dir.path() + "/solid-input.json";
        QVERIFY(controller.exportSolverJson(inputPath).success);
        QCOMPARE(readJson(inputPath), C3D8SolverAdapter().buildSolverInput(solid));
        RunRecord mismatchedResult;
        mismatchedResult.runId = "wrong-result"; mismatchedResult.status = RunStatus::Completed;
        mismatchedResult.hasInputSnapshot = true; mismatchedResult.inputSnapshot = solid;
        mismatchedResult.backendId = "plate-python-v1"; mismatchedResult.modelKind = "rectangular_plate_5dof";
        controller.document()->addRun(mismatchedResult);
        const auto resultRejected = controller.loadRunResults(mismatchedResult.runId);
        QVERIFY(!resultRejected.success);
        QVERIFY(resultRejected.error.contains("does not match"));

        auto corrupt = saved;
        auto runs = corrupt.value("runs").toArray();
        auto badRun = runs[0].toObject(); badRun["backendId"] = "plate-python-v1";
        runs[0] = badRun; corrupt["runs"] = runs;
        QVERIFY(writeBytes(path, QJsonDocument(corrupt).toJson()));
        const double before = target.solid().material.emod;
        auto rejected = ProjectSerializer::load(target, path);
        QVERIFY(!rejected.success);
        QCOMPARE(target.solid().material.emod, before);
        QVERIFY(target.isSolid());
    }

    void schemaOnePlateMigratesWithoutReinterpretation()
    {
        QTemporaryDir dir;
        const auto legacyPath = dir.path() + "/legacy.json";
        ProjectDocument source;
        source.editPlate([](auto &p) { p = fixturePlate(); p.slab.emod = 32123; });
        RunRecord run; run.runId = "legacy-run"; run.status = RunStatus::Completed;
        run.inputSnapshot = fixturePlate(); run.hasInputSnapshot = true; run.modelRevisionAtLaunch = 1;
        source.addRun(run);
        QVERIFY(ProjectSerializer::save(source, legacyPath).success);
        auto legacy = readJson(legacyPath);
        legacy["schemaVersion"] = 1;
        legacy["plate"] = legacy.take("model");
        auto runs = legacy.value("runs").toArray();
        auto oldRun = runs[0].toObject();
        const auto typedSnapshot = oldRun.value("inputSnapshot").toObject();
        oldRun["inputSnapshot"] = typedSnapshot.value("model");
        oldRun.remove("backendId"); oldRun.remove("modelKind");
        runs[0] = oldRun; legacy["runs"] = runs;
        QVERIFY(writeBytes(legacyPath, QJsonDocument(legacy).toJson()));

        ProjectDocument migrated;
        QVERIFY(ProjectSerializer::load(migrated, legacyPath).success);
        QVERIFY(migrated.isPlate());
        QCOMPARE(migrated.plate().slab.emod, 32123.0);
        QCOMPARE(migrated.runs().size(), 1);
        QVERIFY(std::holds_alternative<PlateDefinition>(migrated.runs()[0].inputSnapshot));
        QCOMPARE(migrated.runs()[0].backendId, QString("plate-python-v1"));
        QCOMPARE(migrated.runs()[0].modelKind, QString("rectangular_plate_5dof"));

        const auto migratedPath = dir.path() + "/migrated.json";
        QVERIFY(ProjectSerializer::save(migrated, migratedPath).success);
        const auto v2 = readJson(migratedPath);
        QCOMPARE(v2.value("schemaVersion").toInt(), 2);
        QCOMPARE(v2.value("modelKind").toString(), QString("rectangular_plate_5dof"));
        QVERIFY(v2.contains("model"));
        QVERIFY(!v2.contains("plate"));
    }

    void solidProjectOpensEditsAndInspects()
    {
        QTemporaryDir dir;
        const auto path = dir.path() + "/solid-ui.json";
        ProjectDocument source;
        source.replaceModelForLoad(fixtureSolid());
        source.restoreModelRevision(3);
        QVERIFY(ProjectSerializer::save(source, path).success);

        MainWindow window;
        const auto opened = window.m_controller->open(path);
        QVERIFY2(opened.success, qPrintable(opened.error));
        auto *doc = window.m_controller->document();
        QVERIFY(doc->isSolid());
        QCOMPARE(window.m_projectPanel->m_backendLabel->text(), QString("c3d8-python-v1 (structured C3D8 solid)"));
        QVERIFY(window.m_projectPanel->m_geometrySummaryLabel->text().contains("Z: 3 coordinates"));
        QVERIFY(!window.m_projectPanel->m_editGeometryButton->isHidden());
        QVERIFY(window.m_projectPanel->m_thicknessSpin->isReadOnly());
        QCOMPARE(window.m_projectPanel->m_thicknessSpin->value(), 300.0);
        QCOMPARE(window.m_modelViewport->m_plateMesh->GetNumberOfPoints(), vtkIdType(27));
        QCOMPARE(window.m_meshViewport->m_plateMesh->GetNumberOfCells(), vtkIdType(8));
        QCOMPARE(window.m_modelViewport->m_loadPlaneZ, 0.0);
        QVERIFY(window.m_modelViewport->m_loadFromNegativeZ);
        double overlayPoint[3]; window.m_modelViewport->m_patchMapper->GetInput()->GetPoint(0, overlayPoint);
        QVERIFY(overlayPoint[2] < window.m_modelViewport->m_loadPlaneZ);
        QVERIFY(window.m_modelViewport->m_loadPlaneZ - overlayPoint[2] >= window.m_modelViewport->m_overlayOffset);
        QVERIFY(window.m_modelViewport->m_renderer->GetActiveCamera()->GetPosition()[2] < 0.0);
        window.m_modelViewport->m_renderer->GetActiveCamera()->Azimuth(35.0);
        window.m_modelViewport->m_renderer->GetActiveCamera()->Zoom(1.5);
        QVERIFY(window.m_modelViewport->m_patchActor->GetVisibility());
        QVERIFY(window.m_modelViewport->m_patchOutlineActor->GetVisibility());
        QCOMPARE(window.m_modelViewport->m_patchMapper->GetInput()->GetNumberOfCells(), vtkIdType(1));
        QCOMPARE(window.m_modelViewport->m_overlayRenderer->GetLayer(), 1);
        QCOMPARE(window.m_modelViewport->m_overlayRenderer->GetActiveCamera(),
                 window.m_modelViewport->m_renderer->GetActiveCamera());

        const int revision = doc->modelRevision();
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<GeometryEditorDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->m_z.values->setPlainText("0\n125\n300");
            dialog->accept();
        });
        window.m_projectPanel->editGeometry();
        QCOMPARE(doc->solid().zNodes[1], 125.0);
        QCOMPARE(doc->modelRevision(), revision + 1);
        QCOMPARE(window.m_meshViewport->m_plateMesh->GetNumberOfCells(), vtkIdType(8));
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<GeometryEditorDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->m_z.values->setPlainText("0\n0\n300");
            QVERIFY(!dialog->m_buttons->button(QDialogButtonBox::Ok)->isEnabled());
            dialog->reject();
        });
        window.m_projectPanel->editGeometry();
        QCOMPARE(doc->solid().zNodes[1], 125.0);

        window.m_projectPanel->m_emodSpin->setText("26000");
        window.m_projectPanel->m_nuSpin->setText("0.25");
        window.m_projectPanel->applyMaterial();
        QCOMPARE(doc->solid().material.emod, 26000.0);
        QCOMPARE(doc->solid().material.nu, 0.25);
        window.m_projectPanel->m_kzSpin->setText("0");
        window.m_projectPanel->applyFoundation();
        QCOMPARE(doc->solid().foundation.kz, 3.0);
        window.m_projectPanel->m_loadsTable->item(0, 5)->setText("0.9");
        QCOMPARE(doc->solid().patches[0].q, 0.9);
        const int physicalRevision = doc->modelRevision();
        window.m_projectPanel->m_loadsTable->item(0, 7)->setCheckState(Qt::Unchecked);
        QVERIFY(!doc->solid().patches[0].visible);
        QCOMPARE(doc->modelRevision(), physicalRevision);
        QVERIFY(!window.m_modelViewport->m_patchActor->GetVisibility());
        QVERIFY(!window.m_modelViewport->m_patchOutlineActor->GetVisibility());
        window.m_undoAction->trigger();
        QVERIFY(doc->solid().patches[0].visible);
        QCOMPARE(doc->modelRevision(), physicalRevision);

        emit window.m_meshViewport->nodePicked(12);
        QVERIFY(labels(window.m_inspectorPanel).contains("(0, 1, 1)"));
        QVERIFY(labels(window.m_inspectorPanel).contains("125 mm"));
        QVERIFY(labels(window.m_inspectorPanel).contains("36..38"));
        emit window.m_meshViewport->elementPicked(0);
        QVERIFY(labels(window.m_inspectorPanel).contains("Touches z-min"));
        QVERIFY(labels(window.m_inspectorPanel).contains("8 (24 total)") == false);
        QVERIFY(labels(window.m_inspectorPanel).contains("3 (24 total)"));
        QCOMPARE(window.m_meshViewport->m_highlightMapper->GetInput()->GetNumberOfPoints(), vtkIdType(8));
        QCOMPARE(window.m_meshViewport->m_highlightMapper->GetInput()->GetNumberOfLines(), vtkIdType(12));
        emit window.m_meshViewport->elementPicked(2);
        QVERIFY(labels(window.m_inspectorPanel).contains("Touches z-max"));
        QVERIFY(labels(window.m_inspectorPanel).contains("No pressure patch applies to this element."));
        QVERIFY(!labels(window.m_inspectorPanel).contains("Applied pressure resultant:"));
    }

    void solidResultImportCatalogAndOwnership()
    {
        SolidDefinition solid;
        solid.xNodes = {0,170}; solid.yNodes = {0,280}; solid.zNodes = {0,63};
        solid.material = {24000,.3}; solid.foundation = {50,50,50};
        PressurePatch patch; patch.x1=0; patch.x2=170; patch.y1=0; patch.y2=280; patch.q=.8; solid.patches.append(patch);
        auto reference = C3D8MeshBuilder::build(solid);
        auto imported = ResultImporter::importFromFile(solidResultPath(), "solid", 8, 1, reference, ModelKind::SolidC3D8);
        QVERIFY2(imported.success, qPrintable(imported.error));
        QCOMPARE(imported.resultSet.modelKind(), ModelKind::SolidC3D8);
        QCOMPARE(imported.resultSet.availableFields().size(), 17);
        QVERIFY(imported.resultSet.computeExtrema("szz").valid);
        QCOMPARE(imported.resultSet.computeDisplacementMagnitude().size(), 8);

        auto asymmetric=solid; asymmetric.xNodes={10,180,460}; asymmetric.yNodes={-20,260}; asymmetric.zNodes={5,68,151};
        auto asymmetricReference=C3D8MeshBuilder::build(asymmetric);
        const auto asymmetricPath=QString(FEM_SOURCE_DIR)+"/tests/fixtures/c3d8/asymmetric_overlap/expected/production_results.vtk";
        auto asymmetricResult=ResultImporter::importFromFile(asymmetricPath,"asymmetric",18,4,asymmetricReference,ModelKind::SolidC3D8);
        QVERIFY2(asymmetricResult.success,qPrintable(asymmetricResult.error));
        QCOMPARE(asymmetricResult.resultSet.availableFields().size(),17);
        QCOMPARE(asymmetricResult.resultSet.grid()->GetPointData()->GetArray("solver_node_id")->GetTuple1(17),17.0);
        double point[3]; asymmetricResult.resultSet.grid()->GetPoint(10,point);
        QCOMPARE(point[0],180.0); QCOMPARE(point[1],260.0); QCOMPARE(point[2],5.0);

        ProjectController controller;
        controller.document()->replaceModelForLoad(solid); controller.document()->restoreModelRevision(2);
        RunRecord run; run.runId="solid-result"; run.status=RunStatus::Completed; run.hasInputSnapshot=true;
        run.inputSnapshot=solid; run.modelRevisionAtLaunch=2; run.backendId="c3d8-python-v1";
        run.modelKind="structured_solid_c3d8"; run.resultVtkPath=solidResultPath(); controller.document()->addRun(run);
        const auto unverified=controller.loadRunResults(run.runId);
        QVERIFY(!unverified.success);
        QVERIFY(!controller.document()->hasResultSet());

        auto *grid=imported.resultSet.grid(); grid->GetPointData()->RemoveArray("exy");
        vtkNew<vtkUnstructuredGridWriter> writer; QTemporaryDir dir; const auto bad=dir.path()+"/missing.vtk";
        writer->SetFileName(bad.toUtf8().constData()); writer->SetInputData(grid); QVERIFY(writer->Write());
        QVERIFY(!ResultImporter::importFromFile(bad,"bad",8,1,reference,ModelKind::SolidC3D8).success);

        auto *reordered=asymmetricResult.resultSet.grid(); vtkIdType wrong[8]={1,0,9,10,4,3,12,13}; reordered->ReplaceCell(0,8,wrong);
        const auto wrongPath=dir.path()+"/reordered.vtk"; writer->SetFileName(wrongPath.toUtf8().constData());
        writer->SetInputData(reordered); QVERIFY(writer->Write());
        QVERIFY(!ResultImporter::importFromFile(wrongPath,"wrong",18,4,asymmetricReference,ModelKind::SolidC3D8).success);
    }

    void solidSectionsAndReportMetadata()
    {
        auto solid = fixtureSolid();
        solid.xNodes = {10, 180, 460}; solid.yNodes = {-20, 260}; solid.zNodes = {5, 68, 151};
        solid.material = {24000, 0.3}; solid.foundation = {20, 35, 50};
        const auto path = QString(FEM_SOURCE_DIR) + "/tests/fixtures/c3d8/asymmetric_overlap/expected/production_results.vtk";
        auto imported = ResultImporter::importFromFile(path, "section-run", 18, 4,
            C3D8MeshBuilder::build(solid), ModelKind::SolidC3D8);
        QVERIFY2(imported.success, qPrintable(imported.error));

        ViewportWidget viewport(ViewportWidget::ViewMode::Results);
        viewport.setResultSet(&imported.resultSet);
        QCOMPARE(viewport.m_sectionCombo->count(), 4);
        viewport.setSectionIndex(2); // z=68 interior layer
        QVERIFY(viewport.sectionContext().contains("68 mm"));
        QVERIFY(viewport.sectionContext().contains("no interpolation"));
        auto *section = vtkPolyData::SafeDownCast(viewport.m_resultMapper->GetInput());
        QVERIFY(section);
        QCOMPARE(section->GetNumberOfCells(), vtkIdType(2));
        auto *sourceIds = section->GetCellData()->GetArray("section_source_element_id");
        QVERIFY(sourceIds);
        QCOMPARE(sourceIds->GetTuple1(0), 2.0);
        QCOMPARE(sourceIds->GetTuple1(1), 3.0);
        for (vtkIdType cellId = 0; cellId < section->GetNumberOfCells(); ++cellId)
            for (vtkIdType local = 0; local < section->GetCell(cellId)->GetNumberOfPoints(); ++local) {
                double point[3]; section->GetPoint(section->GetCell(cellId)->GetPointId(local), point);
                QCOMPARE(point[2], 68.0);
            }
        viewport.highlightElement(3);
        QCOMPARE(viewport.m_selectedElement, vtkIdType(3));
        viewport.highlightNode(4);
        QCOMPARE(viewport.m_selectedNode, vtkIdType(4));
        viewport.m_deformed = true;
        viewport.refreshResultActor();
        auto *warpedSection = vtkPolyData::SafeDownCast(viewport.m_resultMapper->GetInput());
        QVERIFY(warpedSection);
        QCOMPARE(warpedSection->GetCellData()->GetArray("solver_element_id")->GetTuple1(1), 3.0);
        viewport.m_deformed = false;
        viewport.setSectionIndex(0);
        QVERIFY(vtkUnstructuredGrid::SafeDownCast(viewport.m_resultMapper->GetInput()));

        QTemporaryDir dir;
        QString error;
        const auto csv = dir.path() + "/solid-results.csv";
        QVERIFY2(ResultExporter::writeCsv(imported.resultSet, 7, false, csv, &error), qPrintable(error));
        QFile file(csv); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto text = QString::fromUtf8(file.readAll());
        const auto rows = text.trimmed().split('\n');
        QCOMPARE(rows.size(), 19);
        QVERIFY(rows[0].contains("backend_id"));
        QVERIFY(rows[0].contains("shear_convention"));
        QVERIFY(rows[0].contains("exy []"));
        QVERIFY(rows[1].contains("c3d8-python-v1"));
        QVERIFY(rows[1].contains("structured_solid_c3d8"));
        QVERIFY(rows[1].contains("engineering shear strains gamma"));
        QVERIFY(rows[1].contains("arithmetically averaged at nodes"));
    }

    void malformedProjectIsTransactional_data()
    {
        QTest::addColumn<QString>("corruption");
        for (const auto &name : {"units", "backend", "slabNumber", "patchNumber", "coordinates", "flag", "duplicateId", "schema", "historyType"})
            QTest::newRow(name) << QString(name);
    }
    void malformedProjectIsTransactional()
    {
        QFETCH(QString, corruption);
        QTemporaryDir dir;
        const auto path = dir.path() + "/project.json";
        ProjectDocument original;
        original.editPlate([](auto &p) { p = fixturePlate(); });
        QVERIFY(ProjectSerializer::save(original, path).success);
        auto obj = readJson(path);
        auto plate = obj["model"].toObject();
        auto slab = plate["slab"].toObject();
        auto patches = plate["patches"].toArray();
        auto patch = patches[0].toObject();
        if (corruption == "units") obj["unitSystem"] = "m-N-Pa";
        if (corruption == "backend") obj["solver"] = QJsonObject{{"backendId", "unknown"}};
        if (corruption == "slabNumber") slab["Emod"] = true;
        if (corruption == "patchNumber") patch["q"] = "oops";
        if (corruption == "flag") patch["enabledForAnalysis"] = "false";
        if (corruption == "coordinates") plate["xNodes"] = "wrong";
        if (corruption == "schema") obj["schemaVersion"] = 0;
        if (corruption == "historyType") obj["runs"] = QJsonObject{{"runId", "lost-history"}};
        patches[0] = patch;
        if (corruption == "duplicateId") patches.append(patch);
        plate["patches"] = patches; plate["slab"] = slab; obj["model"] = plate;
        QVERIFY(writeBytes(path, QJsonDocument(obj).toJson()));
        auto result = ProjectSerializer::load(original, path);
        QVERIFY2(!result.success, qPrintable(corruption));
        QCOMPARE(original.plate().slab.emod, 30000.0);
        QCOMPARE(original.plate().patches[0].q, 0.7);
    }

    void fullFixtureAndExtrema()
    {
        auto result = fixtureResult();
        QVERIFY2(result.success, qPrintable(result.error));
        QCOMPARE(result.resultSet.availableFields().size(), 16);
        QVERIFY(result.resultSet.computeExtrema("w").valid);
        QCOMPARE(result.resultSet.computeDisplacementMagnitude().size(), 9);
    }

    void incompleteAndInconsistentResults_data()
    {
        QTest::addColumn<QString>("corruption");
        for (const auto &name : {"missing", "vector", "nan", "geometry", "connectivity", "ids"})
            QTest::newRow(name) << QString(name);
    }
    void incompleteAndInconsistentResults()
    {
        QFETCH(QString, corruption);
        QTemporaryDir dir;
        auto imported = fixtureResult(); QVERIFY(imported.success);
        auto *grid = imported.resultSet.grid();
        if (corruption == "missing") grid->GetPointData()->RemoveArray("sxy_membrane");
        if (corruption == "vector") grid->GetPointData()->GetArray("displacement")->SetComponent(3, 2, 999);
        if (corruption == "nan") grid->GetPointData()->GetArray("w")->SetTuple1(3, std::numeric_limits<double>::quiet_NaN());
        if (corruption == "geometry") grid->GetPoints()->SetPoint(4, 510, 500, 0);
        if (corruption == "ids") grid->GetPointData()->GetArray("solver_node_id")->SetTuple1(0, 8);
        if (corruption == "connectivity") {
            vtkIdType wrong[4] = {1, 0, 3, 4}; grid->ReplaceCell(0, 4, wrong);
        }
        vtkNew<vtkUnstructuredGridWriter> writer;
        const auto path = dir.path() + "/bad.vtk";
        writer->SetFileName(path.toUtf8().constData()); writer->SetInputData(grid); QVERIFY(writer->Write());
        auto reference = PlateMeshBuilder::build(fixturePlate());
        auto checked = ResultImporter::importFromFile(path, "bad", 9, 4, reference);
        QVERIFY2(!checked.success, qPrintable(corruption));
    }

    void silentProcessAndCancel()
    {
        QTemporaryDir dir;
        const auto path = dir.path() + "/input.json";
        QVERIFY(writeBytes(path, R"({"scenario":"hang"})"));
        SolverJob job;
        QSignalSpy done(&job, &SolverJob::finished);
        job.start(FEM_TEST_PYTHON, QString(FEM_SOURCE_DIR) + "/tests/fake_solver.py", dir.path(), path);
        QTRY_VERIFY_WITH_TIMEOUT(job.status() == RunStatus::Running, 5000);
        job.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 7000);
        QVERIFY(job.status() == RunStatus::Cancelled);
        QVERIFY(!done[0][0].toBool());
    }

    void processFailuresAndUnicode_data()
    {
        QTest::addColumn<QString>("scenario");
        for (const auto &name : {"exit7", "missing", "unicode", "success", "failedStart"})
            QTest::newRow(name) << QString(name);
    }
    void processFailuresAndUnicode()
    {
        QFETCH(QString, scenario);
        QTemporaryDir dir;
        const auto path = dir.path() + "/input.json";
        QVERIFY(writeBytes(path, QJsonDocument(QJsonObject{{"scenario", scenario}, {"fixture", fixturePath()}}).toJson()));
        SolverJob job;
        QSignalSpy done(&job, &SolverJob::finished);
        QSignalSpy logs(&job, &SolverJob::logLine);
        job.start(scenario == "failedStart" ? dir.path() + "/absent.exe" : QString(FEM_TEST_PYTHON),
                  QString(FEM_SOURCE_DIR) + "/tests/fake_solver.py", dir.path(), path);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        QCOMPARE(done[0][0].toBool(), scenario == "success");
        if (scenario == "exit7") QCOMPARE(job.exitCode(), 7);
        if (scenario == "unicode") {
            QStringList text;
            for (const auto &entry : logs) text << entry[1].toString();
            QVERIFY(text.contains(QString::fromUtf8("split → café")));
            QVERIFY(text.contains("last line"));
        }
    }

    void asyncBackendCheck()
    {
        ProjectDocument doc;
        AnalysisController analysis(&doc);
        QSignalSpy checked(&analysis, &AnalysisController::backendChecked);
        analysis.checkBackend();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, 20000);
        QVERIFY2(checked[0][0].toBool(), qPrintable(checked[0][1].toString()));
    }

    void productionProtocol_data()
    {
        QTest::addColumn<QString>("scenario");
        for (const auto &name : {"success", "malformed", "error", "exit7", "wrong_input", "wrong_result",
                                 "missing_manifest", "missing_completion", "duplicate_completion", "hang"})
            QTest::newRow(name) << QString(name);
    }

    void bundledSolverWorkflow()
    {
        const auto executable = qEnvironmentVariable("FEM_TEST_BUNDLED_SOLVER");
        if (executable.isEmpty()) QSKIP("Set FEM_TEST_BUNDLED_SOLVER to test a frozen package.");
        const auto savedDriver = QSettings().value("Backend/DriverPath");
        const auto savedPython = QSettings().value("Backend/PythonExecutable");
        const auto restoreSettings = qScopeGuard([&] {
            QSettings().setValue("Backend/DriverPath", savedDriver);
            QSettings().setValue("Backend/PythonExecutable", savedPython);
        });
        QTemporaryDir dir;
        ProjectController controller;
        controller.newProject(ModelKind::Plate);
        controller.document()->editPlate([](auto &p) { p = fixturePlate(); });
        QVERIFY(controller.saveAs(dir.path() + "/bundled project.json").success);
        controller.analysis()->setDriverPath(executable);
        controller.analysis()->setPythonExecutable(dir.path() + "/missing-python.exe");
        QVERIFY(controller.analysis()->backendFilesCheck().ok);
        QSignalSpy checked(controller.analysis(), &AnalysisController::backendChecked);
        controller.analysis()->checkBackend();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, 20000);
        QVERIFY2(checked[0][0].toBool(), qPrintable(checked[0][1].toString()));
        QSignalSpy done(controller.analysis(), &AnalysisController::runFinished);
        QVERIFY(!hasErrors(controller.analysis()->run()));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
        QVERIFY2(done[0][1].toBool(), qPrintable(done[0][2].toString()));
        QVERIFY(controller.document()->hasResultSet());
        const auto record = controller.document()->runs().last();
        QVERIFY(record.status == RunStatus::Completed);
        QVERIFY(!record.elementSha256.isEmpty());
        auto expected = fixtureResult(); QVERIFY(expected.success);
        auto *actual = controller.document()->resultSet().grid();
        for (const auto &field : plateFieldCatalog()) {
            if (field.isDerived) continue;
            auto *a = actual->GetPointData()->GetArray(field.arrayName.toUtf8().constData());
            auto *b = expected.resultSet.grid()->GetPointData()->GetArray(field.arrayName.toUtf8().constData());
            for (vtkIdType n = 0; n < a->GetNumberOfTuples(); ++n)
                for (int c = 0; c < field.componentCount; ++c)
                    QVERIFY(std::abs(a->GetComponent(n,c) - b->GetComponent(n,c)) <= 1e-7 + 1e-5 * std::abs(b->GetComponent(n,c)));
        }
    }
    void productionProtocol()
    {
        QFETCH(QString, scenario);
        QTemporaryDir dir;
        const auto script = dir.path() + "/plate_solver.py";
        QVERIFY(QFile::copy(QString(FEM_SOURCE_DIR) + "/tests/fake_production_solver.py", script));
        const auto input = dir.path() + "/input.json";
        QVERIFY(writeBytes(input, QJsonDocument(QJsonObject{{"scenario", scenario}, {"fixture", fixturePath()}}).toJson()));
        SolverJob job;
        QSignalSpy done(&job, &SolverJob::finished);
        QSignalSpy phases(&job, &SolverJob::phaseChanged);
        job.start(FEM_TEST_PYTHON, script, dir.path(), input);
        if (scenario == "hang") {
            QTRY_VERIFY_WITH_TIMEOUT(!phases.isEmpty(), 5000);
            job.cancel();
        }
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        QCOMPARE(done[0][0].toBool(), scenario == "success");
        QVERIFY(!phases.isEmpty());
        if (scenario == "hang") {
            QVERIFY(job.status() == RunStatus::Cancelled);
            QCOMPARE(job.exitCode(), 130); // cooperative exit, not the kill fallback
            QVERIFY(QFileInfo::exists(dir.path() + "/cancel.request"));
        }
    }

    void c3d8NumericalAcceptanceManifest()
    {
        const QStringList scenarios{"healthy_checks", "unhealthy_checks", "missing_acceptance"};
        for (const auto &scenario : scenarios) {
            QTemporaryDir dir;
            const auto script = dir.path() + "/c3d8_solver.py";
            QVERIFY(QFile::copy(QString(FEM_SOURCE_DIR) + "/tests/fake_production_solver.py", script));
            const auto input = dir.path() + "/input.json";
            const QJsonObject spec{{"scenario", scenario}, {"fixture", fixturePath()},
                                   {"backendId", "c3d8-python-v1"},
                                   {"modelKind", "structured_solid_c3d8"}};
            QVERIFY(writeBytes(input, QJsonDocument(spec).toJson()));
            SolverJob job;
            QSignalSpy done(&job, &SolverJob::finished);
            job.start(FEM_TEST_PYTHON, script, dir.path(), input,
                      "c3d8-python-v1", "structured_solid_c3d8");
            QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
            QCOMPARE(done[0][0].toBool(), scenario == "healthy_checks");
            if (scenario != "healthy_checks")
                QVERIFY(done[0][2].toString().contains("numerical acceptance", Qt::CaseInsensitive));
        }
    }

    void realRunMatchesReferenceAndReopens()
    {
        QTemporaryDir dir;
        ProjectController controller;
        controller.newProject(ModelKind::Plate);
        controller.document()->editPlate([](auto &p) { p = fixturePlate(); });
        const auto path = dir.path() + "/real project.femproject.json";
        QVERIFY(controller.saveAs(path).success);
        QSignalSpy done(controller.analysis(), &AnalysisController::runFinished);
        auto *original = controller.document();
        QVERIFY(controller.analysis()->driverPath().endsWith("plate_solver.py"));
        QSignalSpy phases(controller.analysis(), &AnalysisController::phaseChanged);
        QVERIFY(!hasErrors(controller.analysis()->run()));
        QVERIFY(controller.analysis()->isRunning());
        controller.newProject(); QCOMPARE(controller.document(), original);
        QVERIFY(!controller.open(path).success);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
        QVERIFY2(done[0][1].toBool(), qPrintable(done[0][2].toString()));
        QVERIFY(controller.document()->hasResultSet());
        QVERIFY(!phases.isEmpty());
        auto expected = fixtureResult(); QVERIFY(expected.success);
        auto *actual = controller.document()->resultSet().grid();
        for (const auto &field : plateFieldCatalog()) {
            if (field.isDerived) continue;
            auto *a = actual->GetPointData()->GetArray(field.arrayName.toUtf8().constData());
            auto *b = expected.resultSet.grid()->GetPointData()->GetArray(field.arrayName.toUtf8().constData());
            for (vtkIdType n = 0; n < a->GetNumberOfTuples(); ++n)
                for (int c = 0; c < field.componentCount; ++c)
                    QVERIFY2(std::abs(a->GetComponent(n,c) - b->GetComponent(n,c)) <= 1e-7 + 1e-5 * std::abs(b->GetComponent(n,c)), qPrintable(field.arrayName));
        }
        auto record = controller.document()->runs().last();
        QVERIFY(record.status == RunStatus::Completed);
        QCOMPARE(record.exitCode, 0);
        QVERIFY(!record.inputSha256.isEmpty());
        QCOMPARE(readJson(record.runDirectory + "/run.json")["status"].toString(), QString("Completed"));
        const auto manifest = readJson(record.runDirectory + "/output_python_square/run-result.json");
        QCOMPARE(manifest["status"].toString(), QString("Completed"));
        QCOMPARE(manifest["inputSha256"].toString(), record.inputSha256);
        QVERIFY(controller.save().success);
        QVERIFY(controller.open(path).success);
        QVERIFY(controller.document()->hasResultSet());
        QVERIFY(!controller.document()->isResultSetStale());
        controller.document()->editPlate([](auto &p) { p.xNodes[1] = 400; });
        QVERIFY(controller.document()->isResultSetStale());
        QVERIFY(controller.save().success);
        QVERIFY(controller.open(path).success);
        QVERIFY(controller.document()->isResultSetStale());
        double xyz[3]; controller.document()->resultSet().grid()->GetPoint(1, xyz);
        QCOMPARE(xyz[0], 500.0);
        verifySavedResultIntegrity(controller, record);
    }

    void solidRealRunMatchesReferenceAndReopens()
    {
        const auto savedC3D8Driver = QSettings().value("Backend/C3D8DriverPath");
        const auto restoreC3D8Driver = qScopeGuard([&] { QSettings().setValue("Backend/C3D8DriverPath", savedC3D8Driver); });
        const auto bundledC3D8 = qEnvironmentVariable("FEM_TEST_BUNDLED_C3D8_SOLVER");
        if (!bundledC3D8.isEmpty()) QSettings().setValue("Backend/C3D8DriverPath", bundledC3D8);
        QTemporaryDir dir;
        SolidDefinition solid;
        solid.xNodes = {0, 170}; solid.yNodes = {0, 280}; solid.zNodes = {0, 63};
        solid.material = {24000, 0.3}; solid.foundation = {50, 50, 50};
        PressurePatch patch;
        patch.name = "Full top-face load";
        patch.x1 = 0; patch.x2 = 170; patch.y1 = 0; patch.y2 = 280; patch.q = 0.8;
        solid.patches.append(patch);

        const auto path = dir.path() + "/solid project.femproject.json";
        ProjectDocument source;
        source.replaceModelForLoad(solid);
        source.restoreModelRevision(1);
        QVERIFY(ProjectSerializer::save(source, path).success);

        ProjectController controller;
        QVERIFY(controller.open(path).success);
        QVERIFY(controller.document()->isSolid());
        QVERIFY(controller.analysis()->driverPath().endsWith(bundledC3D8.isEmpty() ? "c3d8_solver.py" : "FemSolver.exe",
                                                               Qt::CaseInsensitive));
        QVERIFY2(controller.analysis()->backendFilesCheck().ok,
                 qPrintable(controller.analysis()->backendFilesCheck().message));
        QSignalSpy checked(controller.analysis(), &AnalysisController::backendChecked);
        controller.analysis()->checkBackend();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, 20000);
        QVERIFY2(checked[0][0].toBool(), qPrintable(checked[0][1].toString()));

        QSignalSpy done(controller.analysis(), &AnalysisController::runFinished);
        QSignalSpy phases(controller.analysis(), &AnalysisController::phaseChanged);
        QVERIFY(!hasErrors(controller.analysis()->run()));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
        QVERIFY2(done[0][1].toBool(), qPrintable(done[0][2].toString()));
        QVERIFY(!phases.isEmpty());
        QVERIFY(controller.document()->hasResultSet());
        QCOMPARE(controller.document()->resultSet().modelKind(), ModelKind::SolidC3D8);
        QCOMPARE(controller.document()->resultSet().availableFields().size(), 17);

        auto reference = ResultImporter::importFromFile(solidResultPath(), "reference", 8, 1,
            C3D8MeshBuilder::build(solid), ModelKind::SolidC3D8);
        QVERIFY2(reference.success, qPrintable(reference.error));
        auto *actual = controller.document()->resultSet().grid();
        for (const auto &field : solidFieldCatalog()) {
            if (field.isDerived) continue;
            auto *a = actual->GetPointData()->GetArray(field.arrayName.toUtf8().constData());
            auto *b = reference.resultSet.grid()->GetPointData()->GetArray(field.arrayName.toUtf8().constData());
            QVERIFY(a && b);
            for (vtkIdType n = 0; n < a->GetNumberOfTuples(); ++n)
                for (int c = 0; c < field.componentCount; ++c)
                    QVERIFY2(std::abs(a->GetComponent(n, c) - b->GetComponent(n, c))
                             <= 1e-10 + 1e-8 * std::abs(b->GetComponent(n, c)), qPrintable(field.arrayName));
        }

        const auto record = controller.document()->runs().last();
        QVERIFY(record.status == RunStatus::Completed);
        QCOMPARE(record.backendId, QString("c3d8-python-v1"));
        QCOMPARE(record.modelKind, QString("structured_solid_c3d8"));
        QVERIFY(!record.elementSha256.isEmpty());
        const auto manifest = readJson(record.runDirectory + "/output_python_square/run-result.json");
        QCOMPARE(manifest["backendId"].toString(), record.backendId);
        QCOMPARE(manifest["modelKind"].toString(), record.modelKind);
        QVERIFY(controller.save().success);
        QVERIFY(controller.open(path).success);
        QVERIFY(controller.document()->hasResultSet());
        QVERIFY(!controller.document()->isResultSetStale());
        controller.document()->editSolid([](auto &value) { value.xNodes[1] = 180; });
        QVERIFY(controller.document()->isResultSetStale());
        verifySavedResultIntegrity(controller, record);
    }

    void deformationHighlightAndResultReset()
    {
        auto imported = fixtureResult(); QVERIFY(imported.success);
        ViewportWidget viewport(ViewportWidget::ViewMode::Results);
        viewport.setResultSet(&imported.resultSet);
        viewport.m_deformedCheck->setChecked(true);
        viewport.m_deformationSlider->setValue(100);
        viewport.highlightNode(0);
        double displayed[3]; viewport.m_resultMapper->GetInput()->GetPoint(0, displayed);
        for (int d = 0; d < 3; ++d) QCOMPARE(viewport.m_highlightSphere->GetCenter()[d], displayed[d]);
        viewport.highlightElement(0);
        QCOMPARE(viewport.m_highlightMapper->GetInput()->GetNumberOfPoints(), vtkIdType(4));
        viewport.setResultSet(nullptr);
        QVERIFY(!viewport.m_highlightActor->GetVisibility());
        QVERIFY(!viewport.m_deformedCheck->isEnabled());
        QVERIFY(!viewport.m_resultMapper->GetInput());
    }

    void staleSelectionUsesRunGeometry()
    {
        MainWindow window;
        window.m_controller->newProject(ModelKind::Plate);
        auto *doc = window.m_controller->document();
        doc->editPlate([](auto &p) { p = fixturePlate(); });
        RunRecord run; run.runId = "fixture"; run.status = RunStatus::Completed;
        run.hasInputSnapshot = true; run.inputSnapshot = fixturePlate(); run.modelRevisionAtLaunch = doc->modelRevision();
        doc->addRun(run);
        auto imported = fixtureResult(); QVERIFY(imported.success);
        doc->setResultSet(imported.resultSet, run.modelRevisionAtLaunch);
        doc->editPlate([](auto &p) { p.xNodes[1] = 425; });
        emit window.m_resultsViewport->nodePicked(1);
        QVERIFY(labels(window.m_inspectorPanel).contains("500 mm"));
        QVERIFY(!labels(window.m_inspectorPanel).contains("425 mm"));
        QVERIFY(!window.m_meshViewport->m_highlightActor->GetVisibility());
        emit window.m_meshViewport->nodePicked(1);
        QVERIFY(labels(window.m_inspectorPanel).contains("425 mm"));
        QVERIFY(!window.m_resultsViewport->m_highlightActor->GetVisibility());
        QVERIFY(!labels(window.m_inspectorPanel).contains("Out-of-plane displacement"));
    }

    void invalidTableTextDoesNotBecomeZero()
    {
        MainWindow window;
        window.m_controller->newProject(ModelKind::Plate);
        window.m_projectPanel->navigateToField("loads");
        QTableWidget *loads = nullptr;
        for (auto *table : window.m_projectPanel->findChildren<QTableWidget *>())
            if (table->columnCount() == 8) loads = table;
        QVERIFY(loads);
        const double before = window.m_controller->document()->plate().patches[0].q;
        loads->item(0, 5)->setText("not a number");
        QCOMPARE(window.m_controller->document()->plate().patches[0].q, before);
        QCOMPARE(loads->item(0, 5)->text().toDouble(), before);
    }

    void preciseEditorsAndUnchangedApply()
    {
        MainWindow window;
        window.m_controller->newProject(ModelKind::Plate);
        auto *doc = window.m_controller->document();
        doc->editPlate([](auto &p) { p = fixturePlate(); p.foundation.kx = 1.234567890123456e-10; p.slab.emod = 30000.123456789; });
        window.m_projectPanel->refreshAll();
        const int revision = doc->modelRevision();
        window.m_projectPanel->applyFoundation();
        window.m_projectPanel->applyMaterial();
        QCOMPARE(doc->modelRevision(), revision);
        QCOMPARE(doc->plate().foundation.kx, 1.234567890123456e-10);
        QCOMPARE(doc->plate().slab.emod, 30000.123456789);
        window.m_projectPanel->m_kxSpin->setText("invalid");
        window.m_projectPanel->applyFoundation();
        QCOMPARE(doc->modelRevision(), revision);
        GeometryEditorDialog geometry(doc->plate().xNodes, doc->plate().yNodes, {}, false);
        geometry.m_x.values->setPlainText("0\n0\n1000");
        QVERIFY(!geometry.m_buttons->button(QDialogButtonBox::Ok)->isEnabled());
        QCOMPARE(doc->modelRevision(), revision);
    }

    void guidedWorkflowAndActionStates()
    {
        MainWindow window;
        auto *panel = window.m_projectPanel;
        QCOMPARE(panel->m_tree->topLevelItemCount(), 7);
        QCOMPARE(panel->m_tree->topLevelItem(0)->text(0), QString("Overview"));
        QCOMPARE(panel->m_tree->topLevelItem(1)->text(0), QString("1  Model"));
        QCOMPARE(panel->m_tree->topLevelItem(2)->text(0), QString("2  Material & Foundation"));
        QCOMPARE(panel->m_tree->topLevelItem(3)->text(0), QString("3  Loads"));
        QCOMPARE(panel->m_tree->topLevelItem(4)->text(0), QString("4  Validate & Run"));
        QCOMPARE(panel->m_tree->topLevelItem(5)->text(0), QString("5  Results"));
        QCOMPARE(panel->m_tree->topLevelItem(6)->text(0), QString("Advanced"));
        for (int row = 0; row < panel->m_tree->topLevelItemCount(); ++row)
            QVERIFY(panel->m_tree->topLevelItem(row)->icon(1).isNull());

        QStringList buttonText;
        for (auto *button : panel->findChildren<QPushButton *>()) buttonText << button->text();
        QVERIFY(!buttonText.contains("New C3D8"));
        QVERIFY(!buttonText.contains("Import Solver JSON"));
        QVERIFY(!buttonText.contains("Plate on elastic foundation"));
        QVERIFY(buttonText.contains("Edit Coordinates..."));
        QVERIFY(buttonText.contains("New Pressure Load"));
        QVERIFY(!buttonText.contains("Add Patch"));
        QVERIFY(!buttonText.contains("Draw Rectangle"));
        QPushButton *newLoad = nullptr;
        for (auto *button : panel->findChildren<QPushButton *>())
            if (button->text() == "New Pressure Load") newLoad = button;
        QVERIFY(newLoad);
        QVERIFY(newLoad->menu());
        QCOMPARE(newLoad->menu()->actions().size(), 2);

        auto *mainToolbar = window.findChild<QToolBar *>();
        QVERIFY(mainToolbar);
        QStringList toolbarActions;
        for (auto *action : mainToolbar->actions()) toolbarActions << action->text();
        QVERIFY(!toolbarActions.contains("Validate"));
        QVERIFY(!toolbarActions.contains("Run Analysis"));
        QVERIFY(!toolbarActions.contains("Fit View"));

        panel->setAnalysisActionState(true, false);
        QVERIFY(panel->m_runButton->isEnabled());
        QVERIFY(panel->m_stopButton->isHidden());
        QVERIFY(!panel->m_showResultsButton->isEnabled());
        QVERIFY(!panel->m_exportCsvButton->isEnabled());
        QVERIFY(!panel->m_exportImageButton->isEnabled());
        panel->setAnalysisActionState(false, true);
        QVERIFY(panel->m_runButton->isHidden());
        QVERIFY(!panel->m_stopButton->isHidden());
        QVERIFY(panel->m_stopButton->isEnabled());

        panel->navigateToField("backend");
        QCOMPARE(panel->m_details->currentIndex(), 8);
        panel->navigateToField("loads");
        QCOMPARE(panel->m_details->currentIndex(), 5);

        GeometryEditorDialog geometry({0, 500, 1000}, {0, 500, 1000}, {0, 150, 300}, true);
        geometry.m_x.values->setPlainText("0, 125; 500");
        QCOMPARE(geometry.xCoordinates(), QVector<double>({0, 125, 500}));
        geometry.m_y.start->setText("0");
        geometry.m_y.end->setText("1000");
        geometry.m_y.intervals->setValue(4);
        geometry.generateAxis(geometry.m_y);
        QCOMPARE(geometry.yCoordinates(), QVector<double>({0, 250, 500, 750, 1000}));
        geometry.m_buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(geometry.result(), int(QDialog::Accepted));

        ScientificNumberEdit number;
        const double exact = 1.2345678901234567;
        number.setValue(exact);
        QCOMPARE(number.value(), exact);
        number.setText("2.5e-7");
        QCOMPARE(number.value(), 2.5e-7);
    }

    void undoRevisionBranchAndDisplayEdits()
    {
        ProjectDocument doc;
        doc.editPlate([](auto &p) { p = fixturePlate(); });
        const auto initial = doc.modelRevision();
        doc.editPlate([](auto &p) { p.patches[0].visible = false; });
        QCOMPARE(doc.modelRevision(), initial);
        doc.undoStack()->undo();
        QVERIFY(doc.plate().patches[0].visible);
        doc.editPlate([](auto &p) { p.slab.emod *= 2; });
        const auto abandoned = doc.modelRevision();
        doc.undoStack()->undo();
        QCOMPARE(doc.modelRevision(), initial);
        doc.editPlate([](auto &p) { p.slab.emod *= 3; });
        QVERIFY(doc.modelRevision() > abandoned);
        QVERIFY(!doc.undoStack()->canRedo());
    }

    void newProjectSaveOpensSaveAs()
    {
        QTemporaryDir dir;
        MainWindow window;
        const auto path = dir.path() + "/new.femproject.json";
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            dialog->selectFile(path);
            QMetaObject::invokeMethod(dialog, "accept");
        });
        // A watchdog makes a broken dialog interaction fail rather than hang the suite.
        QTimer watchdog;
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, &window, [] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        watchdog.start(5000);
        QVERIFY(window.doSaveProject());
        QVERIFY(QFileInfo::exists(path));
    }

    void newProjectActionCreatesC3D8WithoutModelChooser()
    {
        MainWindow window;
        auto *previous = window.m_controller->document();
        window.doNewProject();
        QVERIFY(window.m_controller->document() != previous);
        QVERIFY(window.m_controller->document()->isSolid());
        QVERIFY(!hasErrors(window.m_controller->validate()));
        QCOMPARE(QApplication::activeModalWidget(), nullptr);
    }

    void patchDialogRejectsInvalidDraft()
    {
        auto plate = fixturePlate();
        auto patch = plate.patches[0];
        patch.x1 = -5;
        PressurePatchDialog dialog(plate, patch);
        QVERIFY(!dialog.m_buttons->button(QDialogButtonBox::Ok)->isEnabled());
        QCOMPARE(dialog.patch().x1, -5.0); // outside bounds are not silently clamped
        dialog.m_x1->setText("250");
        QVERIFY(dialog.m_buttons->button(QDialogButtonBox::Ok)->isEnabled());
        dialog.m_q->setText("1.23456789e-9");
        QCOMPARE(dialog.patch().q, 1.23456789e-9);
        dialog.m_q->setText("-1");
        QVERIFY(!dialog.m_buttons->button(QDialogButtonBox::Ok)->isEnabled());
        dialog.m_q->setText("nan");
        QVERIFY(!dialog.m_buttons->button(QDialogButtonBox::Ok)->isEnabled());
    }

    void drawnPatchApplyIsOneUndoableEdit()
    {
        MainWindow window;
        window.m_controller->newProject(ModelKind::Plate);
        auto *doc = window.m_controller->document();
        doc->editPlate([](auto &p) { p = fixturePlate(); });
        const int beforeCount = doc->plate().patches.size(), beforeRevision = doc->modelRevision();
        QTimer::singleShot(0, &window, [] {
            if (auto *dialog = dynamic_cast<PressurePatchDialog *>(QApplication::activeModalWidget())) {
                dialog->m_q->setText("0.25");
                dialog->m_buttons->button(QDialogButtonBox::Ok)->click();
            }
        });
        QTimer watchdog; watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, &window, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        watchdog.start(5000);
        window.acceptDrawnPatch(QRectF(100, 200, 300, 400));
        QCOMPARE(doc->plate().patches.size(), beforeCount + 1);
        const auto patch = doc->plate().patches.last();
        QCOMPARE(patch.x2, 400.0); QCOMPARE(patch.y2, 600.0); QCOMPARE(patch.q, 0.25);
        QCOMPARE(doc->modelRevision(), beforeRevision + 1);
        QVERIFY(labels(window.m_inspectorPanel).contains("0.25 MPa"));
        window.m_undoAction->trigger();
        QCOMPARE(doc->plate().patches.size(), beforeCount);
        QCOMPARE(doc->modelRevision(), beforeRevision);
        window.m_redoAction->trigger();
        QCOMPARE(doc->plate().patches.last().id, patch.id);
    }

    void nodalCsvAndDerivedExtrema()
    {
        auto imported = fixtureResult(); QVERIFY(imported.success);
        const auto extrema = imported.resultSet.computeExtrema("__displacement_magnitude");
        QVERIFY(extrema.valid); QCOMPARE(extrema.maxAbsoluteNodeId, vtkIdType(4));
        QTemporaryDir dir;
        QString error;
        const auto path = dir.path() + "/results.csv";
        QVERIFY(ResultExporter::writeCsv(imported.resultSet, 12, true, path, &error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto rows = QString::fromUtf8(file.readAll()).trimmed().split('\n');
        QCOMPARE(rows.size(), 10);
        QVERIFY(rows[0].contains("sxy_membrane [MPa]"));
        QVERIFY(rows[0].contains("__displacement_magnitude [mm]"));
        const auto node = rows[2].split(',');
        QCOMPARE(node[1], QString("12"));
        QCOMPARE(node[2], QString("Previous model revision"));
        QCOMPARE(node[4], QString("1"));
        QCOMPARE(node[5].toDouble(), 500.0);
        QVERIFY(!ResultExporter::writeCsv(ResultSet(), 0, false, dir.path() + "/empty.csv", &error));
    }

    void nativeRectangleDrawing()
    {
        if (!qEnvironmentVariableIsSet("FEM_TEST_RENDER")) QSKIP("Requires Windows desktop rendering.");
        ViewportWidget viewport(ViewportWidget::ViewMode::Model);
        viewport.setPlateMesh(PlateMeshBuilder::build(fixturePlate()));
        viewport.resize(900, 700); viewport.show();
        QVERIFY(QTest::qWaitForWindowExposed(&viewport));
        viewport.startRectangleDrawing();
        QTest::qWait(150);
        auto screen = [&](double x, double y) {
            viewport.m_renderer->SetWorldPoint(x, y, 0, 1); viewport.m_renderer->WorldToDisplay();
            const double *p = viewport.m_renderer->GetDisplayPoint();
            const double ratio = viewport.m_vtkWidget->devicePixelRatioF();
            return QPoint(qRound(p[0] / ratio), viewport.m_vtkWidget->height() - 1 - qRound(p[1] / ratio));
        };
        const auto first = screen(750, 750), last = screen(250, 250);
        QSignalSpy drafts(&viewport, &ViewportWidget::rectangleDrafted);
        QTest::mousePress(viewport.m_vtkWidget, Qt::LeftButton, Qt::NoModifier, first);
        QTest::mouseMove(viewport.m_vtkWidget, last);
        QTest::mouseRelease(viewport.m_vtkWidget, Qt::LeftButton, Qt::NoModifier, last);
        QCOMPARE(drafts.count(), 1);
        const QRectF bounds = drafts[0][0].toRectF();
        QVERIFY(std::abs(bounds.left() - 250) < 5); QVERIFY(std::abs(bounds.right() - 750) < 5);
        QVERIFY(std::abs(bounds.top() - 250) < 5); QVERIFY(std::abs(bounds.bottom() - 750) < 5);
        viewport.startRectangleDrawing();
        QTest::keyClick(viewport.m_vtkWidget, Qt::Key_Escape);
        QVERIFY(!viewport.m_drawing); QCOMPARE(drafts.count(), 1);
        viewport.startRectangleDrawing(); viewport.setCameraPreset("Front");
        QPointF point; QVERIFY(!viewport.projectToPlate(200, 200, point));
        viewport.close();
    }

    void nativeRenderingAndPicking()
    {
        if (!qEnvironmentVariableIsSet("FEM_TEST_RENDER")) QSKIP("Native rendering is tested separately with the Windows platform.");
        MainWindow window;
        window.m_controller->newProject(ModelKind::Plate);
        auto *doc = window.m_controller->document();
        doc->editPlate([](auto &p) { p = fixturePlate(); });
        RunRecord run; run.runId = "fixture"; run.status = RunStatus::Completed;
        run.inputSnapshot = fixturePlate(); run.hasInputSnapshot = true; run.modelRevisionAtLaunch = doc->modelRevision();
        doc->addRun(run);
        auto result = fixtureResult(); QVERIFY(result.success);
        doc->setResultSet(result.resultSet, run.modelRevisionAtLaunch);
        window.m_centerTabs->setCurrentWidget(window.m_resultsViewport);
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *viewport = window.m_resultsViewport;
        viewport->setCameraPreset("Top");
        QTest::qWait(300);
        QVERIFY(viewport->m_vtkWidget->isValid());
        auto image = viewport->m_vtkWidget->grabFramebuffer();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(QString(FEM_SOURCE_DIR) + "/build/review-results-viewport.png"));
        QVERIFY(window.grab().save(QString(FEM_SOURCE_DIR) + "/build/review-results-window.png"));
        QVERIFY2(window.width() <= 1450, "Toolbar/form minimum widths make the window wider than requested.");
        viewport->m_pickCombo->setCurrentIndex(1);
        auto *renderer = viewport->m_renderer.Get();
        renderer->SetWorldPoint(500, 500, 0, 1);
        renderer->WorldToDisplay();
        double *display = renderer->GetDisplayPoint();
        const double ratio = viewport->m_vtkWidget->devicePixelRatioF();
        const QPoint click(qRound(display[0] / ratio), viewport->m_vtkWidget->height() - 1 - qRound(display[1] / ratio));
        QSignalSpy picked(viewport, &ViewportWidget::nodePicked);
        QTest::mouseClick(viewport->m_vtkWidget, Qt::LeftButton, Qt::NoModifier, click);
        QTRY_COMPARE(picked.count(), 1);
        QCOMPARE(picked[0][0].value<vtkIdType>(), vtkIdType(4));
        viewport->goToExtremum(2);
        QCOMPARE(viewport->m_selectedNode, vtkIdType(4));
        QString exportError;
        QVERIFY2(viewport->exportScreenshot(QString(FEM_SOURCE_DIR) + "/build/review-phase6-report.png",
                                           doc->modelRevision(), false, &exportError), qPrintable(exportError));
        doc->markClean();
        window.close();
    }

    void nativeGuidedC3D8Workspace()
    {
        if (!qEnvironmentVariableIsSet("FEM_TEST_RENDER")) QSKIP("Native rendering is tested separately with the Windows platform.");
        MainWindow window;
        QVERIFY(window.m_controller->document()->isSolid());
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(300);
        QVERIFY(window.grab().save(QString(FEM_SOURCE_DIR) + "/build/review-c3d8-workflow.png"));
        QVERIFY2(window.width() <= 1450, "Workflow panels make the window wider than requested.");
        auto *viewport = window.m_modelViewport;
        QVERIFY(viewport->m_patchActor->GetVisibility());
        viewport->m_renderer->GetActiveCamera()->Azimuth(30.0);
        viewport->m_renderer->GetActiveCamera()->Elevation(12.0);
        viewport->m_renderer->GetActiveCamera()->Zoom(1.6);
        viewport->m_renderer->ResetCameraClippingRange();
        viewport->render();
        QTest::qWait(150);
        QVERIFY(viewport->m_patchActor->GetVisibility());
        QVERIFY(viewport->m_vtkWidget->grabFramebuffer().save(QString(FEM_SOURCE_DIR) + "/build/review-patch-rotated.png"));
        viewport->m_renderer->GetActiveCamera()->Azimuth(150.0);
        viewport->m_renderer->ResetCameraClippingRange();
        viewport->render();
        QTest::qWait(100);
        QVERIFY(viewport->m_patchActor->GetVisibility());
        QVERIFY(viewport->m_vtkWidget->grabFramebuffer().save(QString(FEM_SOURCE_DIR) + "/build/review-patch-backside.png"));
        auto *camera = viewport->m_renderer->GetActiveCamera();
        camera->SetPosition(1.0, -1.0, 1.0);
        camera->SetFocalPoint(0.0, 0.0, 0.0);
        camera->SetViewUp(0.0, 0.0, 1.0);
        viewport->m_renderer->ResetCamera();
        viewport->render();
        QTest::qWait(100);
        QVERIFY(viewport->m_patchActor->GetVisibility());
        QVERIFY(viewport->m_vtkWidget->grabFramebuffer().save(QString(FEM_SOURCE_DIR) + "/build/review-patch-opposite-side.png"));

        const auto &solid = window.m_controller->document()->solid();
        GeometryEditorDialog geometry(solid.xNodes, solid.yNodes, solid.zNodes, true, &window);
        geometry.show();
        QVERIFY(QTest::qWaitForWindowExposed(&geometry));
        QVERIFY(geometry.grab().save(QString(FEM_SOURCE_DIR) + "/build/review-geometry-editor.png"));
        geometry.close();
        window.m_controller->document()->markClean();
        window.close();
    }

    void captureWeek3SolidResults()
    {
        if (!qEnvironmentVariableIsSet("FEM_TEST_RENDER")) QSKIP("Requires Windows desktop rendering.");

        SolidDefinition solid;
        solid.xNodes = {0, 170}; solid.yNodes = {0, 280}; solid.zNodes = {0, 63};
        solid.material = {24000, 0.3}; solid.foundation = {50, 50, 50};
        PressurePatch patch;
        patch.name = "Full top-face load";
        patch.x1 = 0; patch.x2 = 170; patch.y1 = 0; patch.y2 = 280; patch.q = 0.8;
        solid.patches.append(patch);

        MainWindow window;
        auto *document = window.m_controller->document();
        document->replaceModelForLoad(solid);
        document->restoreModelRevision(1);
        QTemporaryDir runWorkspace;
        QVERIFY(runWorkspace.isValid());
        const auto saved = window.m_controller->saveAs(runWorkspace.path() + "/week3-solid.femproject.json");
        QVERIFY2(saved.success, qPrintable(saved.error));
        QVERIFY2(window.m_controller->analysis()->backendFilesCheck().ok,
                 qPrintable(window.m_controller->analysis()->backendFilesCheck().message));
        QSignalSpy checked(window.m_controller->analysis(), &AnalysisController::backendChecked);
        window.m_controller->analysis()->checkBackend();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, 20000);
        QVERIFY2(checked[0][0].toBool(), qPrintable(checked[0][1].toString()));
        QSignalSpy done(window.m_controller->analysis(), &AnalysisController::runFinished);
        const auto runIssues = window.m_controller->analysis()->run();
        QStringList runMessages;
        for (const auto &issue : runIssues) runMessages.append(issue.message);
        QVERIFY2(!hasErrors(runIssues), qPrintable(runMessages.join(" | ")));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
        QVERIFY2(done[0][1].toBool(), qPrintable(done[0][2].toString()));
        QVERIFY(document->hasResultSet());
        QCOMPARE(document->resultSet().modelKind(), ModelKind::SolidC3D8);
        window.m_centerTabs->setCurrentWidget(window.m_resultsViewport);
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *viewport = window.m_resultsViewport;
        viewport->setCameraPreset("Isometric");
        const int stressIndex = viewport->m_fieldCombo->findData("szz");
        QCOMPARE(viewport->m_fieldCombo->count(), 16);
        QVERIFY(stressIndex >= 0);
        viewport->m_fieldCombo->setCurrentIndex(stressIndex);
        QTest::qWait(350);
        QVERIFY(viewport->m_vtkWidget->isValid());
        QVERIFY(!labels(window.m_projectPanel).contains("convergence verified", Qt::CaseInsensitive));
        QVERIFY(window.grab().save(QString(FEM_SOURCE_DIR) + "/build/week3-solid-results-window.png"));
        QVERIFY(viewport->m_vtkWidget->grabFramebuffer().save(QString(FEM_SOURCE_DIR) + "/build/week3-solid-results-viewport.png"));
        document->markClean();
        window.close();
    }

    void uxImprovementsWorkflow()
    {
        MainWindow window;
        auto *doc = window.m_controller->document();
        QVERIFY(doc != nullptr);

        // 1. Navigation synchronization
        QCOMPARE(window.m_centerTabs->currentIndex(), 0); // Model tab
        window.m_centerTabs->setCurrentIndex(1); // Switch to Mesh tab
        QCOMPARE(window.m_projectPanel->m_details->currentIndex(), 4); // Mesh Summary page

        window.m_centerTabs->setCurrentIndex(2); // Switch to Results tab
        QCOMPARE(window.m_projectPanel->m_details->currentIndex(), 7); // Results Dashboard page

        window.m_centerTabs->setCurrentIndex(0); // Switch to Model tab
        QCOMPARE(window.m_projectPanel->m_details->currentIndex(), 3); // Coordinates page for Model tab

        // 2. Material and Foundation presets
        auto *panel = window.m_projectPanel;
        QVERIFY(panel->m_materialPresetCombo != nullptr);
        int steelIdx = -1;
        for (int i = 0; i < panel->m_materialPresetCombo->count(); ++i) {
            if (panel->m_materialPresetCombo->itemText(i).contains("Steel S355")) {
                steelIdx = i;
                break;
            }
        }
        QVERIFY(steelIdx >= 0);
        panel->m_materialPresetCombo->setCurrentIndex(steelIdx);
        QCOMPARE(panel->m_emodSpin->value(), 210000.0);
        QCOMPARE(panel->m_nuSpin->value(), 0.30);

        int clayIdx = -1;
        for (int i = 0; i < panel->m_foundationPresetCombo->count(); ++i) {
            if (panel->m_foundationPresetCombo->itemText(i).contains("Soft Soil")) {
                clayIdx = i;
                break;
            }
        }
        QVERIFY(clayIdx >= 0);
        panel->m_foundationPresetCombo->setCurrentIndex(clayIdx);
        QCOMPARE(panel->m_kzSpin->value(), 0.015);

        // 3. Geometry Editor uniform grid generator
        GeometryEditorDialog geomDialog({0.0, 1000.0}, {0.0, 1000.0}, {0.0, 200.0}, false, &window);
        geomDialog.m_gridLx->setText("2400");
        geomDialog.m_gridNx->setValue(6);
        geomDialog.generateUniformGrid();
        const auto xCoords = geomDialog.xCoordinates();
        QCOMPARE(xCoords.size(), 7);
        QCOMPARE(xCoords.first(), 0.0);
        QCOMPARE(xCoords.last(), 2400.0);
        QCOMPARE(xCoords[1], 400.0);

        // 4. Viewport camera presets
        auto *viewport = window.m_modelViewport;
        viewport->setCameraPreset("Top");
        viewport->setCameraPreset("Front");
        viewport->setCameraPreset("Isometric");

        // 5. Total load summary calculation
        panel->refreshLoadsPage();
        QVERIFY(!panel->m_totalLoadSummaryLabel->text().isEmpty());

        doc->markClean();
        window.close();
    }

    void liquidGlassThemeWorkflow()
    {
        auto *app = qobject_cast<QApplication *>(QApplication::instance());
        QVERIFY(app);

        // 1. Verify Dark Liquid Glass
        UiStyle::setTheme(*app, UiStyle::Theme::LiquidGlassDark);
        QCOMPARE(UiStyle::currentTheme(), UiStyle::Theme::LiquidGlassDark);
        QCOMPARE(UiStyle::themeName(UiStyle::Theme::LiquidGlassDark), QStringLiteral("Liquid Glass Dark"));
        QVERIFY(UiStyle::isDark(app->palette()));
        QCOMPARE(UiStyle::viewportBackground(), QColor(11, 14, 20));
        QCOMPARE(UiStyle::viewportBackgroundAccent(), QColor(24, 35, 53));

        // 2. Verify Light Liquid Glass
        UiStyle::setTheme(*app, UiStyle::Theme::LiquidGlassLight);
        QCOMPARE(UiStyle::currentTheme(), UiStyle::Theme::LiquidGlassLight);
        QCOMPARE(UiStyle::themeName(UiStyle::Theme::LiquidGlassLight), QStringLiteral("Liquid Glass Light"));
        QVERIFY(!UiStyle::isDark(app->palette()));
        QCOMPARE(UiStyle::viewportBackground(), QColor(237, 243, 249));
        QCOMPARE(UiStyle::viewportBackgroundAccent(), QColor(215, 227, 238));

        // 3. Verify Classic System
        UiStyle::setTheme(*app, UiStyle::Theme::ClassicSystem);
        QCOMPARE(UiStyle::currentTheme(), UiStyle::Theme::ClassicSystem);
        QCOMPARE(UiStyle::themeName(UiStyle::Theme::ClassicSystem), QStringLiteral("Classic System"));

        // 4. Test MainWindow integration & Viewport refresh
        MainWindow window;
        window.onThemeChanged(UiStyle::Theme::LiquidGlassDark);
        QVERIFY(window.m_themeGlassDarkAction);
        QVERIFY(window.m_themeGlassDarkAction->isChecked());
        QCOMPARE(UiStyle::currentTheme(), UiStyle::Theme::LiquidGlassDark);

        // Trigger action
        window.m_themeGlassLightAction->trigger();
        QCOMPARE(UiStyle::currentTheme(), UiStyle::Theme::LiquidGlassLight);
        QVERIFY(window.m_themeGlassLightAction->isChecked());

        // Refresh viewports without crash
        window.m_modelViewport->refreshThemeColors();
        window.m_meshViewport->refreshThemeColors();
        window.m_resultsViewport->refreshThemeColors();

        // Restore default Liquid Glass Dark
        window.onThemeChanged(UiStyle::Theme::LiquidGlassDark);
        QCOMPARE(UiStyle::currentTheme(), UiStyle::Theme::LiquidGlassDark);

        window.close();
    }

    void dockIslandsAndRunSimulationButtonWorkflow()
    {
        MainWindow window;
        QVERIFY(window.m_projectDock);
        QVERIFY(window.m_inspectorDock);
        QVERIFY(window.m_problemsDock);

        // 1. Verify default areas matching the reference image layout:
        // Left workflow panel is full height on the left
        QCOMPARE(window.dockWidgetArea(window.m_projectDock), Qt::LeftDockWidgetArea);
        // Inspector is on the right
        QCOMPARE(window.dockWidgetArea(window.m_inspectorDock), Qt::RightDockWidgetArea);
        // Problems and logs is under center and right
        QCOMPARE(window.dockWidgetArea(window.m_problemsDock), Qt::BottomDockWidgetArea);

        // 2. Verify advanced dock nesting & islands
        QVERIFY(window.isDockNestingEnabled());

        // Test tabbed island creation (e.g. dropping problems onto inspector)
        window.tabifyDockWidget(window.m_inspectorDock, window.m_problemsDock);
        const auto tabbed = window.tabifiedDockWidgets(window.m_inspectorDock);
        QVERIFY(tabbed.contains(window.m_problemsDock));

        // Test layout reset restoring canonical layout
        window.resetLayout();
        QCOMPARE(window.dockWidgetArea(window.m_projectDock), Qt::LeftDockWidgetArea);
        QCOMPARE(window.dockWidgetArea(window.m_inspectorDock), Qt::RightDockWidgetArea);
        QCOMPARE(window.dockWidgetArea(window.m_problemsDock), Qt::BottomDockWidgetArea);

        // 3. Verify prominent bottom Run Simulation button
        QVERIFY(window.m_projectPanel->m_bottomRunButton);
        QCOMPARE(window.m_projectPanel->m_bottomRunButton->text(), QStringLiteral("Run Simulation"));

        // Verify action state synchronization
        window.m_projectPanel->setAnalysisActionState(true, true);
        QCOMPARE(window.m_projectPanel->m_bottomRunButton->text(), QStringLiteral("Running Analysis..."));
        window.m_projectPanel->setAnalysisActionState(true, false);
        QCOMPARE(window.m_projectPanel->m_bottomRunButton->text(), QStringLiteral("Run Simulation"));

        window.m_controller->document()->markClean();
        window.close();
    }
};
}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    QApplication app(argc, argv);
    femgui::UiStyle::apply(app);
    femgui::IntegrationTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "IntegrationTests.moc"
