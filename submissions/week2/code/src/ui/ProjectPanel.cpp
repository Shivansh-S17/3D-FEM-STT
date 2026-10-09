#include "ui/ProjectPanel.h"
#include "ui/ScientificNumberEdit.h"
#include "ui/PressurePatchDialog.h"

#include "controllers/AnalysisController.h"
#include "controllers/ProjectController.h"
#include "core/PlateDefinition.h"
#include "core/SolidDefinition.h"
#include "core/ProjectDocument.h"
#include "core/RunRecord.h"

#include <algorithm>
#include <cmath>
#include <QSignalBlocker>

#include <QCheckBox>
#include <QUuid>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace femgui {

namespace {
constexpr int kPageOverview = 0;
constexpr int kPageMaterial = 1;
constexpr int kPageFoundation = 2;
constexpr int kPageCoordinates = 3;
constexpr int kPageMesh = 4;
constexpr int kPageLoads = 5;
constexpr int kPageAnalysis = 6;
constexpr int kPageResults = 7;

const QVector<PressurePatch> &activePatches(const ProjectDocument *doc)
{
    return doc->isPlate() ? doc->plate().patches : doc->solid().patches;
}

PlateDefinition patchDomain(const ProjectDocument *doc)
{
    if (doc->isPlate()) return doc->plate();
    PlateDefinition domain;
    domain.xNodes = doc->solid().xNodes;
    domain.yNodes = doc->solid().yNodes;
    domain.patches = doc->solid().patches;
    return domain;
}

void editActivePatches(ProjectDocument *doc, const std::function<void(QVector<PressurePatch> &)> &edit,
                       const QString &description = "Edit pressure patch")
{
    if (doc->isPlate()) doc->editPlate([&](auto &model) { edit(model.patches); }, description);
    else doc->editSolid([&](auto &model) { edit(model.patches); }, description);
}
}

ProjectPanel::ProjectPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Vertical, this);

    m_tree = new QTreeWidget(splitter);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    populateTree();

    m_details = new QStackedWidget(splitter);
    for (auto *page : {buildOverviewPage(), buildMaterialPage(), buildFoundationPage(), buildCoordinatesPage(),
                       buildMeshPage(), buildLoadsPage(), buildAnalysisPage(), buildResultsPage()}) {
        for (auto *form : page->findChildren<QFormLayout *>()) form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        for (auto *label : page->findChildren<QLabel *>()) label->setWordWrap(true);
        auto *scroll = new QScrollArea(m_details);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(page);
        m_details->addWidget(scroll);
    }

    splitter->addWidget(m_tree);
    splitter->addWidget(m_details);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);

    layout->addWidget(splitter);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, &ProjectPanel::onCurrentItemChanged);
}

void ProjectPanel::populateTree()
{
    auto addLeaf = [this](QTreeWidgetItem *parentItem, const QString &text, int pageIndex) {
        auto *item = new QTreeWidgetItem(parentItem, QStringList{text});
        item->setData(0, Qt::UserRole, pageIndex);
        return item;
    };

    auto *root = new QTreeWidgetItem(m_tree, QStringList{tr("Project")});
    root->setData(0, Qt::UserRole, kPageOverview);
    root->setExpanded(true);

    auto *model = new QTreeWidgetItem(root, QStringList{tr("Model")});
    model->setExpanded(true);
    addLeaf(model, tr("Material and thickness"), kPageMaterial);
    addLeaf(model, tr("Foundation"), kPageFoundation);

    auto *mesh = new QTreeWidgetItem(root, QStringList{tr("Mesh")});
    mesh->setExpanded(true);
    addLeaf(mesh, tr("X / Y / Z coordinates"), kPageCoordinates);
    addLeaf(mesh, tr("Mesh summary"), kPageMesh);

    auto *conditions = new QTreeWidgetItem(root, QStringList{tr("Boundary Conditions")});
    conditions->setExpanded(true);
    addLeaf(conditions, tr("Pressure patches"), kPageLoads);

    auto *analysis = new QTreeWidgetItem(root, QStringList{tr("Analysis")});
    analysis->setExpanded(true);
    addLeaf(analysis, tr("Backend and run history"), kPageAnalysis);

    addLeaf(root, tr("Results"), kPageResults);

    m_tree->expandAll();
}

void ProjectPanel::onCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous)
{
    Q_UNUSED(previous);
    if (!current)
        return;
    m_details->setCurrentIndex(current->data(0, Qt::UserRole).toInt());
    emit statusMessage(tr("Editing: %1").arg(current->text(0)));
}

void ProjectPanel::bindToController(ProjectController *controller)
{
    m_controller = controller;
    m_hasValidated = false;
    m_backendCheckLabel->clear();
    connect(controller->analysis(), &AnalysisController::backendChecked, this, [this](bool ok, const QString &message) {
        m_backendCheckLabel->setText(message);
        m_backendCheckLabel->setStyleSheet(ok ? "color: green;" : "color: firebrick;");
    });
    refreshAll();
}

