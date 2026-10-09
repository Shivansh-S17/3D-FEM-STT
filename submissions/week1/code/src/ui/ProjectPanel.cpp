#include "ui/ProjectPanel.h"

#include <QFormLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace femgui {

namespace {

struct Category
{
    QString name;
    QString description;
    QList<Category> children;
};

const QList<Category> &workflow()
{
    static const QList<Category> tree = {
        {"C3D8 solid model", "Structured eight-node brick model in the received solver's coordinate frame.", {
            {"X coordinates", "nodes.x: increasing X positions of the structured grid.", {}},
            {"Y coordinates", "nodes.y: increasing Y positions of the structured grid.", {}},
            {"Z coordinates", "nodes.z: increasing Z positions through the solid depth.", {}},
            {"Mesh summary", "Derived node, brick-element and displacement-DOF counts.", {}},
        }},
        {"Material & subgrade", "Received material and foundation-input sections.", {
            {"Material (Emod, nu)", "material.Emod and material.nu define homogeneous isotropic elasticity.", {}},
            {"Subgrade (Kx, Ky, Kz)", "subgrade.Kx, Ky and Kz act on the foundation face.", {}},
        }},
        {"Pressure loads", "Rectangular pressure patches on the loaded face.", {
            {"Load rectangles", "Each loads row contains q, x1, x2, y1 and y2.", {}},
            {"Face convention", "The received solver loads z-min and applies subgrade at z-max.", {}},
        }},
        {"Analysis", "Linear-static full-integration C3D8 intake plan.", {
            {"Integration option", "options.reduced_integration is false for the planned supported path.", {}},
            {"Input schema", "Five sections: nodes, material, subgrade, loads and options.", {}},
            {"Solver connection", "C3D8 execution is planned for a later milestone.", {}},
        }},
        {"Results (demo)", "Output categories expected from the received C3D8 code; this build has no real result import.", {
            {"Displacement (u, v, w)", "Three nodal translation components.", {}},
            {"Stress / strain", "Six stress and six engineering-strain components.", {}},
        }},
    };
    return tree;
}

using Field = QPair<QString, QString>;

QList<Field> exampleFields(const QString &title)
{
    // A 3x3x3 subset of the received inputs.json axes is used by the demo view.
    if (title == "X coordinates") return {{"nodes.x", "[0, 1750, 3500] (preview subset)"}};
    if (title == "Y coordinates") return {{"nodes.y", "[0, 1750, 3500] (preview subset)"}};
    if (title == "Z coordinates") return {{"nodes.z", "[0, 100, 200] (preview subset)"}};
    if (title == "Mesh summary") return {{"element", "C3D8 / eight-node brick"},
                                          {"preview", "27 nodes; 8 bricks; 81 translation DOFs"}};
    if (title == "Material (Emod, nu)") return {{"material.Emod", "24000 (source example)"},
                                                 {"material.nu", "0.30 (source example)"}};
    if (title == "Subgrade (Kx, Ky, Kz)") return {{"subgrade.Kx", "50 (source example)"},
                                                   {"subgrade.Ky", "50 (source example)"},
                                                   {"subgrade.Kz", "50 (source example)"}};
    if (title == "Load rectangles") return {{"loads[0].q", "0.8 (source example)"},
                                            {"x1 / x2", "0 / 3500"},
                                            {"y1 / y2", "0 / 3500"}};
    if (title == "Face convention") return {{"load face", "z-min (source coordinates)"},
                                            {"subgrade face", "z-max (source coordinates)"}};
    if (title == "Integration option") return {{"reduced_integration", "false; full integration"}};
    if (title == "Input schema") return {{"sections", "nodes, material, subgrade, loads, options"},
                                         {"source", "C3D8v/python/inputs.json"}};
    if (title == "Displacement (u, v, w)") return {{"expected output", "u, v, w; not computed here"}};
    if (title == "Stress / strain") return {{"expected output", "six stress and six strain components"},
                                            {"current view", "preview geometry only; no computed fields"}};
    return {};
}

} // namespace

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
    m_details->addWidget(makePlaceholderEditor(
        tr("Project"), tr("Select a workflow category on the left to edit its settings.")));

    splitter->addWidget(m_tree);
    splitter->addWidget(m_details);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);

    layout->addWidget(splitter);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, &ProjectPanel::onCurrentItemChanged);
    m_tree->setCurrentItem(m_tree->topLevelItem(0)->child(0)->child(0));
}

QWidget *ProjectPanel::makePlaceholderEditor(const QString &title, const QString &description)
{
    auto *page = new QWidget(m_details);
    auto *form = new QFormLayout(page);

    auto *titleLabel = new QLabel(QString("<b>%1</b>").arg(title), page);
    form->addRow(titleLabel);

    auto *descLabel = new QLabel(description, page);
    descLabel->setWordWrap(true);
    form->addRow(descLabel);

    for (const Field &field : exampleFields(title)) {
        auto *fieldLabel = new QLabel(QString("<b>%1</b><br>%2")
                                          .arg(field.first.toHtmlEscaped(), field.second.toHtmlEscaped()), page);
        fieldLabel->setWordWrap(true);
        fieldLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(fieldLabel);
    }

    auto *statusLabel = new QLabel(tr("Schema preview only. Values are examples; editing, save/open "
                                       "and C3D8 solve are not connected in this milestone."), page);
    statusLabel->setWordWrap(true);
    statusLabel->setStyleSheet("color: palette(mid);");
    form->addRow(statusLabel);

    return page;
}

void ProjectPanel::populateTree()
{
    auto *root = new QTreeWidgetItem(m_tree, QStringList{tr("C3D8 project")});
    root->setExpanded(true);

    const QIcon previewIcon = style()->standardIcon(QStyle::SP_MessageBoxInformation);

    for (const Category &category : workflow()) {
        auto *categoryItem = new QTreeWidgetItem(root, QStringList{category.name});
        categoryItem->setData(0, Qt::UserRole, category.description);
        categoryItem->setExpanded(true);

        for (const Category &child : category.children) {
            auto *childItem = new QTreeWidgetItem(categoryItem, QStringList{child.name});
            childItem->setData(0, Qt::UserRole, child.description);
            childItem->setIcon(0, previewIcon);
        }
    }

    m_tree->expandAll();
}

void ProjectPanel::onCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous)
{
    Q_UNUSED(previous);
    if (!current)
        return;

    const QString title = current->text(0);
    const QString description = current->data(0, Qt::UserRole).toString();

    if (description.isEmpty()) {
        // Root node: keep the default summary page.
        m_details->setCurrentIndex(0);
        emit statusMessage(tr("Project workflow"));
        return;
    }

    auto *page = makePlaceholderEditor(title, description);
    while (m_details->count() > 1)
        m_details->removeWidget(m_details->widget(1));
    m_details->addWidget(page);
    m_details->setCurrentWidget(page);

    emit statusMessage(tr("Editing: %1").arg(title));
}

} // namespace femgui
