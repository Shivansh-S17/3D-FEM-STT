#include "ui/ViewportWidget.h"

#include <QComboBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkDataSetMapper.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNamedColors.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkPoints.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkUnstructuredGrid.h>

namespace femgui {

ViewportWidget::ViewportWidget(ViewMode mode, QWidget *parent)
    : QWidget(parent)
    , m_mode(mode)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(4, 2, 4, 2);

    auto *fitButton = new QToolButton(toolbar);
    fitButton->setText(tr("Fit View"));
    connect(fitButton, &QToolButton::clicked, this, &ViewportWidget::fitView);
    toolbarLayout->addWidget(fitButton);

    auto *viewCombo = new QComboBox(toolbar);
    viewCombo->addItems({tr("Isometric"), tr("Front"), tr("Top"), tr("Right")});
    connect(viewCombo, &QComboBox::currentTextChanged, this, &ViewportWidget::setCameraPreset);
    toolbarLayout->addWidget(viewCombo);

    auto *repCombo = new QComboBox(toolbar);
    repCombo->addItems({tr("Surface"), tr("Surface with Edges"), tr("Wireframe"), tr("Points")});
    repCombo->setCurrentText(tr("Surface with Edges"));
    connect(repCombo, &QComboBox::currentTextChanged, this, &ViewportWidget::setRepresentation);
    toolbarLayout->addWidget(repCombo);

    auto *axesCheck = new QCheckBox(tr("Axes"), toolbar);
    axesCheck->setChecked(true);
    connect(axesCheck, &QCheckBox::toggled, this, &ViewportWidget::setAxesVisible);
    toolbarLayout->addWidget(axesCheck);

    QString modeLabel;
    switch (m_mode) {
    case ViewMode::Model: modeLabel = tr("Model"); break;
    case ViewMode::Mesh: modeLabel = tr("Mesh"); break;
    case ViewMode::Results: modeLabel = tr("Results (demo data)"); break;
    }
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(new QLabel(modeLabel, toolbar));

    layout->addWidget(toolbar);

    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    layout->addWidget(m_vtkWidget, 1);

    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(m_renderWindow);

    buildDemoMesh();
    buildScene();

    setRepresentation(repCombo->currentText());
    setCameraPreset(viewCombo->currentText());
}

ViewportWidget::~ViewportWidget() = default;

void ViewportWidget::buildDemoMesh()
{
    // A small structured slab using a subset of the received C3D8 input axes.
    // It is a geometry preview, not a mesh imported from a solver result.
    constexpr int nx = 3, ny = 3, nz = 3;
    const double x[nx] = {0.0, 1750.0, 3500.0};
    const double y[ny] = {0.0, 1750.0, 3500.0};
    const double z[nz] = {0.0, 100.0, 200.0};

    vtkNew<vtkPoints> points;
    auto nodeId = [&](int i, int j, int k) {
        return (j * nz + k) * nx + i;
    };
    for (int j = 0; j < ny; ++j)
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < nx; ++i)
                points->InsertNextPoint(x[i], y[j], z[k]);

    m_demoMesh = vtkSmartPointer<vtkUnstructuredGrid>::New();
    m_demoMesh->SetPoints(points);

    for (int j = 0; j < ny - 1; ++j) {
        for (int k = 0; k < nz - 1; ++k) {
            for (int i = 0; i < nx - 1; ++i) {
                vtkIdType hex[8] = {
                    nodeId(i, j, k),         nodeId(i + 1, j, k),
                    nodeId(i + 1, j + 1, k), nodeId(i, j + 1, k),
                    nodeId(i, j, k + 1),         nodeId(i + 1, j, k + 1),
                    nodeId(i + 1, j + 1, k + 1), nodeId(i, j + 1, k + 1),
                };
                m_demoMesh->InsertNextCell(VTK_HEXAHEDRON, 8, hex);
            }
        }
    }
}

void ViewportWidget::buildScene()
{
    vtkNew<vtkNamedColors> colors;

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(colors->GetColor3d("SlateGray").GetData());
    m_renderer->SetBackground2(colors->GetColor3d("LightSteelBlue").GetData());
    m_renderer->GradientBackgroundOn();
    m_renderWindow->AddRenderer(m_renderer);

    m_meshMapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_meshMapper->SetInputData(m_demoMesh);

    m_meshActor = vtkSmartPointer<vtkActor>::New();
    m_meshActor->SetMapper(m_meshMapper);
    m_meshActor->GetProperty()->SetColor(colors->GetColor3d("SteelBlue").GetData());
    m_meshActor->GetProperty()->SetEdgeColor(colors->GetColor3d("Black").GetData());
    m_renderer->AddActor(m_meshActor);

    vtkNew<vtkAxesActor> axes;
    m_axesWidget = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    m_axesWidget->SetOrientationMarker(axes);
    m_axesWidget->SetInteractor(m_renderWindow->GetInteractor());
    m_axesWidget->SetViewport(0.0, 0.0, 0.18, 0.18);
    m_axesWidget->SetEnabled(1);
    m_axesWidget->InteractiveOff();
}

void ViewportWidget::fitView()
{
    if (!m_renderer)
        return;
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void ViewportWidget::setCameraPreset(const QString &preset)
{
    if (!m_renderer)
        return;
    vtkCamera *camera = m_renderer->GetActiveCamera();
    camera->SetFocalPoint(0, 0, 0);

    if (preset == tr("Front")) {
        camera->SetPosition(0, -1, 0);
        camera->SetViewUp(0, 0, 1);
    } else if (preset == tr("Top")) {
        camera->SetPosition(0, 0, 1);
        camera->SetViewUp(0, 1, 0);
    } else if (preset == tr("Right")) {
        camera->SetPosition(1, 0, 0);
        camera->SetViewUp(0, 0, 1);
    } else {
        camera->SetPosition(1, -1, 1);
        camera->SetViewUp(0, 0, 1);
    }
    fitView();
}

void ViewportWidget::setRepresentation(const QString &representation)
{
    if (!m_meshActor)
        return;
    vtkProperty *prop = m_meshActor->GetProperty();
    if (representation == tr("Wireframe")) {
        prop->SetRepresentationToWireframe();
        prop->EdgeVisibilityOff();
    } else if (representation == tr("Points")) {
        prop->SetRepresentationToPoints();
        prop->EdgeVisibilityOff();
        prop->SetPointSize(4.0f);
    } else if (representation == tr("Surface with Edges")) {
        prop->SetRepresentationToSurface();
        prop->EdgeVisibilityOn();
    } else {
        prop->SetRepresentationToSurface();
        prop->EdgeVisibilityOff();
    }
    if (m_renderWindow)
        m_renderWindow->Render();
}

void ViewportWidget::setAxesVisible(bool visible)
{
    if (m_axesWidget)
        m_axesWidget->SetEnabled(visible ? 1 : 0);
    if (m_renderWindow)
        m_renderWindow->Render();
}

} // namespace femgui
