#include "ui/GeometryEditorDialog.h"
#include "ui/ScientificNumberEdit.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

#include <cmath>

namespace femgui {

namespace {
QString coordinateText(const QVector<double> &coordinates)
{
    QStringList lines;
    lines.reserve(coordinates.size());
    for (double coordinate : coordinates) lines.append(QString::number(coordinate, 'g', 17));
    return lines.join('\n');
}

void setState(QLabel *label, const char *state)
{
    label->setProperty("state", state);
    label->style()->unpolish(label);
    label->style()->polish(label);
}
}

GeometryEditorDialog::GeometryEditorDialog(const QVector<double> &xCoordinates,
                                           const QVector<double> &yCoordinates,
                                           const QVector<double> &zCoordinates,
                                           bool solid, QWidget *parent)
    : QDialog(parent), m_solid(solid)
{
    setWindowTitle(tr("Edit Model Geometry"));
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Enter grid-plane coordinates directly or generate evenly spaced values. "
                                "Values must be finite, unique, and strictly increasing."), this);
    intro->setWordWrap(true);
    intro->setProperty("role", "pageIntro");
    layout->addWidget(intro);

    buildUniformGridGenerator(layout, xCoordinates, yCoordinates, zCoordinates);

    auto *axes = new QHBoxLayout;
    m_x = buildAxis(tr("X axis [mm]"), xCoordinates, axes);
    m_y = buildAxis(tr("Y axis [mm]"), yCoordinates, axes);
    if (m_solid) m_z = buildAxis(tr("Z axis [mm]"), zCoordinates, axes);
    connect(m_x.generate, &QPushButton::clicked, this, [this] { generateAxis(m_x); });
    connect(m_y.generate, &QPushButton::clicked, this, [this] { generateAxis(m_y); });
    if (m_solid) connect(m_z.generate, &QPushButton::clicked, this, [this] { generateAxis(m_z); });
    layout->addLayout(axes, 1);

    auto *tip = new QLabel(tr("Paste values separated by new lines, spaces, commas, or semicolons. "
                              "Applying the geometry rebuilds the complete structured mesh."), this);
    tip->setWordWrap(true);
    tip->setProperty("role", "pageIntro");
    layout->addWidget(tip);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply Geometry"));
    m_buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    m_buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);

    validate();
    resize(m_solid ? 980 : 700, 680);
}

GeometryEditorDialog::AxisEditor GeometryEditorDialog::buildAxis(const QString &axisName,
                                                                  const QVector<double> &coordinates,
                                                                  QHBoxLayout *container)
{
    AxisEditor axis;
    auto *group = new QGroupBox(axisName, this);
    auto *layout = new QVBoxLayout(group);
    axis.values = new QPlainTextEdit(coordinateText(coordinates), group);
    axis.values->setLineWrapMode(QPlainTextEdit::NoWrap);
    axis.values->setPlaceholderText(tr("One coordinate per line"));
    axis.values->setAccessibleName(axisName + tr(" coordinate values"));
    layout->addWidget(axis.values, 1);

    auto *generator = new QGroupBox(tr("Even spacing"), group);
    auto *form = new QFormLayout(generator);
    axis.start = new ScientificNumberEdit(generator);
    axis.end = new ScientificNumberEdit(generator);
    axis.intervals = new QSpinBox(generator);
    axis.intervals->setRange(1, 1000000);
    axis.start->setValue(coordinates.isEmpty() ? 0.0 : coordinates.first());
    axis.end->setValue(coordinates.size() < 2 ? 1.0 : coordinates.last());
    axis.intervals->setValue(std::max(1, static_cast<int>(coordinates.size()) - 1));
    form->addRow(tr("Start:"), axis.start);
    form->addRow(tr("End:"), axis.end);
    form->addRow(tr("Intervals:"), axis.intervals);
    axis.generate = new QPushButton(tr("Generate Values"), generator);
    form->addRow(axis.generate);
    layout->addWidget(generator);

    axis.status = new QLabel(group);
    axis.status->setWordWrap(true);
    layout->addWidget(axis.status);
    connect(axis.values, &QPlainTextEdit::textChanged, this, &GeometryEditorDialog::validate);
    container->addWidget(group, 1);
    return axis;
}

QVector<double> GeometryEditorDialog::parseAxis(const AxisEditor &axis, QString *error) const
{
    QVector<double> values;
    if (!axis.values) return values;
    const QStringList tokens = axis.values->toPlainText().split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts);
    values.reserve(tokens.size());
    for (const QString &token : tokens) {
        bool ok = false;
        const double value = token.toDouble(&ok);
        if (!ok || !std::isfinite(value)) {
            if (error) *error = tr("“%1” is not a finite number.").arg(token);
            return {};
        }
        if (!values.isEmpty() && value <= values.last()) {
            if (error) *error = tr("Values must be strictly increasing; %1 follows %2.")
                .arg(value, 0, 'g', 12).arg(values.last(), 0, 'g', 12);
            return {};
        }
        values.append(value);
    }
    if (values.size() < 2) {
        if (error) *error = tr("Enter at least two coordinates.");
        return {};
    }
    return values;
}

