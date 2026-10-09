#pragma once

#include <QWidget>

class QGroupBox;
class QLabel;
class QVBoxLayout;

namespace femgui {

// Right panel: contextual inspector. Shows a project summary and readiness
// checklist when nothing is selected; will switch to node/element/face/
// result-field content once viewport picking is implemented.
class InspectorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit InspectorPanel(QWidget *parent = nullptr);

public slots:
    void showProjectSummary();

private:
    QGroupBox *makeReadinessGroup();

    QVBoxLayout *m_layout = nullptr;
    QLabel *m_readinessLabels[5] = {};
};

} // namespace femgui