void ProjectPanel::refreshAll()
{
    refreshOverviewPage();
    refreshMaterialPage();
    refreshFoundationPage();
    refreshCoordinatesPage();
    refreshMeshPage();
    refreshLoadsPage();
    refreshAnalysisPage();
}

void ProjectPanel::setValidationIssues(const ValidationIssues &issues)
{
    m_hasValidated = true;
    m_lastIssues = issues;
    refreshOverviewPage();
}

void ProjectPanel::refreshSummaries()
{
    refreshOverviewPage();
    refreshMeshPage();
}

void ProjectPanel::navigateToField(const QString &key)
{
    int page = kPageOverview;
    if (key.startsWith("slab") || key.startsWith("material")) page = kPageMaterial;
    else if (key.startsWith("foundation")) page = kPageFoundation;
    else if (key.startsWith("coordinates") || key.startsWith("mesh")) page = kPageCoordinates;
    else if (key.startsWith("loads")) page = kPageLoads;
    else if (key.startsWith("backend") || key.startsWith("run")) page = kPageAnalysis;
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).isValid() && (*it)->data(0, Qt::UserRole).toInt() == page) {
            m_tree->setCurrentItem(*it);
            break;
        }
        ++it;
    }
    m_details->setCurrentIndex(page);
    if (page == kPageLoads && m_controller) {
        const auto &patches = activePatches(m_controller->document());
        for (int row = 0; row < patches.size(); ++row)
            if (key.contains(patches[row].id.toString())) m_loadsTable->selectRow(row);
    }
}

// ---------------------------------------------------------------- Overview

QWidget *ProjectPanel::buildOverviewPage()
{
    auto *page = new QWidget(m_details);
    auto *form = new QFormLayout(page);

    m_nameEdit = new QLineEdit(page);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_controller)
            m_controller->document()->setName(m_nameEdit->text());
    });
    form->addRow(tr("Project name:"), m_nameEdit);
    form->addRow(tr("Units:"), new QLabel(tr("mm - N - MPa (fixed for this backend)"), page));
    m_backendLabel = new QLabel(page);
    form->addRow(tr("Backend:"), m_backendLabel);
    m_revisionLabel = new QLabel(page);
    form->addRow(tr("Model revision:"), m_revisionLabel);
    m_countsLabel = new QLabel(page);
    form->addRow(tr("Mesh:"), m_countsLabel);
    m_readinessLabel = new QLabel(page);
    m_readinessLabel->setWordWrap(true);
    form->addRow(tr("Readiness:"), m_readinessLabel);

    return page;
}

void ProjectPanel::refreshOverviewPage()
{
    if (!m_controller)
        return;
    ProjectDocument *doc = m_controller->document();
    m_nameEdit->setText(doc->name());
    m_revisionLabel->setText(QString::number(doc->modelRevision()));
    m_backendLabel->setText(doc->isPlate() ? tr("plate-python-v1 (plate on elastic foundation)")
                                           : tr("c3d8-python-v1 (structured C3D8 solid)"));
    m_countsLabel->setText(QString(tr("%1 nodes, %2 elements"))
                                .arg(modelNodeCount(doc->model())).arg(modelElementCount(doc->model())));

    int errors = 0, warnings = 0;
    for (const ValidationIssue &issue : m_lastIssues) {
        if (issue.severity == Severity::Error) ++errors;
        else if (issue.severity == Severity::Warning) ++warnings;
    }
    if (!m_hasValidated)
        m_readinessLabel->setText(tr("Not yet validated. Use Validate on the toolbar."));
    else
        m_readinessLabel->setText(tr("%1 error(s), %2 warning(s).").arg(errors).arg(warnings));
}

// ---------------------------------------------------------------- Material

QWidget *ProjectPanel::buildMaterialPage()
{
    auto *page = new QWidget(m_details);
    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout;

    m_emodSpin = new ScientificNumberEdit(page);
    form->addRow(tr("Young's modulus (Emod) [MPa]:"), m_emodSpin);

    m_nuSpin = new ScientificNumberEdit(page);
    form->addRow(tr("Poisson's ratio (nu):"), m_nuSpin);

    m_thicknessSpin = new ScientificNumberEdit(page);
    form->addRow(tr("Thickness (t) [mm]:"), m_thicknessSpin);

    layout->addLayout(form);
    auto *applyButton = new QPushButton(tr("Apply"), page);
    connect(applyButton, &QPushButton::clicked, this, &ProjectPanel::applyMaterial);
    layout->addWidget(applyButton);
    layout->addStretch();
    return page;
}

