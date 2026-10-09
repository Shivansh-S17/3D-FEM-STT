#include <QtTest>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QPushButton>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QTabWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStatusBar>
#include <QScopeGuard>
#include <QSlider>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QVTKOpenGLNativeWidget.h>
#include <vtkActor.h>
#include <vtkCell.h>
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
#include "ui/InspectorPanel.h"
#include "ui/ProjectPanel.h"
#include "ui/ScientificNumberEdit.h"
#include "ui/PressurePatchDialog.h"
#include "io/ResultExporter.h"
#include "ui/ViewportWidget.h"
#include "visualization/PlateMeshBuilder.h"
#include "visualization/C3D8MeshBuilder.h"

namespace femgui {
namespace {
QString fixturePath() { return QString(FEM_SOURCE_DIR) + "/tests/fixtures/tiny_3x3/expected_output/results.vtk"; }
QString driverPath() { return QString(FEM_SOURCE_DIR) + "/solver/reference/expected_solver_implementation.py"; }
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

    void captureNativeWeek2Visuals()
    {
        const QString projectPath = qEnvironmentVariable("WEEK2_PROJECT_PATH");
        const QString visualDir = qEnvironmentVariable("WEEK2_VISUAL_DIR");
        if (projectPath.isEmpty() || visualDir.isEmpty()) QSKIP("Evidence capture only");
        QVERIFY(QDir().mkpath(visualDir));
        MainWindow window;
        const auto opened = window.m_controller->open(projectPath);
        QVERIFY2(opened.success, qPrintable(opened.error));
        QVERIFY(window.m_controller->document()->isSolid());
        QCOMPARE(window.m_meshViewport->m_plateMesh->GetNumberOfCells(), vtkIdType(8));
        window.resize(1600, 900);
        window.show();
        window.statusBar()->clearMessage();
        QTest::qWait(700);
        window.m_projectPanel->navigateToField("coordinates");
        QTest::qWait(300);
        QVERIFY(window.grab().save(visualDir + "/01-coordinate-editor.png"));
        window.m_projectPanel->navigateToField("material");
        QTest::qWait(300);
        QVERIFY(window.grab().save(visualDir + "/02-material-editor.png"));
        window.m_projectPanel->navigateToField("foundation");
        QTest::qWait(300);
        QVERIFY(window.grab().save(visualDir + "/04-foundation-editor.png"));
        window.m_projectPanel->navigateToField("loads");
        QTest::qWait(300);
        QVERIFY(window.grab().save(visualDir + "/05-pressure-patches.png"));
        window.m_centerTabs->setCurrentIndex(1);
        QTest::qWait(400);
        QVERIFY(window.grab().save(visualDir + "/03-brick-mesh.png"));
        window.close();
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

    void oversizedSolidReportsPreviewLimit()
    {
        auto solid = fixtureSolid();
        QVector<double> axis;
        for (int i = 0; i <= 100; ++i) axis.append(i * 10.0);
        solid.xNodes = axis; solid.yNodes = axis; solid.zNodes = axis;
        const auto issues = C3D8SolverAdapter().validate(solid);
        bool sizeError = false;
        for (const auto &issue : issues)
            if (issue.fieldKey == "mesh.size" && issue.severity == Severity::Error) sizeError = true;
        QVERIFY(sizeError);
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
        QCOMPARE(doc.modelRevision(), 4);
        QVERIFY(!doc.solid().patches[0].visible);
        doc.undoStack()->undo();
        QVERIFY(doc.solid().patches[0].visible);
        QCOMPARE(doc.modelRevision(), 4);

        doc.editSolid([](auto &s) { s.material.emod = 30000; }, "Edit solid material");
        QCOMPARE(doc.modelRevision(), 5);
        QCOMPARE(doc.solid().material.emod, 30000.0);
        doc.undoStack()->undo();
        QCOMPARE(doc.modelRevision(), 4);
        QCOMPARE(doc.solid().material.emod, 24000.0);
        doc.undoStack()->redo();
        QCOMPARE(doc.modelRevision(), 5);
        QCOMPARE(doc.solid().material.emod, 30000.0);
        const int revision = doc.modelRevision();
        doc.editPlate([](auto &p) { p.slab.emod = 1; });
        QCOMPARE(doc.modelRevision(), revision);
        QVERIFY(doc.isSolid());
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
        QCOMPARE(window.m_projectPanel->m_zTable->rowCount(), 3);
        QVERIFY(!window.m_projectPanel->m_zTable->parentWidget()->isHidden());
        QVERIFY(window.m_projectPanel->m_thicknessSpin->isReadOnly());
        QCOMPARE(window.m_projectPanel->m_thicknessSpin->value(), 300.0);
        QCOMPARE(window.m_modelViewport->m_plateMesh->GetNumberOfPoints(), vtkIdType(27));
        QCOMPARE(window.m_meshViewport->m_plateMesh->GetNumberOfCells(), vtkIdType(8));
        QCOMPARE(window.m_modelViewport->m_loadPlaneZ, 0.0);
        QVERIFY(window.m_modelViewport->m_loadFromNegativeZ);

        const int revision = doc->modelRevision();
        window.m_projectPanel->m_zTable->item(1, 0)->setText("125");
        window.m_projectPanel->applyCoordinates();
        QCOMPARE(doc->solid().zNodes[1], 125.0);
        QCOMPARE(doc->modelRevision(), revision + 1);
        QCOMPARE(window.m_meshViewport->m_plateMesh->GetNumberOfCells(), vtkIdType(8));
        window.m_projectPanel->m_zTable->item(1, 0)->setText("0");
        window.m_projectPanel->applyCoordinates();
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

    void realRunMatchesReferenceAndReopens()
    {
        QTemporaryDir dir;
        ProjectController controller;
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
        QFile resultFile(record.resultVtkPath);
        QVERIFY(resultFile.open(QIODevice::ReadOnly));
        const auto originalBytes = resultFile.readAll();
        resultFile.close();
        QVERIFY(writeBytes(record.resultVtkPath, originalBytes + "\n"));
        controller.document()->clearResultSet();
        const auto tampered = controller.loadRunResults(record.runId);
        QVERIFY(!tampered.success);
        QVERIFY(tampered.error.contains("provenance"));
        QVERIFY(!controller.document()->hasResultSet());
        QVERIFY(writeBytes(record.resultVtkPath, originalBytes));
        QVERIFY(controller.loadRunResults(record.runId).success);
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
        auto *doc = window.m_controller->document();
        doc->editPlate([](auto &p) { p = fixturePlate(); p.foundation.kx = 1.234567890123456e-10; p.slab.emod = 30000.123456789; });
        window.m_projectPanel->refreshAll();
        const int revision = doc->modelRevision();
        window.m_projectPanel->applyFoundation();
        window.m_projectPanel->applyMaterial();
        QCOMPARE(doc->modelRevision(), revision);
        QCOMPARE(doc->plate().foundation.kx, 1.234567890123456e-10);
        QCOMPARE(doc->plate().slab.emod, 30000.123456789);
        window.m_projectPanel->m_nuSpin->setValue(0.3);
        QCOMPARE(window.m_projectPanel->m_nuSpin->text(), QString("0.3"));
        QCOMPARE(window.m_projectPanel->m_nuSpin->value(), 0.3);
        window.m_projectPanel->refreshAll();
        window.m_projectPanel->m_kxSpin->setText("invalid");
        window.m_projectPanel->applyFoundation();
        QCOMPARE(doc->modelRevision(), revision);
        window.m_projectPanel->m_xTable->item(1, 0)->setText("0");
        window.m_projectPanel->applyCoordinates();
        QCOMPARE(doc->plate().xNodes[1], 500.0);
        QCOMPARE(doc->modelRevision(), revision);
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
};
}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    QApplication app(argc, argv);
    femgui::IntegrationTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "IntegrationTests.moc"
