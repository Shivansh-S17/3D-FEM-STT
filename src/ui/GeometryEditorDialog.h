#pragma once

#include <QDialog>
#include <QVector>

class QDialogButtonBox;
class QHBoxLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

namespace femgui {

class ScientificNumberEdit;

class GeometryEditorDialog : public QDialog
{
    Q_OBJECT

public:
    GeometryEditorDialog(const QVector<double> &xCoordinates,
                         const QVector<double> &yCoordinates,
                         const QVector<double> &zCoordinates,
                         bool solid, QWidget *parent = nullptr);

    QVector<double> xCoordinates() const;
    QVector<double> yCoordinates() const;
    QVector<double> zCoordinates() const;

private:
    friend class IntegrationTests;
    struct AxisEditor {
        QPlainTextEdit *values = nullptr;
        ScientificNumberEdit *start = nullptr;
        ScientificNumberEdit *end = nullptr;
        QSpinBox *intervals = nullptr;
        QLabel *status = nullptr;
        QPushButton *generate = nullptr;
    };

    AxisEditor buildAxis(const QString &axisName, const QVector<double> &coordinates,
                         QHBoxLayout *container);
    QVector<double> parseAxis(const AxisEditor &axis, QString *error = nullptr) const;
    void generateAxis(AxisEditor &axis);
    void validate();
    void buildUniformGridGenerator(QVBoxLayout *parentLayout,
                                   const QVector<double> &x,
                                   const QVector<double> &y,
                                   const QVector<double> &z);
    void updateGridEstimate();
    void generateUniformGrid();

    AxisEditor m_x;
    AxisEditor m_y;
    AxisEditor m_z;
    bool m_solid = true;
    QDialogButtonBox *m_buttons = nullptr;

    ScientificNumberEdit *m_gridLx = nullptr;
    ScientificNumberEdit *m_gridLy = nullptr;
    ScientificNumberEdit *m_gridLz = nullptr;
    QSpinBox *m_gridNx = nullptr;
    QSpinBox *m_gridNy = nullptr;
    QSpinBox *m_gridNz = nullptr;
    QLabel *m_gridEstimateLabel = nullptr;
};

} // namespace femgui
