#include "ui/InspectorPanel.h"

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

    showProjectSummary();
}

QGroupBox *InspectorPanel::makeReadinessGroup()
{
    auto *group = new QGroupBox(tr("Analysis readiness"), this);
    auto *form = new QFormLayout(group);

    const QIcon incomplete = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    const char *steps[5] = {"X/Y/Z nodes", "Material", "Subgrade", "Pressure loads", "Solver connection"};

    for (int i = 0; i < 5; ++i) {
        m_readinessLabels[i] = new QLabel(tr("Not configured"), group);
        auto *iconLabel = new QLabel(group);
        iconLabel->setPixmap(incomplete.pixmap(16, 16));
        auto *row = new QWidget(group);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(iconLabel);
        rowLayout->addWidget(m_readinessLabels[i]);
        form->addRow(tr(steps[i]), row);
    }
    return group;
}

void InspectorPanel::showProjectSummary()
{
    QLayoutItem *item;
    while ((item = m_layout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    auto *generalGroup = new QGroupBox(tr("General"), this);
    auto *generalForm = new QFormLayout(generalGroup);
    generalForm->addRow(tr("Selection:"), new QLabel(tr("None"), generalGroup));
    generalForm->addRow(tr("Model:"), new QLabel(tr("C3D8 input preview"), generalGroup));
    generalForm->addRow(tr("Units:"), new QLabel(tr("Under review"), generalGroup));
    m_layout->addWidget(generalGroup);

    m_layout->addWidget(makeReadinessGroup());

    auto *note = new QLabel(tr("Select a node, element, face, or result field in the "
                                "viewport to inspect it here."), this);
    note->setWordWrap(true);
    note->setStyleSheet("color: palette(mid);");
    m_layout->addWidget(note);
}

} // namespace femgui
