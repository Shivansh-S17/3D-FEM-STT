#include "ui/ProjectPanel.h"
#include "ui/GeometryEditorDialog.h"
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

#include <QComboBox>
#include <QUuid>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <vtkPointData.h>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QScrollArea>
#include <QStackedWidget>
#include "ui/UiStyle.h"
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTreeWidget>
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
constexpr int kPageAdvanced = 8;

QLabel *pageTitle(const QString &title, const QString &intro, QVBoxLayout *layout, QWidget *parent)
{
    auto *heading = new QLabel(title, parent);
    heading->setProperty("role", "pageTitle");
    layout->addWidget(heading);
    auto *description = new QLabel(intro, parent);
    description->setProperty("role", "pageIntro");
    description->setWordWrap(true);
    layout->addWidget(description);
    return heading;
}

QLabel *validationLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setProperty("validation", true);
    label->setWordWrap(true);
    label->hide();
    return label;
}

void setLabelState(QLabel *label, const char *state)
{
    if (!label) return;
    label->setProperty("state", state);
    label->style()->unpolish(label);
    label->style()->polish(label);
}

QString issueText(const ValidationIssues &issues, const QStringList &prefixes)
{
    QStringList messages;
    for (const auto &issue : issues) {
        if (issue.severity != Severity::Error) continue;
        for (const auto &prefix : prefixes) {
            if (issue.fieldKey.startsWith(prefix)) { messages.append(issue.message); break; }
        }
    }
    messages.removeDuplicates();
    return messages.join("\n");
}

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

class WorkflowTreeWidget : public QTreeWidget
{
public:
    explicit WorkflowTreeWidget(QWidget *parent = nullptr)
        : QTreeWidget(parent)
    {
    }

protected:
    void drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool isSelected = selectionModel() && selectionModel()->isRowSelected(index.row(), index.parent());
        const bool isHovered = (option.state & QStyle::State_MouseOver);
        const bool isDark = UiStyle::currentTheme() != UiStyle::Theme::LiquidGlassLight;

        if (isSelected) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);

            int viewWidth = viewport() ? viewport()->width() : option.rect.width();
            QRect fullRowRect(4, option.rect.top() + 1, viewWidth - 8, option.rect.height() - 2);

            QLinearGradient bgGrad(fullRowRect.topLeft(), fullRowRect.topRight());
            if (isDark) {
                bgGrad.setColorAt(0.0, QColor(10, 132, 255, 105));
                bgGrad.setColorAt(1.0, QColor(10, 132, 255, 28));
                painter->setPen(QPen(QColor(10, 132, 255, 130), 1.0));
            } else {
                bgGrad.setColorAt(0.0, QColor(0, 122, 255, 60));
                bgGrad.setColorAt(1.0, QColor(0, 122, 255, 18));
                painter->setPen(QPen(QColor(0, 122, 255, 90), 1.0));
            }
            painter->setBrush(bgGrad);
            painter->drawRoundedRect(fullRowRect.adjusted(0, 0, -1, -1), 8.0, 8.0);

            // Specular left accent bar (3px width)
            QRect accentRect(fullRowRect.left(), fullRowRect.top() + 3, 3, fullRowRect.height() - 6);
            painter->setPen(Qt::NoPen);
            painter->setBrush(isDark ? QColor(10, 132, 255) : QColor(0, 122, 255));
            painter->drawRoundedRect(accentRect, 1.5, 1.5);

            painter->restore();
        } else if (isHovered) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            int viewWidth = viewport() ? viewport()->width() : option.rect.width();
            QRect fullRowRect(4, option.rect.top() + 1, viewWidth - 8, option.rect.height() - 2);
            painter->setPen(Qt::NoPen);
            painter->setBrush(isDark ? QColor(255, 255, 255, 14) : QColor(0, 0, 0, 10));
            painter->drawRoundedRect(fullRowRect, 8.0, 8.0);
            painter->restore();
        }

        // Suppress default QStyle selection/hover states so individual cell pills are not drawn
        QStyleOptionViewItem rowOption = option;
        rowOption.state &= ~QStyle::State_Selected;
        rowOption.state &= ~QStyle::State_MouseOver;

        QTreeWidget::drawRow(painter, rowOption, index);
    }
};

class WorkflowTreeDelegate : public QStyledItemDelegate
{
public:
    explicit WorkflowTreeDelegate(QTreeWidget *tree, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_tree(tree)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool isSelected = m_tree && m_tree->selectionModel() &&
                                m_tree->selectionModel()->isRowSelected(index.row(), index.parent());
        const bool isDark = UiStyle::currentTheme() != UiStyle::Theme::LiquidGlassLight;

        if (index.column() == 0) {
            QStyleOptionViewItem opt = option;
            opt.state &= ~QStyle::State_Selected;
            opt.state &= ~QStyle::State_MouseOver;
            if (isSelected) {
                opt.font.setBold(true);
                opt.palette.setColor(QPalette::Text, isDark ? Qt::white : QColor(0, 122, 255));
            }
            QStyledItemDelegate::paint(painter, opt, index);
        } else if (index.column() == 1) {
            QString text = index.data(Qt::DisplayRole).toString().trimmed();
            if (!text.isEmpty()) {
                drawStatusBadge(painter, option.rect, text, isDark);
            }
        }
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(qMax(s.height(), 32));
        if (index.column() == 1) {
            QString text = index.data(Qt::DisplayRole).toString().trimmed();
            if (!text.isEmpty()) {
                QFont badgeFont = option.font;
                badgeFont.setPointSize(qMax(badgeFont.pointSize() - 2, 9));
                badgeFont.setBold(true);
                QFontMetrics fm(badgeFont);
                s.setWidth(fm.horizontalAdvance(text) + 24);
            } else {
                s.setWidth(0);
            }
        }
        return s;
    }

private:
    void drawStatusBadge(QPainter *painter, const QRect &cellRect, const QString &text, bool isDark) const
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        QFont badgeFont = painter->font();
        badgeFont.setPointSize(qMax(badgeFont.pointSize() - 2, 9));
        badgeFont.setBold(true);
        painter->setFont(badgeFont);

        QFontMetrics fm(badgeFont);
        int textWidth = fm.horizontalAdvance(text);
        int badgeWidth = textWidth + 14;
        int badgeHeight = qBound(18, fm.height() + 4, 22);

        int x = cellRect.right() - badgeWidth - 6;
        int y = cellRect.top() + (cellRect.height() - badgeHeight) / 2;
        QRect badgeRect(x, y, badgeWidth, badgeHeight);

        QColor bgColor;
        QColor textColor;
        QColor borderColor;

