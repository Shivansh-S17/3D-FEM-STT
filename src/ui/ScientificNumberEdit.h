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
    explicit ScientificNumberEdit(QWidget *parent = nullptr) : QLineEdit(parent)
    {
        connect(this, &QLineEdit::textEdited, this, [this] { m_userEdited = true; });
        setAlignment(Qt::AlignRight);
    }
    void setValue(double value)
    {
        m_exactValue = value;
        m_userEdited = false;
        QLineEdit::setText(QString::number(value, 'g', 10));
    }
    void setText(const QString &text)
    {
        m_userEdited = true;
        QLineEdit::setText(text);
    }
    double value() const
    {
        if (!m_userEdited && std::isfinite(m_exactValue)) return m_exactValue;
        bool ok = false;
        const double number = text().trimmed().toDouble(&ok);
        return ok && std::isfinite(number) ? number : std::numeric_limits<double>::quiet_NaN();
    }
private:
    double m_exactValue = std::numeric_limits<double>::quiet_NaN();
    bool m_userEdited = true;
};
}