void ProjectPanel::refreshMaterialPage()
{
    if (!m_controller)
        return;
    const auto *doc = m_controller->document();
    if (doc->isPlate()) {
        const auto &slab = doc->plate().slab;
        m_emodSpin->setValue(slab.emod); m_nuSpin->setValue(slab.nu); m_thicknessSpin->setValue(slab.thickness);
        m_thicknessSpin->setReadOnly(false);
    } else {
        const auto &solid = doc->solid();
        m_emodSpin->setValue(solid.material.emod); m_nuSpin->setValue(solid.material.nu);
        m_thicknessSpin->setValue(solid.zNodes.size() >= 2 ? solid.zNodes.last() - solid.zNodes.first() : 0.0);
        m_thicknessSpin->setReadOnly(true);
    }
}

void ProjectPanel::applyMaterial()
{
    if (!m_controller)
        return;
    double emod = m_emodSpin->value(), nu = m_nuSpin->value(), t = m_thicknessSpin->value();
    if (!std::isfinite(emod) || !std::isfinite(nu) || emod <= 0 || nu <= -1 || nu >= 0.5 ||
        (m_controller->document()->isPlate() && (!std::isfinite(t) || t <= 0))) {
        emit statusMessage(tr("Material not applied: require finite Emod > 0, positive plate thickness and -1 < nu < 0.5."));
        return;
    }
    if (m_controller->document()->isPlate())
        m_controller->document()->editPlate([=](auto &plate) { plate.slab = {emod, nu, t}; });
    else
        m_controller->document()->editSolid([=](auto &solid) { solid.material = {emod, nu}; });
    emit modelEdited();
    emit statusMessage(tr("Material properties updated."));
}

// -------------------------------------------------------------- Foundation

QWidget *ProjectPanel::buildFoundationPage()
{
    auto *page = new QWidget(m_details);
    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout;

    m_kxSpin = new ScientificNumberEdit(page);
    form->addRow(tr("Kx:"), m_kxSpin);

    m_kySpin = new ScientificNumberEdit(page);
    form->addRow(tr("Ky:"), m_kySpin);

    m_kzSpin = new ScientificNumberEdit(page);
    form->addRow(tr("Kz:"), m_kzSpin);

    layout->addLayout(form);
    auto *note = new QLabel(tr("Units and physical model for Kx/Ky/Kz are not yet confirmed against the "
                                "element author's documentation."), page);
    note->setWordWrap(true);
    note->setStyleSheet("color: palette(mid);");
    layout->addWidget(note);
    auto *applyButton = new QPushButton(tr("Apply"), page);
    connect(applyButton, &QPushButton::clicked, this, &ProjectPanel::applyFoundation);
    layout->addWidget(applyButton);
    layout->addStretch();
    return page;
}

void ProjectPanel::refreshFoundationPage()
{
    if (!m_controller)
        return;
    const auto *doc = m_controller->document();
    const auto &foundation = doc->isPlate() ? doc->plate().foundation : doc->solid().foundation;
    m_kxSpin->setValue(foundation.kx);
    m_kySpin->setValue(foundation.ky);
    m_kzSpin->setValue(foundation.kz);
}

void ProjectPanel::applyFoundation()
{
    if (!m_controller)
        return;
    double kx = m_kxSpin->value(), ky = m_kySpin->value(), kz = m_kzSpin->value();
    if (!std::isfinite(kx) || !std::isfinite(ky) || !std::isfinite(kz) ||
        (m_controller->document()->isSolid() && (kx <= 0 || ky <= 0 || kz <= 0))) {
        emit statusMessage(tr("Foundation not applied: enter finite values; C3D8 requires Kx, Ky and Kz greater than zero."));
        return;
    }
    if (m_controller->document()->isPlate())
        m_controller->document()->editPlate([=](auto &model) { model.foundation = {kx, ky, kz}; });
    else
        m_controller->document()->editSolid([=](auto &model) { model.foundation = {kx, ky, kz}; });
    emit modelEdited();
    emit statusMessage(tr("Foundation properties updated."));
}

// ------------------------------------------------------------- Coordinates

