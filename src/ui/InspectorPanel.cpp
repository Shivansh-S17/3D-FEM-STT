#include "ui/InspectorPanel.h"

#include "core/ProjectDocument.h"
#include "core/ValidationIssue.h"

#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace femgui {

namespace {
void setupFormLayout(QFormLayout *form)
{
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setContentsMargins(12, 12, 12, 12);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
}
}

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
    m_layout->setContentsMargins(8, 8, 8, 8);
    m_layout->setSpacing(10);
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
    setupFormLayout(form);

    int errors = 0, warnings = 0;
    for (const ValidationIssue &issue : issues) {
        if (issue.severity == Severity::Error) ++errors;
        else if (issue.severity == Severity::Warning) ++warnings;
    }

    auto *readyLabel = new QLabel(group);
    readyLabel->setText(errors == 0 ? tr("Model inputs valid") : tr("%1 error(s) block Run").arg(errors));
    readyLabel->setProperty("state", errors == 0 ? "success" : "error");
    readyLabel->setWordWrap(true);
    form->addRow(tr("Validation:"), readyLabel);

    if (warnings > 0) {
        auto *warningLabel = new QLabel(tr("%1 warning(s)").arg(warnings), group);
        warningLabel->setProperty("state", "warning");
        warningLabel->setWordWrap(true);
        form->addRow(tr("Warnings:"), warningLabel);
    }

    if (document && document->hasResultSet()) {
        QString runState = document->isResultSetStale() ? tr("Previous model revision (stale)") : tr("Current model revision");
        auto *runLabel = new QLabel(runState, group);
        runLabel->setWordWrap(true);
        form->addRow(tr("Last results:"), runLabel);
    }

    return group;
}

void InspectorPanel::showProjectSummary(const ProjectDocument *document, const ValidationIssues &issues)
{
    clearContent();

    auto *domainGroup = new QGroupBox(tr("Physical Domain & Extents"), this);
    auto *domainForm = new QFormLayout(domainGroup);
    setupFormLayout(domainForm);
    domainForm->addRow(tr("Selection:"), new QLabel(tr("None (Model Overview)"), domainGroup));
    domainForm->addRow(tr("Project:"), new QLabel(document ? document->name() : tr("Untitled project"), domainGroup));
    domainForm->addRow(tr("Units:"), new QLabel(tr("mm · N · MPa"), domainGroup));

    if (document) {
        domainForm->addRow(tr("Model:"), new QLabel(document->isPlate() ? tr("Rectangular plate") : tr("Structured C3D8 solid"), domainGroup));

        QVector<double> xCoords, yCoords, zCoords;
        double thickness = 0.0;
        if (document->isPlate()) {
            const auto &p = document->plate();
            xCoords = p.xNodes;
            yCoords = p.yNodes;
            thickness = p.slab.thickness;
        } else {
            const auto &s = document->solid();
            xCoords = s.xNodes;
            yCoords = s.yNodes;
            zCoords = s.zNodes;
        }

        const double xmin = xCoords.isEmpty() ? 0.0 : xCoords.first();
        const double xmax = xCoords.isEmpty() ? 0.0 : xCoords.last();
        const double Lx = xmax - xmin;

        const double ymin = yCoords.isEmpty() ? 0.0 : yCoords.first();
        const double ymax = yCoords.isEmpty() ? 0.0 : yCoords.last();
        const double Ly = ymax - ymin;

        const double zmin = document->isPlate() ? 0.0 : (zCoords.isEmpty() ? 0.0 : zCoords.first());
        const double zmax = document->isPlate() ? thickness : (zCoords.isEmpty() ? 0.0 : zCoords.last());
        const double Lz = zmax - zmin;

        domainForm->addRow(tr("X domain:"), new QLabel(tr("%1 to %2 mm (Lx = %3 mm)")
            .arg(xmin, 0, 'g', 6).arg(xmax, 0, 'g', 6).arg(Lx, 0, 'g', 6), domainGroup));
        domainForm->addRow(tr("Y domain:"), new QLabel(tr("%1 to %2 mm (Ly = %3 mm)")
            .arg(ymin, 0, 'g', 6).arg(ymax, 0, 'g', 6).arg(Ly, 0, 'g', 6), domainGroup));
        domainForm->addRow(tr("Z extent:"), new QLabel(tr("%1 to %2 mm (Lz = %3 mm)")
            .arg(zmin, 0, 'g', 6).arg(zmax, 0, 'g', 6).arg(Lz, 0, 'g', 6), domainGroup));

        const double areaM2 = (Lx * Ly) * 1e-6;
        const double volM3 = (Lx * Ly * Lz) * 1e-9;
        domainForm->addRow(tr("Footprint:"), new QLabel(tr("%1 m² (%2 mm²)")
            .arg(areaM2, 0, 'f', 3).arg(Lx * Ly, 0, 'g', 6), domainGroup));
        domainForm->addRow(tr("Volume:"), new QLabel(tr("%1 m³")
            .arg(volM3, 0, 'f', 4), domainGroup));
        domainForm->addRow(tr("Mesh size:"), new QLabel(tr("%1 nodes · %2 elements")
            .arg(modelNodeCount(document->model()))
            .arg(modelElementCount(document->model())), domainGroup));
    }
    m_layout->addWidget(domainGroup);

    if (document) {
        auto *loadGroup = new QGroupBox(tr("Applied Loading Resultant"), this);
        auto *loadForm = new QFormLayout(loadGroup);
        setupFormLayout(loadForm);

        const QVector<PressurePatch> &patches = document->isPlate()
            ? document->plate().patches
            : document->solid().patches;

        int activeCount = 0;
        double totalForceN = 0.0;
        double momentX = 0.0;
        double momentY = 0.0;
        for (const auto &p : patches) {
            if (p.enabledForAnalysis) {
                ++activeCount;
                const double f = p.equivalentForce();
                totalForceN += f;
                momentX += f * (p.x1 + p.x2) * 0.5;
                momentY += f * (p.y1 + p.y2) * 0.5;
            }
        }

        loadForm->addRow(tr("Active patches:"), new QLabel(tr("%1 of %2 patches")
            .arg(activeCount).arg(patches.size()), loadGroup));
        loadForm->addRow(tr("Resultant Fz:"), new QLabel(tr("%1 kN (%2 N)")
            .arg(totalForceN / 1000.0, 0, 'f', 2).arg(totalForceN, 0, 'g', 6), loadGroup));
        if (totalForceN > 0.0) {
            const double xc = momentX / totalForceN;
            const double yc = momentY / totalForceN;
            loadForm->addRow(tr("Load centroid:"), new QLabel(tr("X = %1 mm, Y = %2 mm")
                .arg(xc, 0, 'f', 1).arg(yc, 0, 'f', 1), loadGroup));
        } else {
            loadForm->addRow(tr("Load centroid:"), new QLabel(tr("None (0 N load)"), loadGroup));
        }
        m_layout->addWidget(loadGroup);
    }

    m_layout->addWidget(makeReadinessGroup(document, issues));

    if (document && document->hasResultSet()) {
        const auto &res = document->resultSet();
        auto *resGroup = new QGroupBox(tr("Latest Run Quick Summary"), this);
        auto *resForm = new QFormLayout(resGroup);
        setupFormLayout(resForm);
        resForm->addRow(tr("Run ID:"), new QLabel(res.runId(), resGroup));

        const auto wExt = res.computeExtrema("w");
        if (wExt.valid) {
            resForm->addRow(tr("Peak vertical disp.:"), new QLabel(tr("%1 mm")
                .arg(wExt.maxAbsolute, 0, 'g', 5), resGroup));
        }
        const auto vmExt = res.computeExtrema("von_mises");
        if (vmExt.valid) {
            resForm->addRow(tr("Peak von Mises:"), new QLabel(tr("%1 MPa")
                .arg(vmExt.maxAbsolute, 0, 'g', 5), resGroup));
        }
        m_layout->addWidget(resGroup);
    }

    auto *note = new QLabel(tr("Select a node or element in the Mesh/Results viewport to inspect its local properties, coordinates, and scalar field results."), this);
    note->setWordWrap(true);
    note->setProperty("role", "pageIntro");
    m_layout->addWidget(note);

    for (auto *label : findChildren<QLabel *>()) {
        label->setWordWrap(true);
    }
}