        if (text == tr("Ready")) {
            bgColor = isDark ? QColor(48, 209, 88, 40) : QColor(52, 199, 89, 35);
            textColor = isDark ? QColor(48, 209, 88) : QColor(36, 138, 61);
            borderColor = isDark ? QColor(48, 209, 88, 90) : QColor(52, 199, 89, 80);
        } else if (text == tr("Fix")) {
            bgColor = isDark ? QColor(255, 69, 58, 45) : QColor(255, 59, 48, 35);
            textColor = isDark ? QColor(255, 69, 58) : QColor(215, 0, 21);
            borderColor = isDark ? QColor(255, 69, 58, 100) : QColor(255, 59, 48, 80);
        } else if (text == tr("Run")) {
            bgColor = isDark ? QColor(10, 132, 255, 50) : QColor(0, 122, 255, 40);
            textColor = isDark ? QColor(10, 132, 255) : QColor(0, 122, 255);
            borderColor = isDark ? QColor(10, 132, 255, 110) : QColor(0, 122, 255, 90);
        } else if (text == tr("Open")) {
            bgColor = isDark ? QColor(255, 255, 255, 20) : QColor(0, 0, 0, 12);
            textColor = isDark ? QColor(200, 215, 230) : QColor(70, 70, 75);
            borderColor = isDark ? QColor(255, 255, 255, 35) : QColor(0, 0, 0, 20);
        } else {
            bgColor = isDark ? QColor(255, 255, 255, 14) : QColor(0, 0, 0, 10);
            textColor = isDark ? QColor(142, 156, 174) : QColor(142, 142, 147);
            borderColor = isDark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 18);
        }

        painter->setPen(QPen(borderColor, 1.0));
        painter->setBrush(bgColor);
        painter->drawRoundedRect(badgeRect, 6.0, 6.0);

        painter->setPen(textColor);
        painter->drawText(badgeRect, Qt::AlignCenter, text);

        painter->restore();
    }

    QTreeWidget *m_tree = nullptr;
};
}

ProjectPanel::ProjectPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Vertical, this);

    m_tree = new WorkflowTreeWidget(splitter);
    m_tree->setAccessibleName(tr("Analysis workflow"));
    m_tree->setAccessibleDescription(tr("Numbered setup stages with readiness status"));
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    populateTree();

    m_details = new QStackedWidget(splitter);
    m_details->setAccessibleName(tr("Selected workflow stage"));
    for (auto *page : {buildOverviewPage(), buildMaterialPage(), buildFoundationPage(), buildCoordinatesPage(),
                       buildMeshPage(), buildLoadsPage(), buildAnalysisPage(), buildResultsPage(), buildAdvancedPage()}) {
        for (auto *form : page->findChildren<QFormLayout *>()) {
            form->setRowWrapPolicy(QFormLayout::WrapLongRows);
            form->setContentsMargins(12, 12, 12, 12);
            form->setHorizontalSpacing(12);
            form->setVerticalSpacing(8);
        }
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
    splitter->setSizes({210, 400});

    layout->addWidget(splitter);

    auto *bottomBar = new QWidget(this);
    auto *bottomLayout = new QVBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(8, 6, 8, 8);
    m_bottomRunButton = new QPushButton(tr("Run Simulation"), bottomBar);
    m_bottomRunButton->setProperty("primary", true);
    m_bottomRunButton->setCursor(Qt::PointingHandCursor);
    m_bottomRunButton->setToolTip(tr("Run finite element simulation (Ctrl+R)"));
    bottomLayout->addWidget(m_bottomRunButton);
    layout->addWidget(bottomBar);

    connect(m_bottomRunButton, &QPushButton::clicked, this, &ProjectPanel::runRequested);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, &ProjectPanel::onCurrentItemChanged);
}

void ProjectPanel::populateTree()
{
    auto addLeaf = [this](QTreeWidgetItem *parentItem, const QString &text, int pageIndex) {
        auto *item = parentItem ? new QTreeWidgetItem(parentItem, QStringList{text})
                                : new QTreeWidgetItem(m_tree, QStringList{text});
        item->setData(0, Qt::UserRole, pageIndex);
        return item;
    };

    m_tree->setObjectName("workflowTree");
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->setMouseTracking(true);
    m_tree->setItemDelegate(new WorkflowTreeDelegate(m_tree, m_tree));
    m_tree->setColumnWidth(1, 48);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    m_overviewStep = addLeaf(nullptr, tr("Overview"), kPageOverview);
    m_modelStep = addLeaf(nullptr, tr("1  Model"), kPageCoordinates);
    addLeaf(m_modelStep, tr("Geometry"), kPageCoordinates);
    addLeaf(m_modelStep, tr("Mesh summary"), kPageMesh);

    m_materialStep = addLeaf(nullptr, tr("2  Material & Foundation"), kPageMaterial);
    addLeaf(m_materialStep, tr("Material"), kPageMaterial);
    addLeaf(m_materialStep, tr("Foundation"), kPageFoundation);

    m_loadsStep = addLeaf(nullptr, tr("3  Loads"), kPageLoads);
    m_analysisStep = addLeaf(nullptr, tr("4  Validate & Run"), kPageAnalysis);
    m_resultsStep = addLeaf(nullptr, tr("5  Results"), kPageResults);
    m_advancedStep = addLeaf(nullptr, tr("Advanced"), kPageAdvanced);

    // Keep every main stage visible on first launch. Selecting a child page
    // expands its parent automatically, while experienced users can expand
    // Model or Material & Foundation when they need the subpages.
    m_modelStep->setExpanded(false);
    m_materialStep->setExpanded(false);
    m_tree->setCurrentItem(m_overviewStep);
}

void ProjectPanel::onCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous)
{
    Q_UNUSED(previous);
    if (!current)
        return;
    const int pageIndex = current->data(0, Qt::UserRole).toInt();
    m_details->setCurrentIndex(pageIndex);
    emit statusMessage(tr("Editing: %1").arg(current->text(0)));
    emit workflowStepChanged(pageIndex);
}