QWidget *ProjectPanel::buildCoordinatesPage()
{
    auto *page = new QWidget(m_details);
    auto *layout = new QVBoxLayout(page);

    auto *tables = new QVBoxLayout;
    auto *xGroup = new QGroupBox(tr("X coordinates (mm)"), page);
    auto *xLayout = new QVBoxLayout(xGroup);
    m_xTable = new QTableWidget(0, 1, xGroup);
    m_xTable->setHorizontalHeaderLabels({tr("X")});
    m_xTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    xLayout->addWidget(m_xTable);
    auto *xButtons = new QHBoxLayout;
    auto *xAdd = new QPushButton(tr("Add Row"), xGroup);
    auto *xRemove = new QPushButton(tr("Remove Row"), xGroup);
    connect(xAdd, &QPushButton::clicked, this, [this] {
        int row = m_xTable->rowCount();
        m_xTable->insertRow(row);
        m_xTable->setItem(row, 0, new QTableWidgetItem("0"));
    });
    connect(xRemove, &QPushButton::clicked, this, [this] {
        if (m_xTable->currentRow() >= 0)
            m_xTable->removeRow(m_xTable->currentRow());
    });
    xButtons->addWidget(xAdd);
    xButtons->addWidget(xRemove);
    xLayout->addLayout(xButtons);
    tables->addWidget(xGroup);

    auto *yGroup = new QGroupBox(tr("Y coordinates (mm)"), page);
    auto *yLayout = new QVBoxLayout(yGroup);
    m_yTable = new QTableWidget(0, 1, yGroup);
    m_yTable->setHorizontalHeaderLabels({tr("Y")});
    m_yTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    yLayout->addWidget(m_yTable);
    auto *yButtons = new QHBoxLayout;
    auto *yAdd = new QPushButton(tr("Add Row"), yGroup);
    auto *yRemove = new QPushButton(tr("Remove Row"), yGroup);
    connect(yAdd, &QPushButton::clicked, this, [this] {
        int row = m_yTable->rowCount();
        m_yTable->insertRow(row);
        m_yTable->setItem(row, 0, new QTableWidgetItem("0"));
    });
    connect(yRemove, &QPushButton::clicked, this, [this] {
        if (m_yTable->currentRow() >= 0)
            m_yTable->removeRow(m_yTable->currentRow());
    });
    yButtons->addWidget(yAdd);
    yButtons->addWidget(yRemove);
    yLayout->addLayout(yButtons);
    tables->addWidget(yGroup);

    auto *zGroup = new QGroupBox(tr("Z coordinates (mm) — C3D8 only"), page);
    auto *zLayout = new QVBoxLayout(zGroup);
    m_zTable = new QTableWidget(0, 1, zGroup);
    m_zTable->setHorizontalHeaderLabels({tr("Z")});
    m_zTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    zLayout->addWidget(m_zTable);
    auto *zButtons = new QHBoxLayout;
    auto *zAdd = new QPushButton(tr("Add Row"), zGroup);
    auto *zRemove = new QPushButton(tr("Remove Row"), zGroup);
    connect(zAdd, &QPushButton::clicked, this, [this] {
        const int row = m_zTable->rowCount(); m_zTable->insertRow(row);
        m_zTable->setItem(row, 0, new QTableWidgetItem("0"));
    });
    connect(zRemove, &QPushButton::clicked, this, [this] {
        if (m_zTable->currentRow() >= 0) m_zTable->removeRow(m_zTable->currentRow());
    });
    zButtons->addWidget(zAdd); zButtons->addWidget(zRemove); zLayout->addLayout(zButtons);
    tables->addWidget(zGroup);

    layout->addLayout(tables);

    auto *note = new QLabel(tr("Coordinates are the authoritative geometry: enter them directly, sorted and "
                                "increasing, with no duplicates. Adding a row here refines the whole grid row/column."), page);
    note->setWordWrap(true);
    note->setStyleSheet("color: palette(mid);");
    layout->addWidget(note);

    auto *applyButton = new QPushButton(tr("Apply to Model"), page);
    connect(applyButton, &QPushButton::clicked, this, &ProjectPanel::applyCoordinates);
    layout->addWidget(applyButton);

    return page;
}

void ProjectPanel::refreshCoordinatesPage()
{
    if (!m_controller)
        return;
    const auto *doc = m_controller->document();
    const auto &xNodes = doc->isPlate() ? doc->plate().xNodes : doc->solid().xNodes;
    const auto &yNodes = doc->isPlate() ? doc->plate().yNodes : doc->solid().yNodes;

    m_xTable->setRowCount(xNodes.size());
    for (int i = 0; i < xNodes.size(); ++i)
        m_xTable->setItem(i, 0, new QTableWidgetItem(QString::number(xNodes[i], 'g', 17)));

    m_yTable->setRowCount(yNodes.size());
    for (int i = 0; i < yNodes.size(); ++i)
        m_yTable->setItem(i, 0, new QTableWidgetItem(QString::number(yNodes[i], 'g', 17)));
    m_zTable->parentWidget()->setVisible(doc->isSolid());
    const auto zNodes = doc->isSolid() ? doc->solid().zNodes : QVector<double>{};
    m_zTable->setRowCount(zNodes.size());
    for (int i = 0; i < zNodes.size(); ++i)
        m_zTable->setItem(i, 0, new QTableWidgetItem(QString::number(zNodes[i], 'g', 17)));
}

