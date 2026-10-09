#pragma once

#include <QWidget>

#include <vtkNew.h>
#include <vtkSmartPointer.h>

class QVTKOpenGLNativeWidget;
class QComboBox;
class QToolButton;
class QCheckBox;

class vtkGenericOpenGLRenderWindow;
class vtkRenderer;
class vtkDataSetMapper;
class vtkActor;
class vtkUnstructuredGrid;
class vtkOrientationMarkerWidget;
class vtkAxesActor;

namespace femgui {

// Embeds a VTK render window in the center workspace and displays a
// demonstration mesh. Model/Mesh/Results tab switching, camera presets,
// representation mode, and axes visibility are wired; picking and scalar
// result coloring land in a later phase.
class ViewportWidget : public QWidget
{
    Q_OBJECT

public:
    enum class ViewMode { Model, Mesh, Results };

    explicit ViewportWidget(ViewMode mode, QWidget *parent = nullptr);
    ~ViewportWidget() override;

public slots:
    void fitView();
    void setCameraPreset(const QString &preset);
    void setRepresentation(const QString &representation);
    void setAxesVisible(bool visible);

private:
    void buildDemoMesh();
    void buildScene();

    ViewMode m_mode;
    QVTKOpenGLNativeWidget *m_vtkWidget = nullptr;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkUnstructuredGrid> m_demoMesh;
    vtkSmartPointer<vtkDataSetMapper> m_meshMapper;
    vtkSmartPointer<vtkActor> m_meshActor;
    vtkSmartPointer<vtkOrientationMarkerWidget> m_axesWidget;
};

} // namespace femgui