void ProjectPanel::bindToController(ProjectController *controller)
{
    m_controller = controller;
    m_hasValidated = false;
    m_backendCheckLabel->clear();
    connect(controller->analysis(), &AnalysisController::backendChecked, this, [this](bool ok, const QString &message) {
        m_backendCheckLabel->setText(message);
        setLabelState(m_backendCheckLabel, ok ? "success" : "error");
        updateWorkflowStates();
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
    refreshValidationMessages();
    updateWorkflowStates();
}

void ProjectPanel::setValidationIssues(const ValidationIssues &issues)
{
    m_hasValidated = true;
    m_lastIssues = issues;
    refreshOverviewPage();
    refreshValidationMessages();
    refreshAnalysisPage();
    updateWorkflowStates();
}

void ProjectPanel::refreshSummaries()
{
    refreshOverviewPage();
    refreshMeshPage();
    refreshAnalysisPage();
    updateWorkflowStates();
}

void ProjectPanel::navigateToField(const QString &key)
{
    int page = kPageOverview;
    if (key.startsWith("slab") || key.startsWith("material")) page = kPageMaterial;
    else if (key.startsWith("foundation")) page = kPageFoundation;
    else if (key.startsWith("coordinates")) page = kPageCoordinates;
    else if (key.startsWith("mesh")) page = kPageMesh;
    else if (key.startsWith("loads")) page = kPageLoads;
    else if (key.startsWith("backend")) page = kPageAdvanced;
    else if (key.startsWith("run")) page = kPageAnalysis;
    else if (key.startsWith("results")) page = kPageResults;
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
    auto *layout = new QVBoxLayout(page);
    pageTitle(tr("Project overview"),
              tr("Review the active project, then follow the numbered stages to prepare and run the model."), layout, page);

    auto *summaryCard = new QWidget(page);
    summaryCard->setProperty("role", "card");
    auto *form = new QFormLayout(summaryCard);

    m_nameEdit = new QLineEdit(page);
    m_nameEdit->setPlaceholderText(tr("Enter a descriptive project name"));
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
    layout->addWidget(summaryCard);

    m_overviewNextLabel = new QLabel(page);
    m_overviewNextLabel->setWordWrap(true);
    m_overviewNextLabel->setProperty("role", "pageIntro");
    layout->addWidget(m_overviewNextLabel);
    auto *continueButton = new QPushButton(tr("Continue to Model Geometry"), page);
    continueButton->setProperty("primary", true);
    connect(continueButton, &QPushButton::clicked, this, [this] { navigateToField("coordinates"); });
    layout->addWidget(continueButton);
    layout->addStretch();

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
        m_readinessLabel->setText(tr("Validation has not run yet."));
    else
        m_readinessLabel->setText(tr("%1 error(s), %2 warning(s).").arg(errors).arg(warnings));
    if (m_overviewNextLabel) {
        if (errors > 0) m_overviewNextLabel->setText(tr("Next: resolve the highlighted inputs, then validate again."));
        else if (!doc->hasResultSet()) m_overviewNextLabel->setText(tr("Next: review the model stages, then validate and run the analysis."));
        else if (doc->isResultSetStale()) m_overviewNextLabel->setText(tr("Next: run the updated model; the loaded results belong to an earlier revision."));
        else m_overviewNextLabel->setText(tr("The current model has usable results. Open Results to inspect or export them."));
    }
}

// ---------------------------------------------------------------- Material

QWidget *ProjectPanel::buildMaterialPage()
{
    auto *page = new QWidget(m_details);
    auto *layout = new QVBoxLayout(page);
    pageTitle(tr("Material"), tr("Define the elastic material used by every element in the model."), layout, page);
    auto *form = new QFormLayout;

    m_materialPresetCombo = new QComboBox(page);
    m_materialPresetCombo->addItems({
        tr("Custom / Manual"),
        tr("Concrete C25/30 (E = 31,500 MPa, ν = 0.20)"),
        tr("Concrete C30/37 (E = 33,000 MPa, ν = 0.20)"),
        tr("Structural Steel S355 (E = 210,000 MPa, ν = 0.30)"),
        tr("Aluminum 6061-T6 (E = 69,000 MPa, ν = 0.33)")
    });
    connect(m_materialPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProjectPanel::applyMaterialPreset);
    form->addRow(tr("Material preset:"), m_materialPresetCombo);

    m_emodSpin = new ScientificNumberEdit(page);
    m_emodSpin->setToolTip(tr("Elastic modulus E in MPa; must be greater than zero"));
    form->addRow(tr("Young's modulus, E [MPa]:"), m_emodSpin);

    m_nuSpin = new ScientificNumberEdit(page);
    m_nuSpin->setToolTip(tr("Dimensionless Poisson's ratio; must be between -1 and 0.5"));
    form->addRow(tr("Poisson's ratio, ν:"), m_nuSpin);

    m_thicknessSpin = new ScientificNumberEdit(page);
    m_thicknessSpin->setToolTip(tr("For C3D8 this read-only value is derived from the Z coordinates"));
    form->addRow(tr("Thickness, t [mm]:"), m_thicknessSpin);

    connect(m_emodSpin, &QLineEdit::editingFinished, this, &ProjectPanel::applyMaterial);
    connect(m_nuSpin, &QLineEdit::editingFinished, this, &ProjectPanel::applyMaterial);
    connect(m_thicknessSpin, &QLineEdit::editingFinished, this, &ProjectPanel::applyMaterial);

    layout->addLayout(form);
    m_materialValidationLabel = validationLabel(page);
    layout->addWidget(m_materialValidationLabel);

    auto *actionsLayout = new QHBoxLayout;
    auto *applyButton = new QPushButton(tr("Apply"), page);
    applyButton->setToolTip(tr("Apply the material values and revalidate the model"));
    connect(applyButton, &QPushButton::clicked, this, &ProjectPanel::applyMaterial);
    auto *nextButton = new QPushButton(tr("Next: Foundation →"), page);
    nextButton->setProperty("primary", true);
    connect(nextButton, &QPushButton::clicked, this, [this] { navigateToField("foundation"); });
    actionsLayout->addWidget(applyButton);
    actionsLayout->addWidget(nextButton);
    layout->addLayout(actionsLayout);

    layout->addStretch();
    return page;
}

void ProjectPanel::refreshMaterialPage()
{
    if (!m_controller)
        return;
    const auto *doc = m_controller->document();
    double emod = 0.0, nu = 0.0;
    if (doc->isPlate()) {
        const auto &slab = doc->plate().slab;
        emod = slab.emod; nu = slab.nu;
        m_emodSpin->setValue(slab.emod); m_nuSpin->setValue(slab.nu); m_thicknessSpin->setValue(slab.thickness);
        m_thicknessSpin->setReadOnly(false);
    } else {
        const auto &solid = doc->solid();
        emod = solid.material.emod; nu = solid.material.nu;
        m_emodSpin->setValue(solid.material.emod); m_nuSpin->setValue(solid.material.nu);
        m_thicknessSpin->setValue(solid.zNodes.size() >= 2 ? solid.zNodes.last() - solid.zNodes.first() : 0.0);
        m_thicknessSpin->setReadOnly(true);
    }

    int preset = 0;
    if (std::abs(emod - 31500.0) < 1.0 && std::abs(nu - 0.20) < 0.01) preset = 1;
    else if (std::abs(emod - 33000.0) < 1.0 && std::abs(nu - 0.20) < 0.01) preset = 2;
    else if (std::abs(emod - 210000.0) < 1.0 && std::abs(nu - 0.30) < 0.01) preset = 3;
    else if (std::abs(emod - 69000.0) < 1.0 && std::abs(nu - 0.33) < 0.01) preset = 4;
    if (m_materialPresetCombo) {
        const QSignalBlocker blocker(m_materialPresetCombo);
        m_materialPresetCombo->setCurrentIndex(preset);
    }
}

void ProjectPanel::applyMaterialPreset(int index)
{
    if (index <= 0) return;
    double emod = 0.0, nu = 0.0;
    switch (index) {
    case 1: emod = 31500.0; nu = 0.20; break;
    case 2: emod = 33000.0; nu = 0.20; break;
    case 3: emod = 210000.0; nu = 0.30; break;
    case 4: emod = 69000.0; nu = 0.33; break;
    default: return;
    }
    m_emodSpin->setValue(emod);
    m_nuSpin->setValue(nu);
    applyMaterial();
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
    pageTitle(tr("Foundation"), tr("Define the directional foundation coefficients used by the active solver."), layout, page);
    auto *form = new QFormLayout;

    m_foundationPresetCombo = new QComboBox(page);
    m_foundationPresetCombo->addItems({
        tr("Custom / Manual"),
        tr("Rigid Baseline (K = 1)"),
        tr("Soft Soil / Clay (K = 0.015 N/mm³)"),
        tr("Medium Sand (K = 0.050 N/mm³)"),
        tr("Dense Gravel / Rock (K = 0.150 N/mm³)")
    });
    connect(m_foundationPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProjectPanel::applyFoundationPreset);
    form->addRow(tr("Foundation preset:"), m_foundationPresetCombo);

    m_kxSpin = new ScientificNumberEdit(page);
    m_kxSpin->setToolTip(tr("Foundation coefficient in the global X direction"));
    form->addRow(tr("Foundation stiffness, Kx [N/mm³]:"), m_kxSpin);

    m_kySpin = new ScientificNumberEdit(page);
    m_kySpin->setToolTip(tr("Foundation coefficient in the global Y direction"));
    form->addRow(tr("Foundation stiffness, Ky [N/mm³]:"), m_kySpin);

    m_kzSpin = new ScientificNumberEdit(page);
    m_kzSpin->setToolTip(tr("Foundation coefficient in the global Z direction"));
    form->addRow(tr("Foundation stiffness, Kz [N/mm³]:"), m_kzSpin);

    connect(m_kxSpin, &QLineEdit::editingFinished, this, &ProjectPanel::applyFoundation);
    connect(m_kySpin, &QLineEdit::editingFinished, this, &ProjectPanel::applyFoundation);
    connect(m_kzSpin, &QLineEdit::editingFinished, this, &ProjectPanel::applyFoundation);

    layout->addLayout(form);
    auto *note = new QLabel(tr("Units and the physical model for Kx/Ky/Kz represent subgrade reaction modulus in N/mm³."), page);
    note->setWordWrap(true);
    note->setProperty("role", "pageIntro");
    layout->addWidget(note);
    m_foundationValidationLabel = validationLabel(page);
    layout->addWidget(m_foundationValidationLabel);

    auto *actionsLayout = new QHBoxLayout;
    auto *applyButton = new QPushButton(tr("Apply"), page);
    connect(applyButton, &QPushButton::clicked, this, &ProjectPanel::applyFoundation);
    actionsLayout->addWidget(applyButton);

    auto *nav = new QHBoxLayout;
    auto *backBtn = new QPushButton(tr("← Back: Material"), page);
    connect(backBtn, &QPushButton::clicked, this, [this] { navigateToField("material"); });
    auto *nextBtn = new QPushButton(tr("Next: Loads →"), page);
    nextBtn->setProperty("primary", true);
    connect(nextBtn, &QPushButton::clicked, this, [this] { navigateToField("loads"); });
    nav->addWidget(backBtn);
    nav->addWidget(nextBtn);

    layout->addLayout(actionsLayout);
    layout->addLayout(nav);
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

    int preset = 0;
    if (std::abs(foundation.kx - 1.0) < 1e-4 && std::abs(foundation.ky - 1.0) < 1e-4 && std::abs(foundation.kz - 1.0) < 1e-4) preset = 1;
    else if (std::abs(foundation.kz - 0.015) < 1e-4) preset = 2;
    else if (std::abs(foundation.kz - 0.050) < 1e-4) preset = 3;
    else if (std::abs(foundation.kz - 0.150) < 1e-4) preset = 4;
    if (m_foundationPresetCombo) {
        const QSignalBlocker blocker(m_foundationPresetCombo);
        m_foundationPresetCombo->setCurrentIndex(preset);
    }
}

void ProjectPanel::applyFoundationPreset(int index)
{
    if (index <= 0) return;
    double k = 1.0;
    switch (index) {
    case 1: k = 1.0; break;
    case 2: k = 0.015; break;
    case 3: k = 0.050; break;
    case 4: k = 0.150; break;
    default: return;
    }
    m_kxSpin->setValue(k);
    m_kySpin->setValue(k);
    m_kzSpin->setValue(k);
    applyFoundation();
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
    pageTitle(tr("Model geometry"),
              tr("Coordinates define the structured grid. Open the geometry editor for organized axis input, bulk paste, or even-spacing generation."),
              layout, page);

    auto *summaryCard = new QWidget(page);
    summaryCard->setProperty("role", "card");
    auto *summaryLayout = new QVBoxLayout(summaryCard);
    m_geometrySummaryLabel = new QLabel(summaryCard);
    m_geometrySummaryLabel->setWordWrap(true);
    summaryLayout->addWidget(m_geometrySummaryLabel);
    layout->addWidget(summaryCard);

    m_coordinatesValidationLabel = validationLabel(page);
    layout->addWidget(m_coordinatesValidationLabel);

    m_editGeometryButton = new QPushButton(tr("Edit Coordinates..."), page);
    m_editGeometryButton->setProperty("primary", true);
    m_editGeometryButton->setToolTip(tr("Open the full geometry editor with bulk paste and even-spacing tools"));
    connect(m_editGeometryButton, &QPushButton::clicked, this, &ProjectPanel::editGeometry);
    layout->addWidget(m_editGeometryButton);

    auto *nav = new QHBoxLayout;
    auto *backBtn = new QPushButton(tr("← Overview"), page);
    connect(backBtn, &QPushButton::clicked, this, [this] { navigateToField("overview"); });
    auto *nextBtn = new QPushButton(tr("Next: Material & Foundation →"), page);
    nextBtn->setProperty("primary", true);
    connect(nextBtn, &QPushButton::clicked, this, [this] { navigateToField("material"); });
    nav->addWidget(backBtn);
    nav->addWidget(nextBtn);
    layout->addLayout(nav);

    layout->addStretch();
    return page;
}

void ProjectPanel::refreshCoordinatesPage()
{
    if (!m_controller)
        return;
    const auto *doc = m_controller->document();
    const auto axisLine = [](const QString &name, const QVector<double> &axis) {
        if (axis.size() < 2) return QObject::tr("%1: incomplete").arg(name);
        return QObject::tr("%1: %2 coordinates, %3 intervals, %4 to %5 mm")
            .arg(name).arg(axis.size()).arg(axis.size() - 1)
            .arg(axis.first(), 0, 'g', 10).arg(axis.last(), 0, 'g', 10);
    };
    const auto &x = doc->isPlate() ? doc->plate().xNodes : doc->solid().xNodes;
    const auto &y = doc->isPlate() ? doc->plate().yNodes : doc->solid().yNodes;
    QStringList lines{axisLine("X", x), axisLine("Y", y)};
    if (doc->isSolid()) lines.append(axisLine("Z", doc->solid().zNodes));
    lines.append(tr("Every adjacent coordinate pair creates one structured element interval."));
    m_geometrySummaryLabel->setText(lines.join('\n'));
}

void ProjectPanel::editGeometry()
{
    if (!m_controller) return;
    auto *doc = m_controller->document();
    const QVector<double> x = doc->isPlate() ? doc->plate().xNodes : doc->solid().xNodes;
    const QVector<double> y = doc->isPlate() ? doc->plate().yNodes : doc->solid().yNodes;
    const QVector<double> z = doc->isSolid() ? doc->solid().zNodes : QVector<double>{};
    GeometryEditorDialog dialog(x, y, z, doc->isSolid(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QVector<double> newX = dialog.xCoordinates();
    const QVector<double> newY = dialog.yCoordinates();
    const QVector<double> newZ = dialog.zCoordinates();
    if (doc->isPlate())
        doc->editPlate([&](auto &model) { model.xNodes = newX; model.yNodes = newY; }, tr("Edit model geometry"));
    else
        doc->editSolid([&](auto &model) { model.xNodes = newX; model.yNodes = newY; model.zNodes = newZ; }, tr("Edit model geometry"));
    emit modelEdited();
    emit statusMessage(tr("Mesh coordinates updated."));
}

// ------------------------------------------------------------------- Mesh

QWidget *ProjectPanel::buildMeshPage()
{
    auto *page = new QWidget(m_details);
    auto *layout = new QVBoxLayout(page);
    pageTitle(tr("Mesh summary"), tr("Review the mesh derived from the coordinate tables before running the solver."), layout, page);
    m_meshSummaryLabel = new QLabel(page);
    m_meshSummaryLabel->setWordWrap(true);
    layout->addWidget(m_meshSummaryLabel);

    auto *nav = new QHBoxLayout;
    auto *backBtn = new QPushButton(tr("← Geometry"), page);
    connect(backBtn, &QPushButton::clicked, this, [this] { navigateToField("coordinates"); });
    auto *nextBtn = new QPushButton(tr("Next: Material & Foundation →"), page);
    nextBtn->setProperty("primary", true);
    connect(nextBtn, &QPushButton::clicked, this, [this] { navigateToField("material"); });
    nav->addWidget(backBtn);
    nav->addWidget(nextBtn);
    layout->addLayout(nav);

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
           "(fixed for this backend). Use Edit Coordinates on the Model Geometry page to rebuild this mesh.")
            .arg(plate.nodeCount()).arg(plate.nodeCountX()).arg(plate.nodeCountY())
            .arg(plate.elementCount()).arg(plate.dofCount())
            .arg(minDx).arg(minDy).arg(maxAspect, 0, 'g', 3));
}

// ------------------------------------------------------------------ Loads

QWidget *ProjectPanel::buildLoadsPage()
{
    auto *page = new QWidget(m_details);
    auto *layout = new QVBoxLayout(page);
    pageTitle(tr("Pressure loads"),
              tr("Define rectangular pressure patches on the loaded XY face. Enabled patches are combined in one analysis."),
              layout, page);

    m_loadsTable = new QTableWidget(0, 8, page);
    m_loadsTable->setAlternatingRowColors(true);
    m_loadsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_loadsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_loadsTable->setHorizontalHeaderLabels({tr("Name"), tr("x1"), tr("x2"), tr("y1"), tr("y2"), tr("q (MPa)"), tr("Enabled"), tr("Visible")});
    m_loadsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    connect(m_loadsTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = m_loadsTable->currentRow();
        if (!m_updatingLoadsTable && row >= 0 && m_controller && row < activePatches(m_controller->document()).size())
            emit patchSelected(activePatches(m_controller->document())[row].id);
        updatePatchActionStates();
    });
    connect(m_loadsTable, &QTableWidget::cellChanged, this, &ProjectPanel::onLoadsTableCellChanged);
    connect(m_loadsTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { editSelectedPatch(); });
    layout->addWidget(m_loadsTable);

    auto *buttons = new QGridLayout;
    auto *addButton = new QPushButton(tr("New Pressure Load"), page);
    auto *addMenu = new QMenu(addButton);
    addMenu->addAction(tr("Enter Coordinates..."), this, &ProjectPanel::addPatch);
    addMenu->addAction(tr("Draw on Model..."), this, &ProjectPanel::drawPatchRequested);
    addButton->setMenu(addMenu);
    addButton->setToolTip(tr("Create a pressure load by entering its bounds or drawing it on the model"));
    m_duplicatePatchButton = new QPushButton(tr("Duplicate"), page);
    m_deletePatchButton = new QPushButton(tr("Delete"), page);
    m_deletePatchButton->setProperty("destructive", true);
    connect(m_duplicatePatchButton, &QPushButton::clicked, this, &ProjectPanel::duplicateSelectedPatch);
    connect(m_deletePatchButton, &QPushButton::clicked, this, &ProjectPanel::deleteSelectedPatch);
    buttons->addWidget(addButton, 0, 0, 1, 2);
    m_editPatchButton = new QPushButton(tr("Edit Selected"), page);
    connect(m_editPatchButton, &QPushButton::clicked, this, &ProjectPanel::editSelectedPatch);
    buttons->addWidget(m_editPatchButton, 1, 0);
    buttons->addWidget(m_duplicatePatchButton, 1, 1);
    buttons->addWidget(m_deletePatchButton, 2, 0, 1, 2);
    layout->addLayout(buttons);

    m_totalLoadSummaryLabel = new QLabel(page);
    m_totalLoadSummaryLabel->setProperty("role", "card");
    m_totalLoadSummaryLabel->setContentsMargins(10, 8, 10, 8);
    m_totalLoadSummaryLabel->setWordWrap(true);
    layout->addWidget(m_totalLoadSummaryLabel);

    auto *note = new QLabel(tr("All enabled patches enter the same force vector for this backend "
                                "(they are pressure patches, not independent load cases). Overlapping "
                                "patches add pressure in the overlap. Patch outlines remain visible through "
                                "the solid while rotating so loads do not disappear from view."), page);
    note->setWordWrap(true);
    note->setProperty("role", "pageIntro");
    layout->addWidget(note);

    m_loadsValidationLabel = validationLabel(page);
    layout->addWidget(m_loadsValidationLabel);

    auto *nav = new QHBoxLayout;
    auto *backBtn = new QPushButton(tr("← Foundation"), page);
    connect(backBtn, &QPushButton::clicked, this, [this] { navigateToField("foundation"); });
    auto *nextBtn = new QPushButton(tr("Next: Validate & Run →"), page);
    nextBtn->setProperty("primary", true);
    connect(nextBtn, &QPushButton::clicked, this, [this] { navigateToField("run"); });
    nav->addWidget(backBtn);
    nav->addWidget(nextBtn);
    layout->addLayout(nav);

    updatePatchActionStates();
    return page;
}

void ProjectPanel::refreshLoadsPage()
{
    if (!m_controller)
        return;
    m_updatingLoadsTable = true;
    const auto &patches = activePatches(m_controller->document());
    m_loadsTable->setRowCount(patches.size());
    double totalFz = 0.0;
    int activeCount = 0;
    for (int row = 0; row < patches.size(); ++row) {
        const PressurePatch &patch = patches[row];
        if (patch.enabledForAnalysis) {
            double area = std::abs((patch.x2 - patch.x1) * (patch.y2 - patch.y1));
            totalFz += area * std::abs(patch.q);
            ++activeCount;
        }
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
    if (m_totalLoadSummaryLabel) {
        m_totalLoadSummaryLabel->setText(
            tr("Active loads: %1 of %2 patches\nTotal resultant vertical load: %3 kN")
                .arg(activeCount).arg(patches.size()).arg(totalFz / 1000.0, 0, 'f', 2));
    }
    m_updatingLoadsTable = false;
    updatePatchActionStates();
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
    const auto &patches = activePatches(m_controller->document());
    if (row >= patches.size()) return;
    if (QMessageBox::question(this, tr("Delete pressure patch"),
                              tr("Delete “%1”? This can be undone from the Edit menu.").arg(patches[row].name),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
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
    pageTitle(tr("Validate and run"),
              tr("Resolve validation problems, confirm the solver is available, and start the analysis."), layout, page);

    auto *readinessCard = new QWidget(page);
    readinessCard->setProperty("role", "card");
    auto *readinessLayout = new QVBoxLayout(readinessCard);
    m_analysisReadinessLabel = new QLabel(readinessCard);
    m_analysisReadinessLabel->setWordWrap(true);
    readinessLayout->addWidget(m_analysisReadinessLabel);

    m_validationChecklistLabel = new QLabel(readinessCard);
    m_validationChecklistLabel->setWordWrap(true);
    m_validationChecklistLabel->setProperty("role", "subtle");
    readinessLayout->addWidget(m_validationChecklistLabel);

    auto *actions = new QHBoxLayout;
    m_runButton = new QPushButton(tr("Run Analysis"), readinessCard);
    m_stopButton = new QPushButton(tr("Stop"), readinessCard);
    m_runButton->setProperty("primary", true);
    m_stopButton->setProperty("destructive", true);
    m_runButton->setToolTip(tr("Run the active model with the configured solver"));
    m_stopButton->setToolTip(tr("Request cancellation of the running solver"));
    connect(m_runButton, &QPushButton::clicked, this, &ProjectPanel::runRequested);
    connect(m_stopButton, &QPushButton::clicked, this, &ProjectPanel::stopRequested);
    actions->addWidget(m_runButton); actions->addWidget(m_stopButton);
    readinessLayout->addLayout(actions);
    layout->addWidget(readinessCard);

    auto *historyGroup = new QGroupBox(tr("Run history"), page);
    auto *historyLayout = new QVBoxLayout(historyGroup);
    m_runHistoryTable = new QTableWidget(0, 4, historyGroup);
    m_runHistoryTable->setAccessibleName(tr("Analysis run history"));
    m_runHistoryTable->setHorizontalHeaderLabels({tr("Run ID"), tr("Started"), tr("Status"), tr("Exit code")});
    m_runHistoryTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_runHistoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_runHistoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_runHistoryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_runHistoryTable->setAlternatingRowColors(true);
    connect(m_runHistoryTable, &QTableWidget::itemSelectionChanged, this, &ProjectPanel::updateRunHistoryActionStates);
    historyLayout->addWidget(m_runHistoryTable);
    auto *historyButtons = new QVBoxLayout;
    m_openRunFolderButton = new QPushButton(tr("Open Output Folder"), historyGroup);
    connect(m_openRunFolderButton, &QPushButton::clicked, this, [this] {
        int row = m_runHistoryTable->currentRow();
        if (row < 0 || !m_controller)
            return;
        QString runId = m_runHistoryTable->item(row, 0)->text();
        if (const RunRecord *run = m_controller->document()->findRun(runId))
            emit openOutputFolderRequested(run->runDirectory);
    });
    m_loadRunResultsButton = new QPushButton(tr("Load Selected Results"), historyGroup);
    connect(m_loadRunResultsButton, &QPushButton::clicked, this, [this] {
        int row = m_runHistoryTable->currentRow();
        if (row >= 0 && m_runHistoryTable->item(row, 0))
            emit loadRunRequested(m_runHistoryTable->item(row, 0)->text());
    });
    historyButtons->addWidget(m_openRunFolderButton); historyButtons->addWidget(m_loadRunResultsButton);
    historyLayout->addLayout(historyButtons);
    layout->addWidget(historyGroup);

    auto *nav = new QHBoxLayout;
    auto *backBtn = new QPushButton(tr("← Back: Loads"), page);
    connect(backBtn, &QPushButton::clicked, this, [this] { navigateToField("loads"); });
    auto *nextBtn = new QPushButton(tr("Results Dashboard →"), page);
    nextBtn->setProperty("primary", true);
    connect(nextBtn, &QPushButton::clicked, this, [this] { navigateToField("results"); });
    nav->addWidget(backBtn);
    nav->addWidget(nextBtn);
    layout->addLayout(nav);

    updateRunHistoryActionStates();
    return page;
}

void ProjectPanel::refreshAnalysisPage()
{
    if (!m_controller)
        return;
    if (m_pythonPathEdit) m_pythonPathEdit->setText(m_controller->analysis()->pythonExecutable());
    if (m_driverPathEdit) m_driverPathEdit->setText(m_controller->analysis()->driverPath());
    if (m_pythonPathEdit) {
        const bool executableBackend = QFileInfo(m_controller->analysis()->driverPath()).suffix().compare("exe", Qt::CaseInsensitive) == 0;
        m_pythonPathEdit->setEnabled(!executableBackend);
        m_pythonPathEdit->setToolTip(executableBackend
            ? tr("The packaged solver contains its own Python runtime")
            : tr("Python executable used to launch the solver script"));
    }

    const int errors = std::count_if(m_lastIssues.cbegin(), m_lastIssues.cend(), [](const auto &issue) {
        return issue.severity == Severity::Error;
    });
    const auto backend = m_controller->analysis()->backendFilesCheck();
    if (m_analysisReadinessLabel) {
        if (m_controller->analysis()->isRunning()) {
            m_analysisReadinessLabel->setText(tr("Analysis is running. Model editors are locked until it finishes or is cancelled."));
            setLabelState(m_analysisReadinessLabel, "warning");
        } else if (errors > 0) {
            m_analysisReadinessLabel->setText(tr("%1 validation error(s) block the analysis. Double-click a problem below to open its input.").arg(errors));
            setLabelState(m_analysisReadinessLabel, "error");
        } else if (!backend.ok) {
            m_analysisReadinessLabel->setText(tr("Model inputs are valid, but the solver backend is unavailable: %1").arg(backend.message));
            setLabelState(m_analysisReadinessLabel, "error");
        } else {
            m_analysisReadinessLabel->setText(tr("Ready to run. Model inputs are valid and the solver backend is available."));
            setLabelState(m_analysisReadinessLabel, "success");
        }
    }

    if (m_validationChecklistLabel) {
        QStringList checks;
        const auto *doc = m_controller->document();
        const bool geomOk = modelNodeCount(doc->model()) >= 4 && modelElementCount(doc->model()) >= 1;
        checks << (geomOk ? tr("✓ Model Geometry: %1 nodes, %2 elements")
                                .arg(modelNodeCount(doc->model())).arg(modelElementCount(doc->model()))
                          : tr("✗ Model Geometry: Incomplete"));
        checks << tr("✓ Material & Foundation: Configured");
        int activeLoads = 0;
        for (const auto &p : activePatches(doc)) if (p.enabledForAnalysis) ++activeLoads;
        checks << (activeLoads > 0 ? tr("✓ Pressure Loads: %1 active patch(es)").arg(activeLoads)
                                   : tr("⚠ Pressure Loads: 0 active patches"));
        checks << (backend.ok ? tr("✓ Solver Backend: Available") : tr("✗ Solver Backend: %1").arg(backend.message));
        m_validationChecklistLabel->setText(checks.join("\n"));
    }

    const auto &runs = m_controller->document()->runs();
    m_runHistoryTable->setRowCount(runs.size());
    for (int i = 0; i < runs.size(); ++i) {
        const RunRecord &run = runs[i];
        m_runHistoryTable->setItem(i, 0, new QTableWidgetItem(run.runId));
        m_runHistoryTable->setItem(i, 1, new QTableWidgetItem(run.startedAt.toString(Qt::ISODate)));
        m_runHistoryTable->setItem(i, 2, new QTableWidgetItem(toString(run.status)));
        m_runHistoryTable->setItem(i, 3, new QTableWidgetItem(QString::number(run.exitCode)));
    }
    updateRunHistoryActionStates();

    if (m_resultsStateLabel) {
        const auto *doc = m_controller->document();
        if (!doc->hasResultSet()) {
            m_resultsStateLabel->setText(tr("No results are loaded yet. Complete Validate & Run first."));
            setLabelState(m_resultsStateLabel, "warning");
            if (m_showResultsButton) m_showResultsButton->setEnabled(false);
            if (m_runAgainButton) m_runAgainButton->setVisible(false);
            if (m_exportCsvButton) m_exportCsvButton->setEnabled(false);
            if (m_exportImageButton) m_exportImageButton->setEnabled(false);
            if (m_openRunFolderButtonResults) m_openRunFolderButtonResults->setEnabled(false);
            if (m_resultsExtremaLabel) m_resultsExtremaLabel->hide();
            if (m_resultsEquilibriumLabel) m_resultsEquilibriumLabel->hide();
        } else if (doc->isResultSetStale()) {
            m_resultsStateLabel->setText(tr("Results from an earlier model revision are loaded. Run the current model before using them as current results."));
            setLabelState(m_resultsStateLabel, "warning");
            if (m_showResultsButton) m_showResultsButton->setEnabled(true);
            if (m_runAgainButton) m_runAgainButton->setVisible(true);
            if (m_exportCsvButton) m_exportCsvButton->setEnabled(true);
            if (m_exportImageButton) m_exportImageButton->setEnabled(true);
            if (m_openRunFolderButtonResults) m_openRunFolderButtonResults->setEnabled(true);
            if (m_resultsExtremaLabel) m_resultsExtremaLabel->show();
            if (m_resultsEquilibriumLabel) m_resultsEquilibriumLabel->show();
        } else {
            m_resultsStateLabel->setText(tr("Current results are loaded and ready to inspect or export."));
            setLabelState(m_resultsStateLabel, "success");
            if (m_showResultsButton) m_showResultsButton->setEnabled(true);
            if (m_runAgainButton) m_runAgainButton->setVisible(true);
            if (m_exportCsvButton) m_exportCsvButton->setEnabled(true);
            if (m_exportImageButton) m_exportImageButton->setEnabled(true);
            if (m_openRunFolderButtonResults) m_openRunFolderButtonResults->setEnabled(true);
            if (m_resultsExtremaLabel) m_resultsExtremaLabel->show();
            if (m_resultsEquilibriumLabel) m_resultsEquilibriumLabel->show();
        }

        if (doc->hasResultSet() && m_resultsExtremaLabel) {
            QStringList metrics;
            metrics << tr("Run: %1").arg(doc->resultSet().runId());
            metrics << tr("Backend: %1").arg(doc->backendId());
            auto *grid = doc->resultSet().grid();
            if (grid && grid->GetPointData()) {
                if (auto *uMag = grid->GetPointData()->GetArray("displacement_magnitude")) {
                    double r[2]; uMag->GetRange(r);
                    metrics << tr("Peak displacement: %1 mm").arg(r[1], 0, 'g', 4);
                } else if (auto *w = grid->GetPointData()->GetArray("w")) {
                    double r[2]; w->GetRange(r);
                    metrics << tr("Displacement (w): [%1, %2] mm").arg(r[0], 0, 'g', 4).arg(r[1], 0, 'g', 4);
                }
                if (auto *svm = grid->GetPointData()->GetArray("von_mises")) {
                    double r[2]; svm->GetRange(r);
                    metrics << tr("Peak von Mises stress: %1 MPa").arg(r[1], 0, 'g', 4);
                }
            }
            m_resultsExtremaLabel->setText(metrics.join("\n"));
            if (m_resultsEquilibriumLabel) {
                m_resultsEquilibriumLabel->setText(tr("Results loaded for this run. Engineering acceptance remains provisional."));
                setLabelState(m_resultsEquilibriumLabel, "warning");
            }
        }
    }
    updateWorkflowStates();
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

void ProjectPanel::refreshValidationMessages()
{
    auto show = [](QLabel *label, const QString &text) {
        if (!label) return;
        label->setText(text);
        label->setVisible(!text.isEmpty());
    };
    show(m_materialValidationLabel, issueText(m_lastIssues, {"material", "slab"}));
    show(m_foundationValidationLabel, issueText(m_lastIssues, {"foundation"}));
    show(m_coordinatesValidationLabel, issueText(m_lastIssues, {"coordinates", "mesh", "options"}));
    show(m_loadsValidationLabel, issueText(m_lastIssues, {"loads"}));
}

void ProjectPanel::updateWorkflowStates()
{
    if (!m_controller || !m_tree) return;
    auto hasErrorFor = [this](const QStringList &prefixes) {
        return std::any_of(m_lastIssues.cbegin(), m_lastIssues.cend(), [&](const auto &issue) {
            if (issue.severity != Severity::Error) return false;
            return std::any_of(prefixes.cbegin(), prefixes.cend(), [&](const QString &prefix) {
                return issue.fieldKey.startsWith(prefix);
            });
        });
    };
    auto state = [&](QTreeWidgetItem *item, const QString &text, const QString &tip) {
        if (!item) return;
        item->setText(1, text);
        item->setIcon(1, QIcon());
        item->setToolTip(0, tip);
        item->setToolTip(1, tip);
    };
    state(m_overviewStep, tr("Open"), tr("Project summary"));
    state(m_modelStep, hasErrorFor({"coordinates", "mesh", "options"}) ? tr("Fix") : tr("Ready"),
          tr("Geometry and structured mesh"));
    state(m_materialStep, hasErrorFor({"material", "slab", "foundation"}) ? tr("Fix") : tr("Ready"),
          tr("Elastic material and foundation coefficients"));
    state(m_loadsStep, hasErrorFor({"loads"}) ? tr("Fix") : tr("Ready"),
          tr("Enabled pressure patches"));
    const bool overallErrors = hasErrors(m_lastIssues);
    const bool backendReady = m_controller->analysis()->backendFilesCheck().ok;
    state(m_analysisStep, overallErrors || !backendReady ? tr("Fix") : tr("Run"),
          overallErrors ? tr("Resolve validation errors before running")
                        : backendReady ? tr("Model is ready to run") : tr("Configure the solver backend"));
    const auto *doc = m_controller->document();
    state(m_resultsStep, !doc->hasResultSet() ? tr("Pending") : doc->isResultSetStale() ? tr("Previous") : tr("Open"),
          !doc->hasResultSet() ? tr("No result set is loaded")
                               : doc->isResultSetStale() ? tr("Loaded results belong to an earlier model revision")
                                                        : tr("Current results are ready to inspect"));
    state(m_advancedStep, QString(), tr("Solver runtime paths and dependency check"));
    m_tree->resizeColumnToContents(1);
}

void ProjectPanel::setAnalysisActionState(bool canRun, bool running)
{
    if (m_runButton) { m_runButton->setEnabled(canRun); m_runButton->setVisible(!running); }
    if (m_bottomRunButton) {
        m_bottomRunButton->setEnabled(canRun);
        m_bottomRunButton->setText(running ? tr("Running Analysis...") : tr("Run Simulation"));
    }
    if (m_stopButton) { m_stopButton->setEnabled(running); m_stopButton->setVisible(running); }
    if (m_runAgainButton) m_runAgainButton->setEnabled(canRun);
    refreshAnalysisPage();
}

void ProjectPanel::updatePatchActionStates()
{
    const bool selected = m_loadsTable && m_loadsTable->currentRow() >= 0;
    if (m_editPatchButton) m_editPatchButton->setEnabled(selected);
    if (m_duplicatePatchButton) m_duplicatePatchButton->setEnabled(selected);
    if (m_deletePatchButton) m_deletePatchButton->setEnabled(selected);
}

void ProjectPanel::updateRunHistoryActionStates()
{
    const bool selected = m_runHistoryTable && m_runHistoryTable->currentRow() >= 0;
    if (m_openRunFolderButton) m_openRunFolderButton->setEnabled(selected);
    if (m_loadRunResultsButton) m_loadRunResultsButton->setEnabled(selected);
}

// ---------------------------------------------------------------- Results

QWidget *ProjectPanel::buildResultsPage()
{
    auto *page = new QWidget(m_details);
    auto *layout = new QVBoxLayout(page);
    pageTitle(tr("Results dashboard"), tr("Summary of solution metrics, peak responses, and quick export actions."), layout, page);

    m_resultsStateLabel = new QLabel(page);
    m_resultsStateLabel->setProperty("role", "card");
    m_resultsStateLabel->setContentsMargins(12, 10, 12, 10);
    m_resultsStateLabel->setWordWrap(true);
    layout->addWidget(m_resultsStateLabel);

    m_resultsExtremaLabel = new QLabel(page);
    m_resultsExtremaLabel->setProperty("role", "card");
    m_resultsExtremaLabel->setContentsMargins(12, 10, 12, 10);
    m_resultsExtremaLabel->setWordWrap(true);
    layout->addWidget(m_resultsExtremaLabel);

    m_resultsEquilibriumLabel = new QLabel(page);
    m_resultsEquilibriumLabel->setProperty("role", "card");
    m_resultsEquilibriumLabel->setContentsMargins(12, 10, 12, 10);
    m_resultsEquilibriumLabel->setWordWrap(true);
    layout->addWidget(m_resultsEquilibriumLabel);

    m_showResultsButton = new QPushButton(tr("Open 3D Results View"), page);
    m_showResultsButton->setProperty("primary", true);
    connect(m_showResultsButton, &QPushButton::clicked, this, &ProjectPanel::showResultsRequested);

    m_runAgainButton = new QPushButton(tr("Re-run Analysis"), page);
    connect(m_runAgainButton, &QPushButton::clicked, this, &ProjectPanel::runRequested);

    auto *resultActions = new QVBoxLayout;
    resultActions->addWidget(m_showResultsButton);
    resultActions->addWidget(m_runAgainButton);
    layout->addLayout(resultActions);

    auto *exportGroup = new QGroupBox(tr("Actions & export"), page);
    auto *exportLayout = new QVBoxLayout(exportGroup);
    m_exportCsvButton = new QPushButton(tr("Export Nodal CSV"), exportGroup);
    m_exportImageButton = new QPushButton(tr("Export PNG Image"), exportGroup);
    m_openRunFolderButtonResults = new QPushButton(tr("Open Output Folder"), exportGroup);
    connect(m_exportCsvButton, &QPushButton::clicked, this, &ProjectPanel::exportResultsCsvRequested);
    connect(m_exportImageButton, &QPushButton::clicked, this, &ProjectPanel::exportResultsImageRequested);
    connect(m_openRunFolderButtonResults, &QPushButton::clicked, this, [this] {
        if (!m_controller) return;
        const auto *doc = m_controller->document();
        if (doc->hasResultSet()) {
            if (const auto *run = doc->findRun(doc->resultSet().runId())) {
                emit openOutputFolderRequested(run->runDirectory);
            }
        }
    });
    exportLayout->addWidget(m_exportCsvButton);
    exportLayout->addWidget(m_exportImageButton);
    exportLayout->addWidget(m_openRunFolderButtonResults);
    layout->addWidget(exportGroup);

    layout->addStretch();
    return page;
}

QWidget *ProjectPanel::buildAdvancedPage()
{
    auto *page = new QWidget(m_details);
    auto *layout = new QVBoxLayout(page);
    pageTitle(tr("Advanced backend settings"),
              tr("Packaged releases configure these paths automatically. Change them only when testing another solver runtime."),
              layout, page);

    auto *backendGroup = new QGroupBox(tr("Solver backend"), page);
    auto *backendForm = new QFormLayout(backendGroup);
    auto makePathRow = [this, backendGroup](QLineEdit *&edit, const QString &dialogTitle) {
        auto *row = new QWidget(backendGroup);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        edit = new QLineEdit(row);
        edit->setClearButtonEnabled(true);
        auto *browse = new QPushButton(tr("Browse..."), row);
        connect(browse, &QPushButton::clicked, this, [this, edit, dialogTitle] {
            const QString path = QFileDialog::getOpenFileName(this, dialogTitle);
            if (!path.isEmpty()) { edit->setText(path); emit edit->editingFinished(); }
        });
        rowLayout->addWidget(edit); rowLayout->addWidget(browse);
        return row;
    };

    backendForm->addRow(tr("Python for script backends:"), makePathRow(m_pythonPathEdit, tr("Select Python Executable")));
    connect(m_pythonPathEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_controller) m_controller->analysis()->setPythonExecutable(m_pythonPathEdit->text());
    });
    backendForm->addRow(tr("Solver script or executable:"), makePathRow(m_driverPathEdit, tr("Select Solver Script or Executable")));
    connect(m_driverPathEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_controller) m_controller->analysis()->setDriverPath(m_driverPathEdit->text());
    });
    auto *checkButton = new QPushButton(tr("Check Backend"), backendGroup);
    connect(checkButton, &QPushButton::clicked, this, &ProjectPanel::checkBackend);
    backendForm->addRow(checkButton);
    m_backendCheckLabel = new QLabel(backendGroup);
    m_backendCheckLabel->setWordWrap(true);
    backendForm->addRow(m_backendCheckLabel);
    layout->addWidget(backendGroup);
    layout->addStretch();
    return page;
}

} // namespace femgui