void ProjectPanel::applyCoordinates()
{
    if (!m_controller)
        return;

    QVector<double> xNodes, yNodes, zNodes;
    bool ok = true;
    for (int i = 0; i < m_xTable->rowCount() && ok; ++i) {
        QTableWidgetItem *item = m_xTable->item(i, 0);
        double value = item ? item->text().toDouble(&ok) : (ok = false, 0.0);
        if (ok)
            xNodes.append(value);
    }
    for (int i = 0; i < m_yTable->rowCount() && ok; ++i) {
        QTableWidgetItem *item = m_yTable->item(i, 0);
        double value = item ? item->text().toDouble(&ok) : (ok = false, 0.0);
        if (ok)
            yNodes.append(value);
    }
    if (m_controller->document()->isSolid()) for (int i = 0; i < m_zTable->rowCount() && ok; ++i) {
        QTableWidgetItem *item = m_zTable->item(i, 0);
        double value = item ? item->text().toDouble(&ok) : (ok = false, 0.0);
        if (ok) zNodes.append(value);
    }
    if (!ok) {
        emit statusMessage(tr("Coordinate table contains a non-numeric value; not applied."));
        return;
    }
    QVector<QVector<double>> axes{xNodes, yNodes};
    if (m_controller->document()->isSolid()) axes.append(zNodes);
    for (const auto &axis : axes) {
        if (axis.size() < 2) { emit statusMessage(tr("Coordinates not applied: each axis needs at least two values.")); return; }
        for (int i = 0; i < axis.size(); ++i)
            if (!std::isfinite(axis[i]) || (i > 0 && axis[i] <= axis[i - 1])) {
                emit statusMessage(tr("Coordinates not applied: values must be finite, strictly increasing and unique."));
                return;
            }
    }

    if (m_controller->document()->isPlate())
        m_controller->document()->editPlate([&](auto &model) { model.xNodes = xNodes; model.yNodes = yNodes; });
    else
        m_controller->document()->editSolid([&](auto &model) { model.xNodes = xNodes; model.yNodes = yNodes; model.zNodes = zNodes; });
    emit modelEdited();
    emit statusMessage(tr("Mesh coordinates updated."));
}

// ------------------------------------------------------------------- Mesh

QWidget *ProjectPanel::buildMeshPage()
{
    auto *page = new QWidget(m_details);
    auto *layout = new QVBoxLayout(page);
    m_meshSummaryLabel = new QLabel(page);
    m_meshSummaryLabel->setWordWrap(true);
    layout->addWidget(m_meshSummaryLabel);
    layout->addStretch();
    return page;
}

void ProjectPanel::refreshMeshPage()
{
    if (!m_controller)
        return;
    const auto *doc = m_controller->document();
    if (doc->isSolid()) {
        const auto &solid = doc->solid();
        auto minSpacing = [](const QVector<double> &axis) {
            double value = 0.0;
            for (int i = 1; i < axis.size(); ++i) {
                const double d = axis[i] - axis[i - 1];
                if (i == 1 || d < value) value = d;
            }
            return value;
        };
        const double thickness = solid.zNodes.size() >= 2 ? solid.zNodes.last() - solid.zNodes.first() : 0.0;
        m_meshSummaryLabel->setText(
            tr("Nodes: %1 (%2 x %3 x %4)\nElements: %5\nDOFs: %6\nLayers: %7\n"
               "Derived thickness: %8 mm\nMinimum element size: %9 x %10 x %11 mm\n\n"
               "Eight-node C3D8 bricks, 3 translational DOFs per node. Solver order is X fastest, then Z, then Y. "
               "The z-min face is loaded and the z-max face carries foundation.")
                .arg(solid.nodeCount()).arg(solid.nodeCountX()).arg(solid.nodeCountY()).arg(solid.nodeCountZ())
                .arg(solid.elementCount()).arg(solid.dofCount()).arg(std::max(0, solid.nodeCountZ() - 1))
                .arg(thickness).arg(minSpacing(solid.xNodes)).arg(minSpacing(solid.yNodes)).arg(minSpacing(solid.zNodes)));
        return;
    }
    const auto &plate = doc->plate();

    double minDx = 0.0, minDy = 0.0, maxAspect = 0.0;
    for (int i = 1; i < plate.xNodes.size(); ++i) {
        double d = plate.xNodes[i] - plate.xNodes[i - 1];
        if (i == 1 || d < minDx) minDx = d;
    }
    for (int i = 1; i < plate.yNodes.size(); ++i) {
        double d = plate.yNodes[i] - plate.yNodes[i - 1];
        if (i == 1 || d < minDy) minDy = d;
    }
    for (int i = 1; i < plate.xNodes.size(); ++i) {
        for (int j = 1; j < plate.yNodes.size(); ++j) {
            double a = plate.xNodes[i] - plate.xNodes[i - 1];
            double b = plate.yNodes[j] - plate.yNodes[j - 1];
            if (a > 0 && b > 0)
                maxAspect = std::max({maxAspect, a / b, b / a});
        }
    }

    m_meshSummaryLabel->setText(
        tr("Nodes: %1 (%2 x %3)\nElements: %4\nDOFs: %5\nMinimum element size: %6 x %7 mm\n"
           "Maximum element aspect ratio: %8\n\nFour-node rectangular elements, 5 DOFs per node "
           "(fixed for this backend). Edit coordinates on the X/Y coordinates page and click "
           "\"Apply to Model\" to rebuild this mesh.")
            .arg(plate.nodeCount()).arg(plate.nodeCountX()).arg(plate.nodeCountY())
            .arg(plate.elementCount()).arg(plate.dofCount())
            .arg(minDx).arg(minDy).arg(maxAspect, 0, 'g', 3));
}

