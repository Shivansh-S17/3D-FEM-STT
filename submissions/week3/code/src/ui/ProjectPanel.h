#pragma once

#include <QWidget>

#include "core/ValidationIssue.h"

class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;
class QLabel;
class QLineEdit;
class QDoubleSpinBox;
class QTableWidget;
class QPushButton;
class QGroupBox;
class QComboBox;

namespace femgui {

class ProjectController;
class ScientificNumberEdit;

// Left panel: workflow tree plus bound editors for plate and C3D8 models
// (section 5). Pages are built once and kept (never recreated per click),
// fixing the prior placeholder implementation's page-leak on selection.
class ProjectPanel : public QWidget
{
    Q_OBJECT

public:
    enum class WorkflowStep { Overview, Model, MaterialFoundation, Loads, ValidateRun, Results, Advanced };

    explicit ProjectPanel(QWidget *parent = nullptr);

    // Rebinds every page to the controller's current document. Call once
    // after construction and again whenever ProjectController::projectReplaced()
    // fires (New/Open).
    void bindToController(ProjectController *controller);

public slots:
    void setValidationIssues(const ValidationIssues &issues);
    void refreshAll();
    void refreshSummaries();
    void navigateToField(const QString &fieldKey);
    void selectPatch(const QUuid &id);
    void setAnalysisActionState(bool canRun, bool running);

signals:
    void statusMessage(const QString &message);
    // MainWindow owns the viewports; the panel asks for a rebuild rather
    // than touching VTK actors itself.
    void modelEdited();
    void workflowStepChanged(int pageIndex);
    void openOutputFolderRequested(const QString &path);
    void loadRunRequested(const QString &runId);
    void drawPatchRequested();
    void patchSelected(QUuid id);
    void runRequested();
    void stopRequested();
    void showResultsRequested();
    void exportResultsCsvRequested();
    void exportResultsImageRequested();

private:
    friend class IntegrationTests;
    void populateTree();
    QWidget *buildOverviewPage();
    QWidget *buildMaterialPage();
    QWidget *buildFoundationPage();
    QWidget *buildCoordinatesPage();
    QWidget *buildMeshPage();
    QWidget *buildLoadsPage();
    QWidget *buildAnalysisPage();
    QWidget *buildResultsPage();
    QWidget *buildAdvancedPage();

    void refreshOverviewPage();
    void refreshMaterialPage();
    void refreshFoundationPage();
    void refreshCoordinatesPage();
    void refreshMeshPage();
    void refreshLoadsPage();
    void refreshAnalysisPage();
    void refreshValidationMessages();
    void updateWorkflowStates();
    void updatePatchActionStates();
    void updateRunHistoryActionStates();

    void onCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void applyMaterial();
    void applyFoundation();
    void applyMaterialPreset(int index);
    void applyFoundationPreset(int index);
    void editGeometry();
    void onLoadsTableCellChanged(int row, int column);
    void addPatch();
    void editSelectedPatch();
    void duplicateSelectedPatch();
    void deleteSelectedPatch();
    void checkBackend();

    ProjectController *m_controller = nullptr;
    QTreeWidget *m_tree = nullptr;
    QStackedWidget *m_details = nullptr;
    QTreeWidgetItem *m_overviewStep = nullptr;
    QTreeWidgetItem *m_modelStep = nullptr;
    QTreeWidgetItem *m_materialStep = nullptr;
    QTreeWidgetItem *m_loadsStep = nullptr;
    QTreeWidgetItem *m_analysisStep = nullptr;
    QTreeWidgetItem *m_resultsStep = nullptr;
    QTreeWidgetItem *m_advancedStep = nullptr;

    // Overview
    QLineEdit *m_nameEdit = nullptr;
    QLabel *m_revisionLabel = nullptr;
    QLabel *m_countsLabel = nullptr;
    QLabel *m_readinessLabel = nullptr;
    QLabel *m_backendLabel = nullptr;
    QLabel *m_overviewNextLabel = nullptr;

    // Material
    QComboBox *m_materialPresetCombo = nullptr;
    ScientificNumberEdit *m_emodSpin = nullptr;
    ScientificNumberEdit *m_nuSpin = nullptr;
    ScientificNumberEdit *m_thicknessSpin = nullptr;
    QLabel *m_materialValidationLabel = nullptr;

    // Foundation
    QComboBox *m_foundationPresetCombo = nullptr;
    ScientificNumberEdit *m_kxSpin = nullptr;
    ScientificNumberEdit *m_kySpin = nullptr;
    ScientificNumberEdit *m_kzSpin = nullptr;
    QLabel *m_foundationValidationLabel = nullptr;

    // Coordinates
    QLabel *m_geometrySummaryLabel = nullptr;
    QPushButton *m_editGeometryButton = nullptr;
    QLabel *m_coordinatesValidationLabel = nullptr;

    // Mesh
    QLabel *m_meshSummaryLabel = nullptr;

    // Loads
    QTableWidget *m_loadsTable = nullptr;
    QLabel *m_totalLoadSummaryLabel = nullptr;
    QPushButton *m_editPatchButton = nullptr;
    QPushButton *m_duplicatePatchButton = nullptr;
    QPushButton *m_deletePatchButton = nullptr;
    QLabel *m_loadsValidationLabel = nullptr;
    bool m_updatingLoadsTable = false;

    // Analysis
    QLineEdit *m_pythonPathEdit = nullptr;
    QLineEdit *m_driverPathEdit = nullptr;
    QLabel *m_backendCheckLabel = nullptr;
    QLabel *m_validationChecklistLabel = nullptr;
    QTableWidget *m_runHistoryTable = nullptr;
    QLabel *m_analysisReadinessLabel = nullptr;
    QPushButton *m_runButton = nullptr;
    QPushButton *m_bottomRunButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_openRunFolderButton = nullptr;
    QPushButton *m_loadRunResultsButton = nullptr;
    QPushButton *m_showResultsButton = nullptr;
    QPushButton *m_runAgainButton = nullptr;
    QPushButton *m_exportCsvButton = nullptr;
    QPushButton *m_exportImageButton = nullptr;
    QLabel *m_resultsStateLabel = nullptr;
    QLabel *m_resultsExtremaLabel = nullptr;
    QLabel *m_resultsEquilibriumLabel = nullptr;
    QPushButton *m_openRunFolderButtonResults = nullptr;

    ValidationIssues m_lastIssues;
    bool m_hasValidated = false;
};

} // namespace femgui