void GeometryEditorDialog::generateAxis(AxisEditor &axis)
{
    const double start = axis.start->value();
    const double end = axis.end->value();
    if (!std::isfinite(start) || !std::isfinite(end) || end <= start) {
        axis.status->setText(tr("Start and end must be finite, and end must be greater than start."));
        setState(axis.status, "error");
        return;
    }
    const int intervals = axis.intervals->value();
    QVector<double> coordinates;
    coordinates.reserve(intervals + 1);
    for (int i = 0; i <= intervals; ++i)
        coordinates.append(i == intervals ? end : start + (end - start) * i / intervals);
    axis.values->setPlainText(coordinateText(coordinates));
}

void GeometryEditorDialog::validate()
{
    bool valid = true;
    for (AxisEditor *axis : {&m_x, &m_y, m_solid ? &m_z : nullptr}) {
        if (!axis) continue;
        QString error;
        const QVector<double> values = parseAxis(*axis, &error);
        if (error.isEmpty()) {
            axis->status->setText(tr("%1 coordinates · %2 intervals · %3 to %4 mm")
                .arg(values.size()).arg(values.size() - 1)
                .arg(values.first(), 0, 'g', 10).arg(values.last(), 0, 'g', 10));
            setState(axis->status, "success");
        } else {
            axis->status->setText(error);
            setState(axis->status, "error");
            valid = false;
        }
    }
    if (m_buttons) m_buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
}

void GeometryEditorDialog::buildUniformGridGenerator(QVBoxLayout *parentLayout,
                                                     const QVector<double> &x,
                                                     const QVector<double> &y,
                                                     const QVector<double> &z)
{
    auto *group = new QGroupBox(tr("Quick Uniform Domain Generator"), this);
    auto *vbox = new QVBoxLayout(group);

    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("Length X (Lx):")), 0, 0);
    m_gridLx = new ScientificNumberEdit(group);
    grid->addWidget(m_gridLx, 0, 1);

    grid->addWidget(new QLabel(tr("Divisions Nx:")), 0, 2);
    m_gridNx = new QSpinBox(group);
    m_gridNx->setRange(1, 1000);
    grid->addWidget(m_gridNx, 0, 3);

    grid->addWidget(new QLabel(tr("Length Y (Ly):")), 1, 0);
    m_gridLy = new ScientificNumberEdit(group);
    grid->addWidget(m_gridLy, 1, 1);

    grid->addWidget(new QLabel(tr("Divisions Ny:")), 1, 2);
    m_gridNy = new QSpinBox(group);
    m_gridNy->setRange(1, 1000);
    grid->addWidget(m_gridNy, 1, 3);

    if (m_solid) {
        grid->addWidget(new QLabel(tr("Length Z (Lz):")), 2, 0);
        m_gridLz = new ScientificNumberEdit(group);
        grid->addWidget(m_gridLz, 2, 1);

        grid->addWidget(new QLabel(tr("Divisions Nz:")), 2, 2);
        m_gridNz = new QSpinBox(group);
        m_gridNz->setRange(1, 1000);
        grid->addWidget(m_gridNz, 2, 3);
    }

    vbox->addLayout(grid);

    const double defaultLx = (x.size() >= 2) ? std::abs(x.last() - x.first()) : 1000.0;
    const double defaultLy = (y.size() >= 2) ? std::abs(y.last() - y.first()) : 1000.0;
    const double defaultLz = (z.size() >= 2) ? std::abs(z.last() - z.first()) : 200.0;
    const int defaultNx = (x.size() >= 2) ? std::max(1, static_cast<int>(x.size()) - 1) : 10;
    const int defaultNy = (y.size() >= 2) ? std::max(1, static_cast<int>(y.size()) - 1) : 10;
    const int defaultNz = (z.size() >= 2) ? std::max(1, static_cast<int>(z.size()) - 1) : 4;

    m_gridLx->setValue(defaultLx);
    m_gridLy->setValue(defaultLy);
    if (m_gridLz) m_gridLz->setValue(defaultLz);
    m_gridNx->setValue(defaultNx);
    m_gridNy->setValue(defaultNy);
    if (m_gridNz) m_gridNz->setValue(defaultNz);

    auto *bottomRow = new QHBoxLayout;
    m_gridEstimateLabel = new QLabel(group);
    m_gridEstimateLabel->setProperty("role", "pageIntro");
    bottomRow->addWidget(m_gridEstimateLabel, 1);

    auto *genBtn = new QPushButton(tr("Apply Uniform Grid (0 to L)"), group);
    genBtn->setToolTip(tr("Populates all axis coordinate lists from 0 to L with the specified number of divisions."));
    connect(genBtn, &QPushButton::clicked, this, &GeometryEditorDialog::generateUniformGrid);
    bottomRow->addWidget(genBtn);

    vbox->addLayout(bottomRow);

    connect(m_gridLx, &ScientificNumberEdit::textChanged, this, &GeometryEditorDialog::updateGridEstimate);
    connect(m_gridLy, &ScientificNumberEdit::textChanged, this, &GeometryEditorDialog::updateGridEstimate);
    if (m_gridLz) connect(m_gridLz, &ScientificNumberEdit::textChanged, this, &GeometryEditorDialog::updateGridEstimate);
    connect(m_gridNx, qOverload<int>(&QSpinBox::valueChanged), this, &GeometryEditorDialog::updateGridEstimate);
    connect(m_gridNy, qOverload<int>(&QSpinBox::valueChanged), this, &GeometryEditorDialog::updateGridEstimate);
    if (m_gridNz) connect(m_gridNz, qOverload<int>(&QSpinBox::valueChanged), this, &GeometryEditorDialog::updateGridEstimate);

    updateGridEstimate();
    parentLayout->addWidget(group);
}