// ------------------------------------------------------------------ Loads

QWidget *ProjectPanel::buildLoadsPage()
{
    auto *page = new QWidget(m_details);
    auto *layout = new QVBoxLayout(page);

    m_loadsTable = new QTableWidget(0, 8, page);
    m_loadsTable->setHorizontalHeaderLabels({tr("Name"), tr("x1"), tr("x2"), tr("y1"), tr("y2"), tr("q (MPa)"), tr("Enabled"), tr("Visible")});
    m_loadsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    connect(m_loadsTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = m_loadsTable->currentRow();
        if (!m_updatingLoadsTable && row >= 0 && m_controller && row < activePatches(m_controller->document()).size())
            emit patchSelected(activePatches(m_controller->document())[row].id);
    });
    connect(m_loadsTable, &QTableWidget::cellChanged, this, &ProjectPanel::onLoadsTableCellChanged);
    layout->addWidget(m_loadsTable);

    auto *buttons = new QHBoxLayout;
    auto *addButton = new QPushButton(tr("Add Patch"), page);
    auto *duplicateButton = new QPushButton(tr("Duplicate"), page);
    auto *deleteButton = new QPushButton(tr("Delete"), page);
    connect(addButton, &QPushButton::clicked, this, &ProjectPanel::addPatch);
    connect(duplicateButton, &QPushButton::clicked, this, &ProjectPanel::duplicateSelectedPatch);
    connect(deleteButton, &QPushButton::clicked, this, &ProjectPanel::deleteSelectedPatch);
    buttons->addWidget(addButton);
    buttons->addWidget(duplicateButton);
    buttons->addWidget(deleteButton);
    layout->addLayout(buttons);
    auto *editButtons = new QHBoxLayout;
    auto *editButton = new QPushButton(tr("Edit Selected"), page);
    connect(editButton, &QPushButton::clicked, this, &ProjectPanel::editSelectedPatch);
    auto *drawButton = new QPushButton(tr("Draw Rectangle"), page);
    connect(drawButton, &QPushButton::clicked, this, &ProjectPanel::drawPatchRequested);
    editButtons->addWidget(editButton); editButtons->addWidget(drawButton);
    layout->addLayout(editButtons);

    auto *note = new QLabel(tr("All enabled patches enter the same force vector for this backend "
                                "(they are pressure patches, not independent load cases). Overlapping "
                                "patches add pressure in the overlap."), page);
    note->setWordWrap(true);
    note->setStyleSheet("color: palette(mid);");
    layout->addWidget(note);

    return page;
}

void ProjectPanel::refreshLoadsPage()
{
    if (!m_controller)
        return;
    m_updatingLoadsTable = true;
    const auto &patches = activePatches(m_controller->document());
    m_loadsTable->setRowCount(patches.size());
    for (int row = 0; row < patches.size(); ++row) {
        const PressurePatch &patch = patches[row];
        m_loadsTable->setItem(row, 0, new QTableWidgetItem(patch.name));
        m_loadsTable->setItem(row, 1, new QTableWidgetItem(QString::number(patch.x1, 'g', 17)));
        m_loadsTable->setItem(row, 2, new QTableWidgetItem(QString::number(patch.x2, 'g', 17)));
        m_loadsTable->setItem(row, 3, new QTableWidgetItem(QString::number(patch.y1, 'g', 17)));
        m_loadsTable->setItem(row, 4, new QTableWidgetItem(QString::number(patch.y2, 'g', 17)));
        m_loadsTable->setItem(row, 5, new QTableWidgetItem(QString::number(patch.q, 'g', 17)));
        auto *enabledItem = new QTableWidgetItem();
        enabledItem->setCheckState(patch.enabledForAnalysis ? Qt::Checked : Qt::Unchecked);
        m_loadsTable->setItem(row, 6, enabledItem);
        auto *visibleItem = new QTableWidgetItem();
        visibleItem->setCheckState(patch.visible ? Qt::Checked : Qt::Unchecked);
        m_loadsTable->setItem(row, 7, visibleItem);
    }
    m_updatingLoadsTable = false;
}

