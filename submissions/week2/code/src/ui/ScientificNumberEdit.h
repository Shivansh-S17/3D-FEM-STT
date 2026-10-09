#pragma once
#include <QLineEdit>
#include <cmath>
#include <limits>

namespace femgui {
// Preserve round-trip double precision and accept scientific notation without
// silently clamping loaded values to a spin box's display range/decimals.
class ScientificNumberEdit : public QLineEdit
{
public:
    explicit ScientificNumberEdit(QWidget *parent = nullptr) : QLineEdit(parent) {}
    void setValue(double value)
    {
        for (int precision = 1; precision <= 17; ++precision) {
            const QString text = QString::number(value, 'g', precision);
            bool ok = false;
            const double parsed = text.toDouble(&ok);
            if (ok && parsed == value) {
                setText(text);
                return;
            }
        }
        setText(QString::number(value, 'g', 17));
    }
    double value() const
    {
        bool ok = false;
        const double number = text().trimmed().toDouble(&ok);
        return ok && std::isfinite(number) ? number : std::numeric_limits<double>::quiet_NaN();
    }
};
}