void GeometryEditorDialog::updateGridEstimate()
{
    if (!m_gridEstimateLabel || !m_gridLx || !m_gridLy || !m_gridNx || !m_gridNy) return;
    const int nx = m_gridNx->value();
    const int ny = m_gridNy->value();
    const int nz = (m_solid && m_gridNz) ? m_gridNz->value() : 1;
    const qint64 elements = static_cast<qint64>(nx) * ny * nz;
    const qint64 nodes = static_cast<qint64>(nx + 1) * (ny + 1) * (m_solid ? (nz + 1) : 1);
    const double dx = nx > 0 ? m_gridLx->value() / nx : 0.0;
    const double dy = ny > 0 ? m_gridLy->value() / ny : 0.0;
    if (m_solid && m_gridLz) {
        const double dz = nz > 0 ? m_gridLz->value() / nz : 0.0;
        m_gridEstimateLabel->setText(tr("Estimate: %1 elements, %2 nodes (dx=%3, dy=%4, dz=%5 mm)")
            .arg(elements).arg(nodes).arg(dx, 0, 'g', 4).arg(dy, 0, 'g', 4).arg(dz, 0, 'g', 4));
    } else {
        m_gridEstimateLabel->setText(tr("Estimate: %1 elements, %2 nodes (dx=%3, dy=%4 mm)")
            .arg(elements).arg(nodes).arg(dx, 0, 'g', 4).arg(dy, 0, 'g', 4));
    }
}

void GeometryEditorDialog::generateUniformGrid()
{
    auto generateList = [](double length, int intervals) -> QVector<double> {
        QVector<double> coords;
        coords.reserve(intervals + 1);
        for (int i = 0; i <= intervals; ++i) {
            coords.append(i == intervals ? length : length * i / intervals);
        }
        return coords;
    };

    if (m_gridLx && m_gridNx && m_x.values) {
        const double lx = m_gridLx->value();
        const int nx = m_gridNx->value();
        m_x.start->setValue(0.0);
        m_x.end->setValue(lx);
        m_x.intervals->setValue(nx);
        m_x.values->setPlainText(coordinateText(generateList(lx, nx)));
    }
    if (m_gridLy && m_gridNy && m_y.values) {
        const double ly = m_gridLy->value();
        const int ny = m_gridNy->value();
        m_y.start->setValue(0.0);
        m_y.end->setValue(ly);
        m_y.intervals->setValue(ny);
        m_y.values->setPlainText(coordinateText(generateList(ly, ny)));
    }
    if (m_solid && m_gridLz && m_gridNz && m_z.values) {
        const double lz = m_gridLz->value();
        const int nz = m_gridNz->value();
        m_z.start->setValue(0.0);
        m_z.end->setValue(lz);
        m_z.intervals->setValue(nz);
        m_z.values->setPlainText(coordinateText(generateList(lz, nz)));
    }
    validate();
}

QVector<double> GeometryEditorDialog::xCoordinates() const { return parseAxis(m_x); }
QVector<double> GeometryEditorDialog::yCoordinates() const { return parseAxis(m_y); }
QVector<double> GeometryEditorDialog::zCoordinates() const { return m_solid ? parseAxis(m_z) : QVector<double>{}; }

} // namespace femgui