void ProjectPanel::onLoadsTableCellChanged(int row, int column)
{
    if (m_updatingLoadsTable || !m_controller)
        return;
    if (row < 0 || row >= activePatches(m_controller->document()).size())
        return;

    QTableWidgetItem *item = m_loadsTable->item(row, column);
    if (!item)
        return;

    double numericValue = 0;
    if (column >= 1 && column <= 5) {
        bool ok = false;
        numericValue = item->text().toDouble(&ok);
        if (!ok || !std::isfinite(numericValue)) {
            emit statusMessage(tr("Invalid pressure-patch number; previous value restored."));
            const auto &patch = activePatches(m_controller->document())[row];
            const double oldValues[] = {patch.x1, patch.x2, patch.y1, patch.y2, patch.q};
            QSignalBlocker blocker(m_loadsTable);
            item->setText(QString::number(oldValues[column - 1], 'g', 17));
            return;
        }
    }

    editActivePatches(m_controller->document(), [&](QVector<PressurePatch> &patches) {
        PressurePatch &patch = patches[row];
        switch (column) {
        case 0: patch.name = item->text(); break;
        case 1: patch.x1 = numericValue; break;
        case 2: patch.x2 = numericValue; break;
        case 3: patch.y1 = numericValue; break;
        case 4: patch.y2 = numericValue; break;
        case 5: patch.q = numericValue; break;
        case 6: patch.enabledForAnalysis = item->checkState() == Qt::Checked; break;
        case 7: patch.visible = item->checkState() == Qt::Checked; break;
        default: break;
        }
    });
    emit modelEdited();
}

