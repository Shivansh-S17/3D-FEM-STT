#include "ui/PressurePatchDialog.h"
#include "ui/ScientificNumberEdit.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <cmath>

namespace femgui {
PressurePatchDialog::PressurePatchDialog(const PlateDefinition &plate, const PressurePatch &draft, QWidget *parent)
    : QDialog(parent), m_plate(plate), m_draft(draft)
{
    setWindowTitle(tr("Pressure Load"));
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Define a rectangular pressure region on the loaded surface. Bounds use the model coordinate system."), this);
    intro->setWordWrap(true);
    intro->setProperty("role", "subtle");
    layout->addWidget(intro);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_name = new QLineEdit(draft.name, this);
    form->addRow(tr("Name:"), m_name);
    auto number = [&](const QString &label, double value) {
        auto *edit = new ScientificNumberEdit(this);
        edit->setValue(value);
        form->addRow(label, edit);
        connect(edit, &QLineEdit::textChanged, this, [this] { validate(); });
        return edit;
    };
    m_x1 = number(tr("X minimum [mm]:"), draft.x1);
    m_x2 = number(tr("X maximum [mm]:"), draft.x2);
    m_y1 = number(tr("Y minimum [mm]:"), draft.y1);
    m_y2 = number(tr("Y maximum [mm]:"), draft.y2);
    m_q = number(tr("Pressure, q [MPa]:"), draft.q);
    m_x1->setToolTip(tr("Lower X bound of the loaded rectangle."));
    m_x2->setToolTip(tr("Upper X bound of the loaded rectangle."));
    m_y1->setToolTip(tr("Lower Y bound of the loaded rectangle."));
    m_y2->setToolTip(tr("Upper Y bound of the loaded rectangle."));
    m_q->setToolTip(tr("Positive pressure is applied normal to the loaded surface."));
    m_enabled = new QCheckBox(tr("Enabled for analysis"), this);
    m_enabled->setChecked(draft.enabledForAnalysis);
    m_visible = new QCheckBox(tr("Visible in Model view"), this);
    m_visible->setChecked(draft.visible);
    form->addRow(m_enabled);
    form->addRow(m_visible);
    layout->addLayout(form);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_name, &QLineEdit::textChanged, this, [this] { validate(); });
    layout->addWidget(m_buttons);
    validate();
    resize(460, sizeHint().height());
}

PressurePatch PressurePatchDialog::patch() const
{
    auto patch = m_draft;
    patch.name = m_name->text().trimmed();
    patch.x1 = m_x1->value(); patch.x2 = m_x2->value();
    patch.y1 = m_y1->value(); patch.y2 = m_y2->value(); patch.q = m_q->value();
    patch.enabledForAnalysis = m_enabled->isChecked(); patch.visible = m_visible->isChecked();
    return patch;
}

void PressurePatchDialog::validate()
{
    const auto p = patch();
    QString error;
    if (p.name.isEmpty()) error = tr("Enter a patch name.");
    else if (!std::isfinite(p.x1) || !std::isfinite(p.x2) || !std::isfinite(p.y1) || !std::isfinite(p.y2) || !std::isfinite(p.q))
        error = tr("Enter finite numeric bounds and pressure.");
    else if (p.x1 >= p.x2 || p.y1 >= p.y2 || p.q < 0 || !std::isfinite(p.equivalentForce()))
        error = tr("Require positive width and height, nonnegative pressure, and a finite force.");
    else if (m_plate.xNodes.size() < 2 || m_plate.yNodes.size() < 2)
        error = tr("Define a valid model grid first.");
    else if (p.x1 < m_plate.xNodes.first() || p.x2 > m_plate.xNodes.last() ||
             p.y1 < m_plate.yNodes.first() || p.y2 > m_plate.yNodes.last())
        error = tr("The patch extends outside the plate. Edit the bounds; they will not be clamped.");
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(error.isEmpty());
    m_summary->setProperty("state", error.isEmpty() ? "success" : "error");
    m_summary->style()->unpolish(m_summary);
    m_summary->style()->polish(m_summary);
    m_summary->setText(error.isEmpty() ? tr("Area: %1 mm²\nEquivalent force: %2 N\nOverlapping enabled patches add pressure. Visibility does not disable a load.")
        .arg(p.area(), 0, 'g', 8).arg(p.equivalentForce(), 0, 'g', 8) : error);
}
}