void InspectorPanel::showNode(const NodeInfo &info)
{
    clearContent();

    auto *generalGroup = new QGroupBox(tr("General"), this);
    auto *generalForm = new QFormLayout(generalGroup);
    setupFormLayout(generalForm);
    generalForm->addRow(tr("Selection:"), new QLabel(tr("Node"), generalGroup));
    generalForm->addRow(tr("Solver node ID:"), new QLabel(QString::number(info.solverId), generalGroup));
    generalForm->addRow(info.solid ? tr("Grid indices (i, j, k):") : tr("Grid indices (i, j):"),
                        new QLabel(info.solid ? QString("(%1, %2, %3)").arg(info.gridI).arg(info.gridJ).arg(info.gridK)
                                              : QString("(%1, %2)").arg(info.gridI).arg(info.gridJ), generalGroup));
    m_layout->addWidget(generalGroup);

    auto *geomGroup = new QGroupBox(tr("Geometry"), this);
    auto *geomForm = new QFormLayout(geomGroup);
    setupFormLayout(geomForm);
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
        setupFormLayout(resultsForm);
        auto *context = new QLabel(info.resultContext, resultsGroup);
        context->setWordWrap(true);
        resultsForm->addRow(context);
        for (auto it = info.fieldValues.constBegin(); it != info.fieldValues.constEnd(); ++it) {
            QString unit = info.fieldUnits.value(it.key());
            resultsForm->addRow(it.key() + ":", new QLabel(QString("%1 %2").arg(it.value()).arg(unit), resultsGroup));
        }
        m_layout->addWidget(resultsGroup);
    } else {
        auto *note = new QLabel(tr("No completed run is loaded; showing geometry only."), this);
        note->setWordWrap(true);
        note->setProperty("role", "pageIntro");
        m_layout->addWidget(note);
    }

    for (auto *label : findChildren<QLabel *>()) {
        label->setWordWrap(true);
    }
}

void InspectorPanel::showElement(const ElementInfo &info)
{
    clearContent();

    auto *generalGroup = new QGroupBox(tr("General"), this);
    auto *generalForm = new QFormLayout(generalGroup);
    setupFormLayout(generalForm);
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
    setupFormLayout(geomForm);
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
    setupFormLayout(loadsForm);
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

    for (auto *label : findChildren<QLabel *>()) {
        label->setWordWrap(true);
    }
}

void InspectorPanel::showPatch(const PressurePatch &p)
{
    clearContent();
    auto *group = new QGroupBox(tr("Pressure patch"), this);
    auto *form = new QFormLayout(group);
    setupFormLayout(form);
    auto add = [&](const QString &label, const QString &text) {
        auto *value = new QLabel(text, group);
        value->setWordWrap(true);
        form->addRow(label, value);
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

    for (auto *label : findChildren<QLabel *>()) {
        label->setWordWrap(true);
    }
}

} // namespace femgui