void ProjectPanel::addPatch()
{
    if (!m_controller)
        return;
    const auto domain = patchDomain(m_controller->document());
    PressurePatch draft;
    draft.name = QString("Patch %1").arg(domain.patches.size() + 1);
    if (!domain.xNodes.isEmpty() && !domain.yNodes.isEmpty()) {
        draft.x1 = domain.xNodes.first(); draft.x2 = domain.xNodes.last();
        draft.y1 = domain.yNodes.first(); draft.y2 = domain.yNodes.last();
    }
    PressurePatchDialog dialog(domain, draft, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto patch = dialog.patch();
    editActivePatches(m_controller->document(), [&](auto &patches) { patches.append(patch); }, tr("Add pressure patch"));
    refreshLoadsPage();
    selectPatch(patch.id);
    emit modelEdited();
}

void ProjectPanel::editSelectedPatch()
{
    const int row = m_loadsTable->currentRow();
    if (!m_controller || row < 0 || row >= activePatches(m_controller->document()).size()) return;
    const auto domain = patchDomain(m_controller->document());
    PressurePatchDialog dialog(domain, domain.patches[row], this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto patch = dialog.patch();
    editActivePatches(m_controller->document(), [&](auto &patches) { patches[row] = patch; }, tr("Edit pressure patch"));
    refreshLoadsPage();
    selectPatch(patch.id);
}

void ProjectPanel::selectPatch(const QUuid &id)
{
    if (!m_controller) return;
    navigateToField("loads");
    const auto &patches = activePatches(m_controller->document());
    const QSignalBlocker blocker(m_loadsTable);
    for (int row = 0; row < patches.size(); ++row) if (patches[row].id == id) m_loadsTable->selectRow(row);
}

void ProjectPanel::duplicateSelectedPatch()
{
    if (!m_controller)
        return;
    int row = m_loadsTable->currentRow();
    if (row < 0)
        return;
    editActivePatches(m_controller->document(), [row](QVector<PressurePatch> &patches) {
        if (row >= patches.size())
            return;
        PressurePatch copy = patches[row];
        copy.id = QUuid::createUuid();
        copy.name += " (copy)";
        patches.insert(row + 1, copy);
    });
    refreshLoadsPage();
    emit modelEdited();
}

void ProjectPanel::deleteSelectedPatch()
{
    if (!m_controller)
        return;
    int row = m_loadsTable->currentRow();
    if (row < 0)
        return;
    editActivePatches(m_controller->document(), [row](QVector<PressurePatch> &patches) {
        if (row < patches.size())
            patches.removeAt(row);
    });
    refreshLoadsPage();
    emit modelEdited();
}

// --------------------------------------------------------------- Analysis

QWidget *ProjectPanel::buildAnalysisPage()
{
    auto *page = new QWidget(m_details);
    auto *layout = new QVBoxLayout(page);

    auto *backendGroup = new QGroupBox(tr("Backend"), page);
    auto *backendForm = new QFormLayout(backendGroup);

    auto makePathRow = [this, backendGroup](QLineEdit *&edit, const QString &dialogTitle) {
        auto *row = new QWidget(backendGroup);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        edit = new QLineEdit(row);
        auto *browse = new QPushButton(tr("Browse..."), row);
        connect(browse, &QPushButton::clicked, this, [this, edit, dialogTitle] {
            QString path = QFileDialog::getOpenFileName(this, dialogTitle);
            if (!path.isEmpty())
            {
                edit->setText(path);
                emit edit->editingFinished();
            }
        });
        rowLayout->addWidget(edit);
        rowLayout->addWidget(browse);
        return row;
    };

    backendForm->addRow(tr("Python (scripts only):"), makePathRow(m_pythonPathEdit, tr("Select Python Executable")));
    connect(m_pythonPathEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_controller)
            m_controller->analysis()->setPythonExecutable(m_pythonPathEdit->text());
    });
    backendForm->addRow(tr("Solver script or exe:"), makePathRow(m_driverPathEdit, tr("Select Solver Script or Executable")));
    connect(m_driverPathEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_controller)
            m_controller->analysis()->setDriverPath(m_driverPathEdit->text());
    });

    auto *checkButton = new QPushButton(tr("Check Backend"), backendGroup);
    connect(checkButton, &QPushButton::clicked, this, &ProjectPanel::checkBackend);
    backendForm->addRow(checkButton);
    m_backendCheckLabel = new QLabel(backendGroup);
    m_backendCheckLabel->setWordWrap(true);
    backendForm->addRow(m_backendCheckLabel);

    layout->addWidget(backendGroup);

    auto *historyGroup = new QGroupBox(tr("Run history"), page);
    auto *historyLayout = new QVBoxLayout(historyGroup);
    m_runHistoryTable = new QTableWidget(0, 4, historyGroup);
    m_runHistoryTable->setHorizontalHeaderLabels({tr("Run ID"), tr("Started"), tr("Status"), tr("Exit code")});
    m_runHistoryTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_runHistoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    historyLayout->addWidget(m_runHistoryTable);
    auto *openFolderButton = new QPushButton(tr("Open Output Folder"), historyGroup);
    connect(openFolderButton, &QPushButton::clicked, this, [this] {
        int row = m_runHistoryTable->currentRow();
        if (row < 0 || !m_controller)
            return;
        QString runId = m_runHistoryTable->item(row, 0)->text();
        if (const RunRecord *run = m_controller->document()->findRun(runId))
            emit openOutputFolderRequested(run->runDirectory);
    });
    historyLayout->addWidget(openFolderButton);
    auto *loadResultsButton = new QPushButton(tr("Load Selected Run Results"), historyGroup);
    connect(loadResultsButton, &QPushButton::clicked, this, [this] {
        int row = m_runHistoryTable->currentRow();
        if (row >= 0 && m_runHistoryTable->item(row, 0))
            emit loadRunRequested(m_runHistoryTable->item(row, 0)->text());
    });
    historyLayout->addWidget(loadResultsButton);
    layout->addWidget(historyGroup);

    return page;
}

void ProjectPanel::refreshAnalysisPage()
{
    if (!m_controller)
        return;
    m_pythonPathEdit->setText(m_controller->analysis()->pythonExecutable());
    m_driverPathEdit->setText(m_controller->analysis()->driverPath());

    const auto &runs = m_controller->document()->runs();
    m_runHistoryTable->setRowCount(runs.size());
    for (int i = 0; i < runs.size(); ++i) {
        const RunRecord &run = runs[i];
        m_runHistoryTable->setItem(i, 0, new QTableWidgetItem(run.runId));
        m_runHistoryTable->setItem(i, 1, new QTableWidgetItem(run.startedAt.toString(Qt::ISODate)));
        m_runHistoryTable->setItem(i, 2, new QTableWidgetItem(toString(run.status)));
        m_runHistoryTable->setItem(i, 3, new QTableWidgetItem(QString::number(run.exitCode)));
    }
}

void ProjectPanel::checkBackend()
{
    if (!m_controller)
        return;
    m_controller->analysis()->setPythonExecutable(m_pythonPathEdit->text());
    m_controller->analysis()->setDriverPath(m_driverPathEdit->text());
    m_backendCheckLabel->setText(tr("Checking solver and numerical dependencies..."));
    m_controller->analysis()->checkBackend();
}

// ---------------------------------------------------------------- Results

QWidget *ProjectPanel::buildResultsPage()
{
    auto *page = new QWidget(m_details);
    auto *layout = new QVBoxLayout(page);
    auto *note = new QLabel(tr("Switch to the Results tab in the center workspace to choose a field, "
                                "component, and deformation scale, and to probe nodes. This page only "
                                "confirms whether a usable result set is loaded."), page);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();
    return page;
}

} // namespace femgui
