#include "ui/InspectorPanel.h"

#include "core/ProjectDocument.h"
#include "core/ValidationIssue.h"

#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

namespace femgui {

InspectorPanel::InspectorPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scrollArea);
    m_layout = new QVBoxLayout(content);
    m_layout->setAlignment(Qt::AlignTop);

    scrollArea->setWidget(content);
    outer->addWidget(scrollArea);

    showProjectSummary(nullptr, {});
}

void InspectorPanel::clearContent()
{
    QLayoutItem *item;
    while ((item = m_layout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
}

QGroupBox *InspectorPanel::makeReadinessGroup(const ProjectDocument *document, const ValidationIssues &issues)
{
    auto *group = new QGroupBox(tr("Analysis readiness"), this);
    auto *form = new QFormLayout(group);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    int errors = 0, warnings = 0;
    for (const ValidationIssue &issue : issues) {
        if (issue.severity == Severity::Error) ++errors;
        else if (issue.severity == Severity::Warning) ++warnings;
    }

    const QIcon okIcon = style()->standardIcon(QStyle::SP_DialogApplyButton);
    const QIcon errorIcon = style()->standardIcon(QStyle::SP_MessageBoxCritical);
    const QIcon warnIcon = style()->standardIcon(QStyle::SP_MessageBoxWarning);

    auto *readyLabel = new QLabel(group);
    auto *readyIcon = new QLabel(group);
    readyIcon->setPixmap((errors == 0 ? okIcon : errorIcon).pixmap(16, 16));
    readyLabel->setText(errors == 0 ? tr("Inputs valid; backend required") : tr("%1 error(s) block Run").arg(errors));
    auto *readyRow = new QWidget(group);
    auto *readyRowLayout = new QHBoxLayout(readyRow);
    readyRowLayout->setContentsMargins(0, 0, 0, 0);
    readyRowLayout->addWidget(readyIcon);
    readyRowLayout->addWidget(readyLabel);
    form->addRow(tr("Validation:"), readyRow);

    if (warnings > 0) {
        auto *warnRow = new QWidget(group);
        auto *warnRowLayout = new QHBoxLayout(warnRow);
        warnRowLayout->setContentsMargins(0, 0, 0, 0);
        auto *warnIconLabel = new QLabel(group);
        warnIconLabel->setPixmap(warnIcon.pixmap(16, 16));
        warnRowLayout->addWidget(warnIconLabel);
        warnRowLayout->addWidget(new QLabel(tr("%1 warning(s)").arg(warnings), group));
        form->addRow(QString(), warnRow);
    }

    if (document && document->hasResultSet()) {
        QString runState = document->isResultSetStale() ? tr("Previous model revision (stale)") : tr("Current model revision");
        form->addRow(tr("Last results:"), new QLabel(runState, group));
    }

    return group;
}

void InspectorPanel::showProjectSummary(const ProjectDocument *document, const ValidationIssues &issues)
{
    clearContent();

    auto *generalGroup = new QGroupBox(tr("General"), this);
    auto *generalForm = new QFormLayout(generalGroup);
    generalForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    generalForm->addRow(tr("Selection:"), new QLabel(tr("None"), generalGroup));
    generalForm->addRow(tr("Project:"), new QLabel(document ? document->name() : tr("Untitled project"), generalGroup));
    generalForm->addRow(tr("Units:"), new QLabel(tr("mm - N - MPa"), generalGroup));
    if (document) {
        generalForm->addRow(tr("Model:"), new QLabel(document->isPlate() ? tr("Rectangular plate") : tr("Structured C3D8 solid"), generalGroup));
        generalForm->addRow(tr("Nodes:"), new QLabel(QString::number(modelNodeCount(document->model())), generalGroup));
        generalForm->addRow(tr("Elements:"), new QLabel(QString::number(modelElementCount(document->model())), generalGroup));
        generalForm->addRow(tr("Model revision:"), new QLabel(QString::number(document->modelRevision()), generalGroup));
    }
    m_layout->addWidget(generalGroup);

    m_layout->addWidget(makeReadinessGroup(document, issues));

    auto *note = new QLabel(tr("Select a node or element in the Mesh/Results viewport to inspect it here."), this);
    note->setWordWrap(true);
    note->setStyleSheet("color: palette(mid);");
    m_layout->addWidget(note);
}

void InspectorPanel::showNode(const NodeInfo &info)
{
    clearContent();

    auto *generalGroup = new QGroupBox(tr("General"), this);
    auto *generalForm = new QFormLayout(generalGroup);
    generalForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    generalForm->addRow(tr("Selection:"), new QLabel(tr("Node"), generalGroup));
    generalForm->addRow(tr("Solver node ID:"), new QLabel(QString::number(info.solverId), generalGroup));
    generalForm->addRow(info.solid ? tr("Grid indices (i, j, k):") : tr("Grid indices (i, j):"),
                        new QLabel(info.solid ? QString("(%1, %2, %3)").arg(info.gridI).arg(info.gridJ).arg(info.gridK)
                                              : QString("(%1, %2)").arg(info.gridI).arg(info.gridJ), generalGroup));
    m_layout->addWidget(generalGroup);

    auto *geomGroup = new QGroupBox(tr("Geometry"), this);
    auto *geomForm = new QFormLayout(geomGroup);
    geomForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    geomForm->addRow(tr("X:"), new QLabel(QString("%1 mm").arg(info.x), geomGroup));
    geomForm->addRow(tr("Y:"), new QLabel(QString("%1 mm").arg(info.y), geomGroup));
    if (info.solid) geomForm->addRow(tr("Z:"), new QLabel(QString("%1 mm").arg(info.z), geomGroup));
    geomForm->addRow(info.solid ? tr("DOFs (u, v, w):") : tr("DOFs (w, theta_x, theta_y, u, v):"),
                      new QLabel(QString("%1..%2").arg(info.solverId * info.dofsPerNode)
                                                       .arg(info.solverId * info.dofsPerNode + info.dofsPerNode - 1), geomGroup));
    m_layout->addWidget(geomGroup);

    if (info.hasResults) {
        auto *resultsGroup = new QGroupBox(tr("Node results"), this);
        auto *resultsForm = new QFormLayout(resultsGroup);
    resultsForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        auto *context = new QLabel(info.resultContext, resultsGroup); context->setWordWrap(true);
        resultsForm->addRow(context);
        for (auto it = info.fieldValues.constBegin(); it != info.fieldValues.constEnd(); ++it) {
            QString unit = info.fieldUnits.value(it.key());
            resultsForm->addRow(it.key() + ":", new QLabel(QString("%1 %2").arg(it.value()).arg(unit), resultsGroup));
        }
        m_layout->addWidget(resultsGroup);
    } else {
        auto *note = new QLabel(tr("No completed run is loaded; showing geometry only."), this);
        note->setWordWrap(true);
        note->setStyleSheet("color: palette(mid);");
        m_layout->addWidget(note);
    }
}

void InspectorPanel::showElement(const ElementInfo &info)
{
    clearContent();

    auto *generalGroup = new QGroupBox(tr("General"), this);
    auto *generalForm = new QFormLayout(generalGroup);
    generalForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    generalForm->addRow(tr("Selection:"), new QLabel(tr("Element"), generalGroup));
    generalForm->addRow(tr("Solver element ID:"), new QLabel(QString::number(info.solverId), generalGroup));
    if (!info.resultContext.isEmpty()) generalForm->addRow(tr("Results:"), new QLabel(info.resultContext, generalGroup));
    QStringList nodeIdStrings;
    for (vtkIdType id : info.nodeIds)
        nodeIdStrings << QString::number(id);
    generalForm->addRow(tr("Node IDs (ordered):"), new QLabel(nodeIdStrings.join(", "), generalGroup));
    m_layout->addWidget(generalGroup);

    auto *geomGroup = new QGroupBox(tr("Geometry"), this);
    auto *geomForm = new QFormLayout(geomGroup);
    geomForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    geomForm->addRow(tr("Bounds X:"), new QLabel(QString("%1 .. %2 mm").arg(info.xmin).arg(info.xmax), geomGroup));
    geomForm->addRow(tr("Bounds Y:"), new QLabel(QString("%1 .. %2 mm").arg(info.ymin).arg(info.ymax), geomGroup));
    if (info.solid) geomForm->addRow(tr("Bounds Z:"), new QLabel(QString("%1 .. %2 mm").arg(info.zmin).arg(info.zmax), geomGroup));
    geomForm->addRow(info.solid ? tr("Size (a x b x c):") : tr("Size (a x b):"),
                     new QLabel(info.solid ? QString("%1 x %2 x %3 mm").arg(info.width).arg(info.height).arg(info.depth)
                                           : QString("%1 x %2 mm").arg(info.width).arg(info.height), geomGroup));
    geomForm->addRow(info.solid ? tr("Volume:") : tr("Area:"),
                     new QLabel(info.solid ? QString("%1 mm^3").arg(info.volume) : QString("%1 mm^2").arg(info.area), geomGroup));
    geomForm->addRow(tr("Aspect ratio:"), new QLabel(QString::number(info.aspectRatio, 'g', 3), geomGroup));
    geomForm->addRow(tr("DOFs per node:"), new QLabel(QString("%1 (%2 total)").arg(info.dofsPerNode).arg(info.dofsPerNode * info.nodeIds.size()), geomGroup));
    if (info.solid) {
        geomForm->addRow(tr("Loaded face:"), new QLabel(info.onLoadedFace ? tr("Touches z-min") : tr("No"), geomGroup));
        geomForm->addRow(tr("Foundation face:"), new QLabel(info.onFoundationFace ? tr("Touches z-max") : tr("No"), geomGroup));
    }
    m_layout->addWidget(geomGroup);

    auto *loadsGroup = new QGroupBox(tr("Loads"), this);
    auto *loadsForm = new QFormLayout(loadsGroup);
    loadsForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    if (info.overlappingPatchNames.isEmpty()) {
        loadsForm->addRow(new QLabel(tr("No pressure patch applies to this element."), loadsGroup));
    } else {
        QStringList patchNames;
        for (const QString &name : info.overlappingPatchNames)
            patchNames << name;
        loadsForm->addRow(tr("Overlapping patches:"), new QLabel(patchNames.join(", "), loadsGroup));
        loadsForm->addRow(tr("Applied pressure resultant:"), new QLabel(QString("%1 N (sum of loaded-face overlap area x q)").arg(info.appliedPressureResultant), loadsGroup));
    }
    m_layout->addWidget(loadsGroup);
}

void InspectorPanel::showPatch(const PressurePatch &p)
{
    clearContent();
    auto *group = new QGroupBox(tr("Pressure patch"), this);
    auto *form = new QFormLayout(group);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    auto add = [&](const QString &label, const QString &text) {
        auto *value = new QLabel(text, group); value->setWordWrap(true); form->addRow(label, value);
    };
    add(tr("Name:"), p.name);
    add(tr("ID:"), p.id.toString(QUuid::WithoutBraces));
    add(tr("X bounds:"), tr("%1 .. %2 mm").arg(p.x1).arg(p.x2));
    add(tr("Y bounds:"), tr("%1 .. %2 mm").arg(p.y1).arg(p.y2));
    add(tr("Pressure:"), tr("%1 MPa").arg(p.q));
    add(tr("Area:"), tr("%1 mm²").arg(p.area()));
    add(tr("Equivalent force:"), tr("%1 N").arg(p.equivalentForce()));
    add(tr("Analysis:"), p.enabledForAnalysis ? tr("Enabled") : tr("Disabled"));
    add(tr("Display:"), p.visible ? tr("Visible") : tr("Hidden; analysis enablement is separate"));
    m_layout->addWidget(group);
}

} // namespace femgui
