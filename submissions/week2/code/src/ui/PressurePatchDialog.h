#pragma once
#include <QDialog>
#include "core/PlateDefinition.h"

class QLineEdit;
class QCheckBox;
class QLabel;
class QDialogButtonBox;
namespace femgui {
class ScientificNumberEdit;
class PressurePatchDialog : public QDialog
{
public:
    PressurePatchDialog(const PlateDefinition &plate, const PressurePatch &draft, QWidget *parent = nullptr);
    PressurePatch patch() const;
private:
    friend class IntegrationTests;
    void validate();
    PlateDefinition m_plate;
    PressurePatch m_draft;
    QLineEdit *m_name;
    ScientificNumberEdit *m_x1, *m_x2, *m_y1, *m_y2, *m_q;
    QCheckBox *m_enabled, *m_visible;
    QLabel *m_summary;
    QDialogButtonBox *m_buttons;
};
}
