#pragma once

#include <QMap>
#include <QString>
#include <QVector>
#include <QWidget>

#include <vtkType.h>

class QGroupBox;
class QLabel;
class QVBoxLayout;

namespace femgui {

class ProjectDocument;
struct PressurePatch;
struct ValidationIssue;
using ValidationIssues = QVector<ValidationIssue>;

// Right panel: contextual inspector . MainWindow computes the
// selection payload (it has both the mesh and the active ResultSet) and
// hands it here purely for display.
class InspectorPanel : public QWidget
{
    Q_OBJECT

public:
    struct NodeInfo
    {
        vtkIdType solverId = -1;
        int gridI = -1, gridJ = -1, gridK = -1;
        double x = 0.0, y = 0.0, z = 0.0;
        int dofsPerNode = 5;
        bool solid = false;
        bool hasResults = false;
        QString resultContext;
        QMap<QString, double> fieldValues; // label -> value, only when hasResults
        QMap<QString, QString> fieldUnits;
    };

    struct ElementInfo
    {
        vtkIdType solverId = -1;
        QVector<vtkIdType> nodeIds;
        double xmin = 0.0, xmax = 0.0, ymin = 0.0, ymax = 0.0, zmin = 0.0, zmax = 0.0;
        double width = 0.0, height = 0.0, depth = 0.0, area = 0.0, volume = 0.0, aspectRatio = 0.0;
        int dofsPerNode = 5;
        bool solid = false, onLoadedFace = false, onFoundationFace = false;
        QVector<QString> overlappingPatchNames;
        double appliedPressureResultant = 0.0; // sum(overlapArea * q); see plan 7.3
        QString resultContext;
    };

    explicit InspectorPanel(QWidget *parent = nullptr);

public slots:
    void showProjectSummary(const ProjectDocument *document, const ValidationIssues &issues);
    void showNode(const NodeInfo &info);
    void showElement(const ElementInfo &info);
    void showPatch(const PressurePatch &patch);

private:
    void clearContent();
    QGroupBox *makeReadinessGroup(const ProjectDocument *document, const ValidationIssues &issues);

    QVBoxLayout *m_layout = nullptr;
};

} // namespace femgui
