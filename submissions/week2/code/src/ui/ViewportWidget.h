#pragma once

#include <QWidget>
#include <QRectF>
#include "core/PlateDefinition.h"

#include <vtkNew.h>
#include <vtkSmartPointer.h>

class QVTKOpenGLNativeWidget;
class QComboBox;
class QToolButton;
class QCheckBox;
class QSlider;
class QLabel;

class vtkGenericOpenGLRenderWindow;
class vtkRenderer;
class vtkDataSetMapper;
class vtkActor;
class vtkUnstructuredGrid;
class vtkOrientationMarkerWidget;
class vtkAxesActor;
class vtkScalarBarActor;
class vtkSphereSource;
class vtkPolyDataMapper;
class vtkPolyData;
class vtkAppendPolyData;

namespace femgui {

class ResultSet;
struct PressurePatch;

// Embeds a VTK render window in the center workspace. One instance backs
// each of the Model/Mesh/Results tabs ; MainWindow feeds each
// instance the shared plate mesh or the active ResultSet as they change.
class ViewportWidget : public QWidget
{
    Q_OBJECT

public:
    enum class ViewMode { Model, Mesh, Results };
    enum class PickMode { None, Node, Element };

    explicit ViewportWidget(ViewMode mode, QWidget *parent = nullptr);
    ~ViewportWidget() override;

    // Model/Mesh tabs: plate quads or C3D8 bricks. loadPlaneZ is the face on
    // which pressure patches are edited and drawn.
    void setPlateMesh(vtkSmartPointer<vtkUnstructuredGrid> mesh, double loadPlaneZ = 0.0);
    void setPressurePatches(const QVector<PressurePatch> &patches, double loadPlaneZ = 0.0);

    // Results tab: the active run's dataset. Pass nullptr to clear.
    void setResultSet(const ResultSet *resultSet);
    void setActiveField(const QString &arrayName);

    void clearSelectionHighlight();
    void highlightNode(vtkIdType solverNodeId);
    void highlightElement(vtkIdType solverElementId);
    void highlightPatch(const QUuid &id);
    void setEditingEnabled(bool enabled);
    bool exportScreenshot(const QString &path, int revision, bool stale, QString *error);

public slots:
    void fitView();
    void setCameraPreset(const QString &preset);
    void setRepresentation(const QString &representation);
    void setAxesVisible(bool visible);
    void setPickMode(int index); // matches m_pickCombo item index
    void startRectangleDrawing();
    void cancelRectangleDrawing();
    void goToExtremum(int kind); // 0: min, 1: max, 2: absolute max

signals:
    void nodePicked(vtkIdType solverNodeId);
    void elementPicked(vtkIdType solverElementId);
    void selectionCleared();
    void rectangleDrafted(QRectF bounds);
    void patchPicked(QUuid id);
    void statusMessage(QString message);

private:
    friend class PickInteractorStyle;
    friend class IntegrationTests;
    void render();
    void handleClickPick(int displayX, int displayY);
    void buildScene();
    void refreshMeshActor();
    void refreshResultActor();
    void rebuildPatchActors(const QVector<PressurePatch> &patches);
    void updateDeformationControlsEnabled();
    bool projectToPlate(int x, int y, QPointF &point);
    void beginRectangle(int x, int y);
    void moveRectangle(int x, int y);
    void finishRectangle(int x, int y);
    void showRectangle(const QRectF &bounds, bool valid);

    ViewMode m_mode;
    PickMode m_pickMode = PickMode::None;

    QVTKOpenGLNativeWidget *m_vtkWidget = nullptr;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderer> m_renderer;

    // Model/Mesh geometry.
    vtkSmartPointer<vtkUnstructuredGrid> m_plateMesh;
    vtkSmartPointer<vtkDataSetMapper> m_meshMapper;
    vtkSmartPointer<vtkActor> m_meshActor;
    vtkSmartPointer<vtkAppendPolyData> m_patchAppend;
    vtkSmartPointer<vtkPolyDataMapper> m_patchMapper;
    vtkSmartPointer<vtkActor> m_patchActor;
    QVector<PressurePatch> m_patches;
    double m_loadPlaneZ = 0.0, m_overlayOffset = 0.5;
    bool m_loadFromNegativeZ = false;
    bool m_drawing = false, m_draggingRectangle = false, m_editingEnabled = true;
    QPointF m_rectangleStart;
    QRectF m_rectangleBounds;
    vtkSmartPointer<vtkPolyDataMapper> m_draftMapper;
    vtkSmartPointer<vtkActor> m_draftActor;
    QToolButton *m_drawButton = nullptr;
    QLabel *m_draftLabel = nullptr;

    // Results geometry (a display copy; the ResultSet's own grid is never mutated).
    const ResultSet *m_resultSet = nullptr;
    vtkSmartPointer<vtkUnstructuredGrid> m_resultDisplayGrid;
    vtkSmartPointer<vtkDataSetMapper> m_resultMapper;
    vtkSmartPointer<vtkActor> m_resultActor;
    vtkSmartPointer<vtkScalarBarActor> m_scalarBar;
    QString m_activeField;
    bool m_deformed = false;
    double m_deformationScale = 1.0;

    // Selection highlight.
    vtkSmartPointer<vtkSphereSource> m_highlightSphere;
    vtkSmartPointer<vtkPolyDataMapper> m_highlightMapper;
    vtkSmartPointer<vtkActor> m_highlightActor;

    vtkSmartPointer<vtkOrientationMarkerWidget> m_axesWidget;

    // Toolbar widgets referenced from update logic.
    QComboBox *m_pickCombo = nullptr;
    QComboBox *m_viewCombo = nullptr;
    QComboBox *m_fieldCombo = nullptr;
    QCheckBox *m_deformedCheck = nullptr;
    QSlider *m_deformationSlider = nullptr;
    QLabel *m_extremaLabel = nullptr;
    QLabel *m_scaleLabel = nullptr;
    vtkIdType m_selectedNode = -1;
    vtkIdType m_selectedElement = -1;
};

} // namespace femgui
