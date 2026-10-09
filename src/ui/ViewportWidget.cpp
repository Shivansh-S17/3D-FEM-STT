#include "ui/ViewportWidget.h"

#include "core/PlateDefinition.h"
#include "core/ResultSet.h"
#include "ui/UiStyle.h"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <QShortcut>
#include <QPainter>
#include <QSaveFile>
#include <QSignalBlocker>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkAppendPolyData.h>
#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellPicker.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkDoubleArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkIdTypeArray.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkLookupTable.h>
#include <vtkObjectFactory.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkPointData.h>
#include <vtkPointPicker.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkScalarBarActor.h>
#include <vtkTextProperty.h>
#include <vtkSphereSource.h>
#include <vtkUnstructuredGrid.h>
#include <vtkWarpVector.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace femgui {

namespace {
constexpr int kClickDragThresholdPixels = 4;
}

// Forwards a plain (small-movement) left click to ViewportWidget::handleClickPick
// while still letting the base class drive normal camera interaction for drags
// (section 7.1: dragging beyond a small threshold navigates, it does not select).
class PickInteractorStyle : public vtkInteractorStyleTrackballCamera
{
public:
    static PickInteractorStyle *New();
    vtkTypeMacro(PickInteractorStyle, vtkInteractorStyleTrackballCamera);

    void SetOwner(ViewportWidget *owner) { m_owner = owner; }

    void OnLeftButtonDown() override
    {
        int *pos = GetInteractor()->GetEventPosition();
        m_pressX = pos[0];
        m_pressY = pos[1];
        if (m_owner && m_owner->m_drawing) { m_owner->beginRectangle(pos[0], pos[1]); return; }
        Superclass::OnLeftButtonDown();
    }

    void OnLeftButtonUp() override
    {
        int *pos = GetInteractor()->GetEventPosition();
        if (m_owner && m_owner->m_drawing) { m_owner->finishRectangle(pos[0], pos[1]); return; }
        Superclass::OnLeftButtonUp();
        int dx = pos[0] - m_pressX;
        int dy = pos[1] - m_pressY;
        if (m_owner && (dx * dx + dy * dy) <= kClickDragThresholdPixels * kClickDragThresholdPixels)
            m_owner->handleClickPick(pos[0], pos[1]);
    }

    void OnMouseMove() override
    {
        if (m_owner && m_owner->m_drawing) {
            int *pos = GetInteractor()->GetEventPosition();
            m_owner->moveRectangle(pos[0], pos[1]);
            return;
        }
        Superclass::OnMouseMove();
        if (m_owner && this->GetState() == 0) {
            int *pos = GetInteractor()->GetEventPosition();
            m_owner->handleHover(pos[0], pos[1]);
        }
    }

    void OnChar() override
    {
        // VTK's default 'w'/'s' shortcuts change representation behind Qt's controls.
        if (GetInteractor()->GetKeyCode() == 27 && m_owner) {
            m_owner->cancelRectangleDrawing();
            emit m_owner->selectionCleared();
        }
    }

private:
    ViewportWidget *m_owner = nullptr;
    int m_pressX = 0;
    int m_pressY = 0;
};

vtkStandardNewMacro(PickInteractorStyle);

ViewportWidget::ViewportWidget(ViewMode mode, QWidget *parent)
    : QWidget(parent)
    , m_mode(mode)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("viewportHud"));
    auto *toolbarRows = new QVBoxLayout(toolbar);
    toolbarRows->setContentsMargins(0, 0, 0, 0);
    toolbarRows->setSpacing(0);
    auto *toolbarLayout = new QHBoxLayout;
    auto *toolbarOptions = new QHBoxLayout;
    toolbarRows->addLayout(toolbarLayout);
    toolbarRows->addLayout(toolbarOptions);
    toolbarOptions->setContentsMargins(6, 2, 6, 4);
    toolbarLayout->setContentsMargins(6, 4, 6, 4);

    auto *fitButton = new QToolButton(toolbar);
    fitButton->setObjectName(QStringLiteral("fitButton"));
    fitButton->setText(tr("Fit View"));
    fitButton->setToolTip(tr("Fit the complete model in the viewport (F)"));
    connect(fitButton, &QToolButton::clicked, this, &ViewportWidget::fitView);
    toolbarLayout->addWidget(fitButton);

    auto *cameraSegmented = new QWidget(toolbar);
    cameraSegmented->setObjectName(QStringLiteral("cameraSegmentedControl"));
    auto *cameraLayout = new QHBoxLayout(cameraSegmented);
    cameraLayout->setContentsMargins(2, 2, 2, 2);
    cameraLayout->setSpacing(2);

    auto *cameraGroup = new QButtonGroup(this);
    cameraGroup->setExclusive(true);

    for (const auto &p : {std::make_pair(tr("Iso"), tr("Isometric")),
                          std::make_pair(tr("Top"), tr("Top")),
                          std::make_pair(tr("Front"), tr("Front")),
                          std::make_pair(tr("Right"), tr("Right"))}) {
        auto *btn = new QToolButton(cameraSegmented);
        btn->setText(p.first);
        btn->setCheckable(true);
        btn->setToolTip(tr("Switch camera to %1 view").arg(p.second));
        if (p.second == tr("Isometric")) {
            btn->setChecked(true);
        }
        cameraGroup->addButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, target = p.second] { setCameraPreset(target); });
        cameraLayout->addWidget(btn);
    }
    toolbarLayout->addWidget(cameraSegmented);

    auto *viewCombo = new QComboBox(toolbar);
    m_viewCombo = viewCombo;
    viewCombo->addItems({tr("Isometric"), tr("Front"), tr("Top"), tr("Right")});
    viewCombo->setToolTip(tr("Camera direction"));
    connect(viewCombo, &QComboBox::currentTextChanged, this, &ViewportWidget::setCameraPreset);
    viewCombo->setVisible(false);

    auto *repCombo = new QComboBox(toolbar);
    repCombo->addItems({tr("Surface"), tr("Surface with Edges"), tr("Wireframe"), tr("Points")});
    repCombo->setCurrentText(tr("Surface with Edges"));
    repCombo->setToolTip(tr("Model display style"));
    connect(repCombo, &QComboBox::currentTextChanged, this, &ViewportWidget::setRepresentation);
    toolbarOptions->addWidget(repCombo);

    auto *axesCheck = new QCheckBox(tr("Axes"), toolbar);
    axesCheck->setChecked(true);
    axesCheck->setToolTip(tr("Show or hide the orientation axes"));
    connect(axesCheck, &QCheckBox::toggled, this, &ViewportWidget::setAxesVisible);
    toolbarOptions->addWidget(axesCheck);

    if (m_mode == ViewMode::Mesh || m_mode == ViewMode::Results) {
        m_pickCombo = new QComboBox(toolbar);
        m_pickCombo->addItem(tr("Navigate"));
        m_pickCombo->addItem(tr("Select Node"));
        m_pickCombo->addItem(tr("Select Element"));
        m_pickCombo->setToolTip(tr("Choose whether dragging navigates or clicking selects model entities"));
        connect(m_pickCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ViewportWidget::setPickMode);
        toolbarOptions->addWidget(m_pickCombo);
    }

    QWidget *resultControls = nullptr;
    if (m_mode == ViewMode::Results) {
        resultControls = new QWidget(this);
        resultControls->setObjectName(QStringLiteral("viewportHudSub"));
        auto *resultRows = new QVBoxLayout(resultControls);
        resultRows->setContentsMargins(0, 0, 0, 0);
        auto *resultLayout = new QHBoxLayout;
        auto *deformationLayout = new QHBoxLayout;
        resultRows->addLayout(resultLayout);
        resultRows->addLayout(deformationLayout);
        deformationLayout->setContentsMargins(6, 2, 6, 4);
        resultLayout->setContentsMargins(6, 4, 6, 4);
        resultLayout->addWidget(new QLabel(tr("Result:"), resultControls));
        m_fieldCombo = new QComboBox(resultControls);
        m_fieldCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_fieldCombo->setMinimumContentsLength(18);
        m_fieldCombo->setToolTip(tr("Scalar result quantity displayed by the color legend"));
        connect(m_fieldCombo, &QComboBox::currentTextChanged, this, [this](const QString &) {
            if (m_fieldCombo->currentIndex() >= 0)
                setActiveField(m_fieldCombo->currentData().toString());
        });
        resultLayout->addWidget(m_fieldCombo, 1);

        resultLayout->addWidget(new QLabel(tr("Section:"), resultControls));
        m_sectionCombo = new QComboBox(resultControls);
        m_sectionCombo->setMinimumContentsLength(12);
        m_sectionCombo->setToolTip(tr("Display the full solid or a grid-aligned Z layer"));
        connect(m_sectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ViewportWidget::setSectionIndex);
        resultLayout->addWidget(m_sectionCombo);

        m_deformedCheck = new QCheckBox(tr("Deformed"), resultControls);
        m_deformedCheck->setToolTip(tr("Warp the result geometry using the displacement vector"));
        connect(m_deformedCheck, &QCheckBox::toggled, this, [this](bool checked) {
            m_deformed = checked;
            updateDeformationControlsEnabled();
            refreshResultActor();
        });
        deformationLayout->addWidget(m_deformedCheck);

        deformationLayout->addWidget(new QLabel(tr("Scale:"), resultControls));
        m_deformationSlider = new QSlider(Qt::Horizontal, resultControls);
        m_deformationSlider->setRange(1, 500); // 0.1x .. 50x, see mapping below
        m_deformationSlider->setValue(10);
        m_deformationSlider->setMaximumWidth(90);
        m_deformationSlider->setToolTip(tr("Deformation magnification from 0.1x to 50x"));
        connect(m_deformationSlider, &QSlider::valueChanged, this, [this](int value) {
            m_deformationScale = value / 10.0;
            m_scaleLabel->setText(QString::number(m_deformationScale, 'g', 4) + "x");
            refreshResultActor();
        });
        deformationLayout->addWidget(m_deformationSlider);
        m_scaleLabel = new QLabel("1x", resultControls);
        deformationLayout->addWidget(m_scaleLabel);
        updateDeformationControlsEnabled();

        deformationLayout->addWidget(new QLabel(tr("Extrema:"), resultControls));
        for (int kind = 0; kind < 3; ++kind) {
            auto *button = new QToolButton(resultControls);
            button->setText(QStringList{tr("Min"), tr("Max"), tr("|Max|")}[kind]);
            button->setToolTip(QStringList{tr("Select the minimum value in the visible result section"),
                                            tr("Select the maximum value in the visible result section"),
                                            tr("Select the largest absolute value in the visible result section")}[kind]);
            connect(button, &QToolButton::clicked, this, [this, kind] { goToExtremum(kind); });
            deformationLayout->addWidget(button);
        }

        m_extremaLabel = new QLabel(this);
        m_extremaLabel->setContentsMargins(4, 2, 4, 2);
        m_extremaLabel->setWordWrap(true);
        m_sectionLabel = new QLabel(this);
        m_sectionLabel->setContentsMargins(4, 2, 4, 2);
        m_sectionLabel->setWordWrap(true);
    }

    QString modeLabel;
    switch (m_mode) {
    case ViewMode::Model: modeLabel = tr("Model"); break;
    case ViewMode::Mesh: modeLabel = tr("Mesh"); break;
    case ViewMode::Results: modeLabel = tr("Results"); break;
    }
    toolbarOptions->addStretch();
    toolbarOptions->addWidget(new QLabel(modeLabel, toolbar));

    layout->addWidget(toolbar);
    if (resultControls) {
        layout->addWidget(resultControls);
        auto *infoRow = new QHBoxLayout;
        infoRow->setContentsMargins(4, 1, 4, 1);
        infoRow->addWidget(m_extremaLabel, 1);
        infoRow->addWidget(m_sectionLabel);
        layout->addLayout(infoRow);
    }
    if (m_mode == ViewMode::Model) {
        m_draftLabel = new QLabel(this);
        m_draftLabel->setWordWrap(true);
        m_draftLabel->hide();
        layout->addWidget(m_draftLabel);
    }
    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setProperty("role", "card");
    m_emptyLabel->setContentsMargins(12, 10, 12, 10);
    m_emptyLabel->setText(m_mode == ViewMode::Results
        ? tr("No results are loaded. Validate the model and run the analysis to inspect fields here.")
        : tr("No valid mesh is available. Review the model inputs and validation messages."));
    m_emptyLabel->setVisible(m_mode == ViewMode::Results);
    layout->addWidget(m_emptyLabel);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { cancelRectangleDrawing(); emit selectionCleared(); });

    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    m_vtkWidget->setAccessibleName(m_mode == ViewMode::Model ? tr("Model viewport")
                                   : m_mode == ViewMode::Mesh ? tr("Mesh viewport")
                                                              : tr("Results viewport"));
    m_vtkWidget->setAccessibleDescription(tr("Interactive 3D view. Drag to rotate or pan and use the toolbar to change interaction mode."));
    layout->addWidget(m_vtkWidget, 1);

    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(m_renderWindow);

    buildScene();
    m_draftMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_draftActor = vtkSmartPointer<vtkActor>::New();
    m_draftActor->SetMapper(m_draftMapper);
    m_draftActor->GetProperty()->SetRepresentationToWireframe();
    m_draftActor->GetProperty()->SetLineWidth(3);
    m_draftActor->PickableOff();
    m_draftActor->VisibilityOff();
    m_draftActor->UseBoundsOff();
    m_overlayRenderer->AddActor(m_draftActor);

    vtkNew<PickInteractorStyle> style;
    style->SetOwner(this);
    m_renderWindow->GetInteractor()->SetInteractorStyle(style);

    setRepresentation(repCombo->currentText());
    setCameraPreset(viewCombo->currentText());
}

ViewportWidget::~ViewportWidget() = default;

void ViewportWidget::render()
{
    // Hidden tabs and test widgets do not need an OpenGL context or a render.
    if (m_vtkWidget && m_vtkWidget->isVisible() && m_renderWindow) m_renderWindow->Render();
}

void ViewportWidget::buildScene()
{
    const QPalette palette = QApplication::palette();
    auto vtkColor = [](const QColor &color) {
        return std::array<double, 3>{color.redF(), color.greenF(), color.blueF()};
    };
    const auto background = vtkColor(UiStyle::viewportBackground(palette));
    const auto backgroundAccent = vtkColor(UiStyle::viewportBackgroundAccent(palette));
    const auto mesh = vtkColor(UiStyle::meshColor(palette));
    const auto edge = vtkColor(UiStyle::edgeColor(palette));
    const auto load = vtkColor(UiStyle::loadColor(palette));
    const auto selection = vtkColor(UiStyle::selectionColor(palette));

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetLayer(0);
    m_renderer->SetBackground(background.data());
    m_renderer->SetBackground2(backgroundAccent.data());
    m_renderer->GradientBackgroundOn();
    m_renderWindow->AddRenderer(m_renderer);
    m_renderWindow->SetNumberOfLayers(2);

    // Pressure loads are interaction overlays. This renderer shares the model
    // camera but keeps its own depth buffer, preventing patches from flickering
    // or vanishing while the model is rotated and zoomed.
    m_overlayRenderer = vtkSmartPointer<vtkRenderer>::New();
    m_overlayRenderer->SetLayer(1);
    m_overlayRenderer->SetActiveCamera(m_renderer->GetActiveCamera());
    m_overlayRenderer->InteractiveOff();
    m_overlayRenderer->PreserveDepthBufferOff();
    m_renderWindow->AddRenderer(m_overlayRenderer);

    m_meshMapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_meshActor = vtkSmartPointer<vtkActor>::New();
    m_meshActor->SetMapper(m_meshMapper);
    m_meshActor->GetProperty()->SetColor(mesh[0], mesh[1], mesh[2]);
    m_meshActor->GetProperty()->SetEdgeColor(edge[0], edge[1], edge[2]);
    m_meshActor->VisibilityOff();
    m_renderer->AddActor(m_meshActor);

    m_patchAppend = vtkSmartPointer<vtkAppendPolyData>::New();
    m_patchMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_patchActor = vtkSmartPointer<vtkActor>::New();
    m_patchActor->SetMapper(m_patchMapper);
    m_patchActor->GetProperty()->SetColor(load[0], load[1], load[2]);
    m_patchActor->GetProperty()->SetOpacity(0.55);
    m_patchActor->GetProperty()->LightingOff();
    m_patchActor->PickableOff();
    m_patchActor->VisibilityOff();
    m_patchActor->UseBoundsOff();
    m_renderer->AddActor(m_patchActor);

    // The filled patch obeys normal model occlusion. A foreground outline
    // remains visible as an interaction cue even when the loaded face turns
    // away from the camera.
    m_patchOutlineActor = vtkSmartPointer<vtkActor>::New();
    m_patchOutlineActor->SetMapper(m_patchMapper);
    m_patchOutlineActor->GetProperty()->SetColor(load[0], load[1], load[2]);
    m_patchOutlineActor->GetProperty()->SetRepresentationToWireframe();
    m_patchOutlineActor->GetProperty()->SetLineWidth(3.0);
    m_patchOutlineActor->GetProperty()->LightingOff();
    m_patchOutlineActor->PickableOff();
    m_patchOutlineActor->VisibilityOff();
    m_patchOutlineActor->UseBoundsOff();
    m_overlayRenderer->AddActor(m_patchOutlineActor);

    m_resultMapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_resultActor = vtkSmartPointer<vtkActor>::New();
    m_resultActor->SetMapper(m_resultMapper);
    m_resultActor->VisibilityOff();
    m_renderer->AddActor(m_resultActor);

    m_scalarBar = vtkSmartPointer<vtkScalarBarActor>::New();
    m_scalarBar->SetNumberOfLabels(5);
    m_scalarBar->SetPosition(0.80, 0.12);
    m_scalarBar->SetWidth(0.18);
    m_scalarBar->SetHeight(0.72);
    m_scalarBar->SetTitleRatio(0.18);
    m_scalarBar->SetVerticalTitleSeparation(16);
    m_scalarBar->GetTitleTextProperty()->SetFontSize(12);
    m_scalarBar->GetTitleTextProperty()->BoldOff();
    m_scalarBar->GetTitleTextProperty()->ItalicOff();
    m_scalarBar->GetLabelTextProperty()->SetFontSize(11);
    m_scalarBar->GetLabelTextProperty()->BoldOff();
    m_scalarBar->GetLabelTextProperty()->ItalicOff();
    m_scalarBar->VisibilityOff();
    m_renderer->AddViewProp(m_scalarBar);

    m_highlightSphere = vtkSmartPointer<vtkSphereSource>::New();
    m_highlightSphere->SetRadius(1.0); // sized relative to mesh extent in highlightNode()
    m_highlightMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_highlightMapper->SetInputConnection(m_highlightSphere->GetOutputPort());
    m_highlightActor = vtkSmartPointer<vtkActor>::New();
    m_highlightActor->SetMapper(m_highlightMapper);
    m_highlightActor->GetProperty()->SetColor(selection[0], selection[1], selection[2]);
    m_highlightActor->PickableOff();
    m_highlightActor->VisibilityOff();
    m_renderer->AddActor(m_highlightActor);

    vtkNew<vtkAxesActor> axes;
    m_axesWidget = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    m_axesWidget->SetOrientationMarker(axes);
    m_axesWidget->SetInteractor(m_renderWindow->GetInteractor());
    m_axesWidget->SetViewport(0.0, 0.0, 0.18, 0.18);
    m_axesWidget->SetEnabled(1);
    m_axesWidget->InteractiveOff();
}

void ViewportWidget::setPlateMesh(vtkSmartPointer<vtkUnstructuredGrid> mesh, double loadPlaneZ)
{
    if (m_mode == ViewMode::Results)
        return;
    cancelRectangleDrawing();
    m_plateMesh = mesh;
    m_loadPlaneZ = loadPlaneZ;
    if (mesh && mesh->GetNumberOfPoints() > 0) {
        double bounds[6]; mesh->GetBounds(bounds);
        const double diagonal = std::hypot(std::hypot(bounds[1] - bounds[0], bounds[3] - bounds[2]), bounds[5] - bounds[4]);
        // A camera-distance-independent physical separation avoids depth-buffer
        // flicker between the mesh face and its translucent load overlay.
        m_overlayOffset = std::max(1e-9, diagonal * 5e-4);
        m_loadFromNegativeZ = bounds[5] > bounds[4] && std::abs(loadPlaneZ - bounds[4]) <= m_overlayOffset;
    }
    refreshMeshActor();
    if (m_viewCombo && m_viewCombo->currentText() == tr("Isometric"))
        setCameraPreset(tr("Isometric"));
    else
        fitView();
}

void ViewportWidget::refreshMeshActor()
{
    bool hasMesh = m_plateMesh && m_plateMesh->GetNumberOfCells() > 0;
    if (m_emptyLabel) m_emptyLabel->setVisible(!hasMesh);
    m_meshMapper->SetInputData(hasMesh ? m_plateMesh : nullptr);
    m_meshActor->SetVisibility(hasMesh);
    if (m_renderWindow)
        render();
}

void ViewportWidget::setPressurePatches(const QVector<PressurePatch> &patches, double loadPlaneZ)
{
    if (m_mode != ViewMode::Model)
        return;
    m_loadPlaneZ = loadPlaneZ;
    m_patches = patches;
    rebuildPatchActors(patches);
}

void ViewportWidget::rebuildPatchActors(const QVector<PressurePatch> &patches)
{
    m_patchAppend = vtkSmartPointer<vtkAppendPolyData>::New();
    int visibleCount = 0;

    for (const PressurePatch &patch : patches) {
        if (!patch.visible)
            continue;
        if (!std::isfinite(patch.x1) || !std::isfinite(patch.x2) || !std::isfinite(patch.y1) ||
            !std::isfinite(patch.y2) || patch.x1 >= patch.x2 || patch.y1 >= patch.y2) continue;
        vtkNew<vtkPoints> points;
        points->SetDataTypeToDouble();
        const double z = m_loadPlaneZ + (m_loadFromNegativeZ ? -m_overlayOffset : m_overlayOffset);
        points->InsertNextPoint(patch.x1, patch.y1, z);
        points->InsertNextPoint(patch.x2, patch.y1, z);
        points->InsertNextPoint(patch.x2, patch.y2, z);
        points->InsertNextPoint(patch.x1, patch.y2, z);

        vtkNew<vtkPolyData> quad;
        quad->SetPoints(points);
        vtkNew<vtkCellArray> cells;
        vtkIdType ids[4] = {0, 1, 2, 3};
        cells->InsertNextCell(4, ids);
        quad->SetPolys(cells);

        m_patchAppend->AddInputData(quad);
        ++visibleCount;
    }

    if (visibleCount > 0) {
        m_patchAppend->Update();
        m_patchMapper->SetInputConnection(m_patchAppend->GetOutputPort());
    } else {
        m_patchMapper->SetInputData(nullptr);
    }
    m_patchActor->SetVisibility(visibleCount > 0);
    m_patchOutlineActor->SetVisibility(visibleCount > 0);
    if (m_renderWindow)
        render();
}

void ViewportWidget::setResultSet(const ResultSet *resultSet)
{
    if (m_mode != ViewMode::Results)
        return;
    m_resultSet = resultSet;
    clearSelectionHighlight();

    if (!m_fieldCombo)
        return;
    m_fieldCombo->blockSignals(true);
    m_fieldCombo->clear();
    if (m_sectionCombo) {
        m_sectionCombo->blockSignals(true);
        m_sectionCombo->clear();
    }
    m_sectionZLevels.clear();
    m_sectionLayerIndex = -1;
    m_sectionSurface = nullptr;

    if (!resultSet || !resultSet->isValid()) {
        if (m_emptyLabel) m_emptyLabel->show();
        m_resultDisplayGrid = nullptr;
        m_resultActor->SetVisibility(false);
        m_scalarBar->VisibilityOff();
        m_resultMapper->SetInputData(nullptr);
        m_activeField.clear();
        updateDeformationControlsEnabled();
        if (m_extremaLabel)
            m_extremaLabel->clear();
        if (m_sectionLabel) m_sectionLabel->clear();
        if (m_sectionCombo) { m_sectionCombo->setVisible(false); m_sectionCombo->blockSignals(false); }
        m_fieldCombo->blockSignals(false);
        if (m_renderWindow)
            render();
        return;
    }

    if (m_emptyLabel) m_emptyLabel->hide();

    m_resultDisplayGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    m_resultDisplayGrid->ShallowCopy(resultSet->grid());

    QVector<double> magnitude = resultSet->computeDisplacementMagnitude();
    if (!magnitude.isEmpty()) {
        vtkNew<vtkDoubleArray> magArray;
        magArray->SetName("__displacement_magnitude");
        magArray->SetNumberOfComponents(1);
        magArray->SetNumberOfTuples(magnitude.size());
        for (int i = 0; i < magnitude.size(); ++i)
            magArray->SetTypedComponent(i, 0, magnitude[i]);
        m_resultDisplayGrid->GetPointData()->AddArray(magArray);
    }

    if (m_sectionCombo) {
        const bool solid = resultSet->modelKind() == ModelKind::SolidC3D8;
        m_sectionCombo->setVisible(solid);
        if (solid) {
            for (vtkIdType pointId = 0; pointId < m_resultDisplayGrid->GetNumberOfPoints(); ++pointId) {
                double point[3]; m_resultDisplayGrid->GetPoint(pointId, point);
                m_sectionZLevels.append(point[2]);
            }
            std::sort(m_sectionZLevels.begin(), m_sectionZLevels.end());
            m_sectionZLevels.erase(std::unique(m_sectionZLevels.begin(), m_sectionZLevels.end(), [](double a, double b) {
                return std::abs(a - b) <= 1e-10 * std::max({1.0, std::abs(a), std::abs(b)});
            }), m_sectionZLevels.end());
            m_sectionCombo->addItem(tr("Full volume"));
            for (int layer = 0; layer < m_sectionZLevels.size(); ++layer) {
                QString suffix = layer == 0 ? tr(" (loaded face)")
                    : layer == m_sectionZLevels.size() - 1 ? tr(" (foundation face)") : tr(" (interior)");
                m_sectionCombo->addItem(tr("Z = %1 mm%2").arg(m_sectionZLevels[layer], 0, 'g', 8).arg(suffix));
            }
            m_sectionCombo->setCurrentIndex(0);
        }
        m_sectionCombo->blockSignals(false);
    }
    if (m_sectionLabel) m_sectionLabel->setText(sectionContext());

    int defaultIndex = -1;
    int i = 0;
    for (const FieldDescriptor &field : resultSet->availableFields()) {
        if (field.componentCount != 1)
            continue; // the raw 3-component "displacement" array isn't directly colorable; use magnitude instead
        m_fieldCombo->addItem(field.label, field.arrayName);
        if (field.arrayName == "w")
            defaultIndex = i;
        ++i;
    }
    if (defaultIndex < 0 && m_fieldCombo->count() > 0)
        defaultIndex = 0;
    m_fieldCombo->blockSignals(false);

    m_resultMapper->SetInputData(m_resultDisplayGrid);
    m_resultActor->SetVisibility(true);
    updateDeformationControlsEnabled();

    if (defaultIndex >= 0) {
        m_fieldCombo->setCurrentIndex(defaultIndex);
        setActiveField(m_fieldCombo->itemData(defaultIndex).toString());
    }
    fitView();
}

QString ViewportWidget::sectionContext() const
{
    if (!m_resultSet || m_resultSet->modelKind() != ModelKind::SolidC3D8 || m_sectionLayerIndex < 0)
        return tr("Full model volume in the solver source frame.");
    if (m_sectionLayerIndex >= m_sectionZLevels.size()) return {};
    const QString face = m_sectionLayerIndex == 0 ? tr("loaded face")
        : m_sectionLayerIndex == m_sectionZLevels.size() - 1 ? tr("foundation face") : tr("interior layer");
    const QString owner = m_sectionLayerIndex == m_sectionZLevels.size() - 1
        ? tr("source cells below the plane") : tr("source cells above the plane");
    return tr("Grid-aligned Z section at %1 mm (%2). Vertices are original solver nodes; no interpolation is used. "
              "Section element IDs identify %3.")
        .arg(m_sectionZLevels[m_sectionLayerIndex], 0, 'g', 10).arg(face, owner);
}

void ViewportWidget::rebuildSectionSurface()
{
    m_sectionSurface = nullptr;
    if (!m_resultDisplayGrid || m_sectionLayerIndex < 0 || m_sectionLayerIndex >= m_sectionZLevels.size()) return;
    const double sectionZ = m_sectionZLevels[m_sectionLayerIndex];
    double bounds[6]; m_resultDisplayGrid->GetBounds(bounds);
    const double tolerance = 1e-10 * std::max(1.0, bounds[5] - bounds[4]);
    const bool topLayer = m_sectionLayerIndex == m_sectionZLevels.size() - 1;

    vtkNew<vtkCellArray> faces;
    vtkNew<vtkIdTypeArray> solverIds;
    solverIds->SetName("solver_element_id");
    vtkNew<vtkIdTypeArray> sourceIds;
    sourceIds->SetName("section_source_element_id");
    auto *existingIds = m_resultDisplayGrid->GetCellData()->GetArray("solver_element_id");
    for (vtkIdType cellId = 0; cellId < m_resultDisplayGrid->GetNumberOfCells(); ++cellId) {
        auto *cell = m_resultDisplayGrid->GetCell(cellId);
        double cellBounds[6]; cell->GetBounds(cellBounds);
        const double faceZ = topLayer ? cellBounds[5] : cellBounds[4];
        if (std::abs(faceZ - sectionZ) > tolerance) continue;
        vtkIdType faceIds[4];
        int count = 0;
        for (vtkIdType local = 0; local < cell->GetNumberOfPoints(); ++local) {
            const auto pointId = cell->GetPointId(local);
            double point[3]; m_resultDisplayGrid->GetPoint(pointId, point);
            if (std::abs(point[2] - sectionZ) <= tolerance && count < 4) faceIds[count++] = pointId;
        }
        if (count != 4) continue;
        faces->InsertNextCell(4, faceIds);
        const vtkIdType solverId = existingIds ? static_cast<vtkIdType>(existingIds->GetTuple1(cellId)) : cellId;
        solverIds->InsertNextValue(solverId);
        sourceIds->InsertNextValue(solverId);
    }

    m_sectionSurface = vtkSmartPointer<vtkPolyData>::New();
    m_sectionSurface->SetPoints(m_resultDisplayGrid->GetPoints());
    m_sectionSurface->SetPolys(faces);
    m_sectionSurface->GetPointData()->ShallowCopy(m_resultDisplayGrid->GetPointData());
    m_sectionSurface->GetCellData()->AddArray(solverIds);
    m_sectionSurface->GetCellData()->AddArray(sourceIds);
}

void ViewportWidget::setSectionIndex(int index)
{
    if (!m_resultSet || m_resultSet->modelKind() != ModelKind::SolidC3D8) return;
    const int layer = index - 1;
    if (layer < -1 || layer >= m_sectionZLevels.size()) return;
    if (m_sectionCombo && m_sectionCombo->currentIndex() != index) {
        const QSignalBlocker blocker(m_sectionCombo);
        m_sectionCombo->setCurrentIndex(index);
    }
    m_sectionLayerIndex = layer;
    rebuildSectionSurface();
    clearSelectionHighlight();
    if (m_sectionLabel) m_sectionLabel->setText(sectionContext());
    refreshResultActor();
    fitView();
}

void ViewportWidget::updateDeformationControlsEnabled()
{
    if (!m_deformedCheck)
        return;
    bool hasDisplacement = m_resultDisplayGrid && m_resultDisplayGrid->GetPointData()->GetArray("displacement");
    m_deformedCheck->setEnabled(hasDisplacement);
    if (m_deformationSlider)
        m_deformationSlider->setEnabled(hasDisplacement && m_deformed);
}

void ViewportWidget::setActiveField(const QString &arrayName)
{
    m_activeField = arrayName;
    if (!m_resultSet || !m_resultDisplayGrid)
        return;

    QString effectiveArray = arrayName;
    double signedMin = 0.0, signedMax = 0.0, maxAbs = 0.0;
    vtkIdType maxAbsId = -1;
    bool haveExtrema = false;

    if (arrayName == "__displacement_magnitude") {
        vtkDataArray *array = m_resultDisplayGrid->GetPointData()->GetArray("__displacement_magnitude");
        if (array) {
            vtkIdType n = array->GetNumberOfTuples();
            signedMin = signedMax = maxAbs = n > 0 ? array->GetTuple1(0) : 0.0;
            maxAbsId = n > 0 ? 0 : -1;
            for (vtkIdType i = 1; i < n; ++i) {
                double v = array->GetTuple1(i);
                signedMin = std::min(signedMin, v);
                signedMax = std::max(signedMax, v);
                if (v > maxAbs) { maxAbs = v; maxAbsId = i; }
            }
            haveExtrema = true;
        }
    } else {
        ResultSet::Extrema extrema = m_resultSet->computeExtrema(arrayName);
        if (extrema.valid) {
            signedMin = extrema.signedMin;
            signedMax = extrema.signedMax;
            maxAbs = extrema.maxAbsolute;
            maxAbsId = extrema.maxAbsoluteNodeId;
            haveExtrema = true;
        }
    }

    m_resultDisplayGrid->GetPointData()->SetActiveScalars(effectiveArray.toUtf8().constData());
    m_resultMapper->SetScalarModeToUsePointFieldData();
    m_resultMapper->SelectColorArray(effectiveArray.toUtf8().constData());

    vtkNew<vtkLookupTable> lut;
    // Blue-white-red diverging colors, rather than a hue sweep through green.
    lut->SetNumberOfTableValues(256);
    lut->Build();
    for (int i = 0; i < 256; ++i) {
        const double t = i / 255.0;
        lut->SetTableValue(i, t < 0.5 ? 2*t : 1.0, t < 0.5 ? 2*t : 2*(1-t), t < 0.5 ? 1.0 : 2*(1-t), 1.0);
    }
    m_resultMapper->SetLookupTable(lut);

    double range[2] = {signedMin, signedMax};
    if (range[0] == range[1]) {
        // Zero/constant-field guard: avoid a zero-width legend range.
        range[0] -= 1.0;
        range[1] += 1.0;
    }
    m_resultMapper->SetScalarRange(range);
    lut->SetTableRange(range); // the scalar bar must display the same range as the mapper

    QString label = arrayName;
    QString unit;
    for (const FieldDescriptor &field : m_resultSet->availableFields()) {
        if (field.arrayName == arrayName) {
            label = field.label;
            unit = field.unit;
            break;
        }
    }
    m_scalarBar->SetLookupTable(lut);
    m_scalarBar->SetTitle((label + (unit.isEmpty() ? QString() : " [" + unit + "]")).toUtf8().constData());
    m_scalarBar->VisibilityOn();

    if (m_extremaLabel && haveExtrema) {
        m_extremaLabel->setText(tr("min %1  max %2  |max| %3 @ node %4")
                                     .arg(signedMin, 0, 'g', 4).arg(signedMax, 0, 'g', 4)
                                     .arg(maxAbs, 0, 'g', 4).arg(maxAbsId));
    }

    refreshResultActor();
}

void ViewportWidget::refreshResultActor()
{
    if (!m_resultDisplayGrid)
        return;

    vtkDataSet *base = m_sectionLayerIndex >= 0 && m_sectionSurface ? static_cast<vtkDataSet *>(m_sectionSurface.Get())
                                                                    : static_cast<vtkDataSet *>(m_resultDisplayGrid.Get());
    if (m_deformed && base->GetPointData()->GetArray("displacement")) {
        base->GetPointData()->SetActiveVectors("displacement");
        vtkNew<vtkWarpVector> warp;
        warp->SetInputData(base);
        warp->SetScaleFactor(m_deformationScale);
        warp->Update();
        m_resultMapper->SetInputData(warp->GetOutput());
        // Re-apply the active scalar selection: warp preserves point data,
        // but SetInputData above swapped which dataset the mapper reads.
        if (!m_activeField.isEmpty()) {
            m_resultMapper->SetScalarModeToUsePointFieldData();
            m_resultMapper->SelectColorArray(m_activeField.toUtf8().constData());
        }
    } else {
        m_resultMapper->SetInputData(base);
    }

    if (m_selectedNode >= 0) highlightNode(m_selectedNode);
    else if (m_selectedElement >= 0) highlightElement(m_selectedElement);

    if (m_renderWindow)
        render();
}

void ViewportWidget::clearSelectionHighlight()
{
    if (m_draftActor && !m_drawing) m_draftActor->VisibilityOff();
    m_selectedNode = m_selectedElement = -1;
    m_highlightActor->SetVisibility(false);
    if (m_renderWindow)
        render();
}

void ViewportWidget::highlightNode(vtkIdType solverNodeId)
{
    auto *grid = (m_mode == ViewMode::Results) ? m_resultMapper->GetInput() : static_cast<vtkDataSet *>(m_plateMesh.Get());
    if (!grid || solverNodeId < 0 || solverNodeId >= grid->GetNumberOfPoints()) {
        clearSelectionHighlight();
        return;
    }
    double point[3];
    m_selectedNode = solverNodeId;
    m_selectedElement = -1;
    m_highlightMapper->SetInputConnection(m_highlightSphere->GetOutputPort());
    grid->GetPoint(solverNodeId, point);
    double bounds[6];
    grid->GetBounds(bounds);
    double diag = std::sqrt(std::pow(bounds[1] - bounds[0], 2) + std::pow(bounds[3] - bounds[2], 2));
    m_highlightSphere->SetRadius(std::max(diag * 0.01, 1e-3));
    m_highlightSphere->SetCenter(point);
    m_highlightActor->SetVisibility(true);
    if (m_renderWindow)
        render();
}

void ViewportWidget::highlightElement(vtkIdType solverElementId)
{
    auto *grid = (m_mode == ViewMode::Results) ? m_resultMapper->GetInput() : static_cast<vtkDataSet *>(m_plateMesh.Get());
    if (!grid) {
        clearSelectionHighlight();
        return;
    }
    vtkCellData *cellData = grid->GetCellData();
    vtkDataArray *ids = cellData ? cellData->GetArray("solver_element_id") : nullptr;
    vtkIdType localCellId = solverElementId;
    if (ids) {
        localCellId = -1;
        for (vtkIdType c = 0; c < ids->GetNumberOfTuples(); ++c) {
            if (static_cast<vtkIdType>(ids->GetTuple1(c)) == solverElementId) {
                localCellId = c;
                break;
            }
        }
    }
    if (localCellId < 0 || localCellId >= grid->GetNumberOfCells()) {
        clearSelectionHighlight();
        return;
    }
    m_selectedElement = solverElementId;
    m_selectedNode = -1;
    auto *cell = grid->GetCell(localCellId);
    vtkNew<vtkPoints> points;
    vtkNew<vtkCellArray> lines;
    const vtkIdType count = cell->GetNumberOfPoints();
    for (vtkIdType i = 0; i < count; ++i) {
        double xyz[3];
        grid->GetPoint(cell->GetPointId(i), xyz);
        points->InsertNextPoint(xyz);
    }
    if (count == 8) {
        const vtkIdType edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        for (const auto &edge : edges) lines->InsertNextCell(2, edge);
    } else {
        lines->InsertNextCell(count + 1);
        for (vtkIdType i = 0; i < count; ++i) lines->InsertCellPoint(i);
        lines->InsertCellPoint(0);
    }
    vtkNew<vtkPolyData> outline;
    outline->SetPoints(points);
    outline->SetLines(lines);
    m_highlightMapper->SetInputData(outline);
    m_highlightMapper->ScalarVisibilityOff();
    m_highlightActor->GetProperty()->SetLineWidth(4.0);
    m_highlightActor->SetVisibility(true);
    if (m_renderWindow)
        render();
}

void ViewportWidget::handleHover(int displayX, int displayY)
{
    if (m_mode == ViewMode::Model)
        return;

    vtkActor *targetActor = (m_mode == ViewMode::Results) ? m_resultActor.Get() : m_meshActor.Get();
    if (!targetActor || !targetActor->GetVisibility() || !m_renderer)
        return;

    if (std::abs(displayX - m_lastHoverX) < 2 && std::abs(displayY - m_lastHoverY) < 2)
        return;
    m_lastHoverX = displayX;
    m_lastHoverY = displayY;

    vtkNew<vtkPointPicker> picker;
    picker->SetTolerance(0.015);
    picker->PickFromListOn();
    picker->AddPickList(targetActor);
    if (picker->Pick(displayX, displayY, 0, m_renderer) != 0) {
        const vtkIdType localId = picker->GetPointId();
        if (localId >= 0) {
            vtkDataSet *grid = (m_mode == ViewMode::Results) ? m_resultMapper->GetInput() : static_cast<vtkDataSet *>(m_plateMesh.Get());
            if (grid) {
                vtkIdType solverId = localId;
                if (vtkDataArray *ids = grid->GetPointData()->GetArray("solver_node_id"))
                    solverId = static_cast<vtkIdType>(ids->GetTuple1(localId));
                double coords[3];
                grid->GetPoint(localId, coords);
                QString msg = tr("Node #%1 (%2, %3, %4 mm)")
                    .arg(solverId)
                    .arg(coords[0], 0, 'f', 1)
                    .arg(coords[1], 0, 'f', 1)
                    .arg(coords[2], 0, 'f', 1);
                if (m_mode == ViewMode::Results && !m_activeField.isEmpty()) {
                    if (vtkDataArray *scalars = grid->GetPointData()->GetArray(m_activeField.toUtf8().constData())) {
                        const double val = scalars->GetTuple1(localId);
                        msg += tr(" · %1 = %2").arg(m_activeField).arg(val, 0, 'g', 5);
                    }
                }
                if (m_vtkWidget)
                    m_vtkWidget->setToolTip(msg);
                emit statusMessage(msg);
                return;
            }
        }
    }
    if (m_vtkWidget && !m_vtkWidget->toolTip().isEmpty())
        m_vtkWidget->setToolTip(QString());
}

void ViewportWidget::handleClickPick(int displayX, int displayY)
{
    if (m_mode == ViewMode::Model) {
        QPointF point;
        if (projectToPlate(displayX, displayY, point))
            for (auto it = m_patches.crbegin(); it != m_patches.crend(); ++it)
                if (it->visible && point.x() >= it->x1 && point.x() <= it->x2 && point.y() >= it->y1 && point.y() <= it->y2) {
                    emit patchPicked(it->id);
                    return;
                }
        emit selectionCleared();
        return;
    }
    if (m_pickMode == PickMode::None)
        return;

    vtkActor *targetActor = (m_mode == ViewMode::Results) ? m_resultActor.Get() : m_meshActor.Get();
    if (!targetActor || !targetActor->GetVisibility())
        return;

    if (m_pickMode == PickMode::Node) {
        vtkNew<vtkPointPicker> picker;
        picker->SetTolerance(0.01);
        picker->PickFromListOn();
        picker->AddPickList(targetActor);
        if (picker->Pick(displayX, displayY, 0, m_renderer) == 0) {
            emit selectionCleared();
            return;
        }
        vtkIdType localId = picker->GetPointId();
        if (localId < 0) { emit selectionCleared(); return; }
        vtkDataSet *grid = (m_mode == ViewMode::Results) ? m_resultMapper->GetInput() : static_cast<vtkDataSet *>(m_plateMesh.Get());
        vtkIdType solverId = localId;
        if (grid) {
            if (vtkDataArray *ids = grid->GetPointData()->GetArray("solver_node_id"))
                solverId = static_cast<vtkIdType>(ids->GetTuple1(localId));
        }
        emit nodePicked(solverId);
    } else if (m_pickMode == PickMode::Element) {
        vtkNew<vtkCellPicker> picker;
        picker->SetTolerance(0.001);
        picker->PickFromListOn();
        picker->AddPickList(targetActor);
        if (picker->Pick(displayX, displayY, 0, m_renderer) == 0) {
            emit selectionCleared();
            return;
        }
        vtkIdType localId = picker->GetCellId();
        if (localId < 0) { emit selectionCleared(); return; }
        vtkDataSet *grid = (m_mode == ViewMode::Results) ? m_resultMapper->GetInput() : static_cast<vtkDataSet *>(m_plateMesh.Get());
        vtkIdType solverId = localId;
        if (grid) {
            if (vtkDataArray *ids = grid->GetCellData()->GetArray("solver_element_id"))
                solverId = static_cast<vtkIdType>(ids->GetTuple1(localId));
        }
        emit elementPicked(solverId);
    }
}

void ViewportWidget::setPickMode(int index)
{
    if (!m_pickCombo)
        return;
    QString text = m_pickCombo->itemText(index);
    if (text == tr("Select Node"))
        m_pickMode = PickMode::Node;
    else if (text == tr("Select Element"))
        m_pickMode = PickMode::Element;
    else
        m_pickMode = PickMode::None;
}

void ViewportWidget::fitView()
{
    if (!m_renderer)
        return;
    m_renderer->ResetCamera();
    render();
}

void ViewportWidget::setCameraPreset(const QString &preset)
{
    if (m_viewCombo && m_viewCombo->currentText() != preset) {
        const QSignalBlocker blocker(m_viewCombo);
        m_viewCombo->setCurrentText(preset);
    }
    if (auto *seg = findChild<QWidget *>(QStringLiteral("cameraSegmentedControl"))) {
        for (auto *btn : seg->findChildren<QToolButton *>()) {
            bool matches = (preset == tr("Isometric") && btn->text() == tr("Iso")) ||
                           (preset == btn->text());
            if (matches && !btn->isChecked()) {
                const QSignalBlocker blocker(btn);
                btn->setChecked(true);
            }
        }
    }
    if (!m_renderer)
        return;
    vtkCamera *camera = m_renderer->GetActiveCamera();
    camera->SetParallelProjection(preset == tr("Top"));
    camera->SetFocalPoint(0, 0, 0);

    if (preset == tr("Front")) {
        camera->SetPosition(0, -1, 0);
        camera->SetViewUp(0, 0, 1);
    } else if (preset == tr("Top")) {
        camera->SetPosition(0, 0, m_loadFromNegativeZ ? -1 : 1);
        camera->SetViewUp(0, 1, 0);
    } else if (preset == tr("Right")) {
        camera->SetPosition(1, 0, 0);
        camera->SetViewUp(0, 0, 1);
    } else {
        camera->SetPosition(1, -1, m_loadFromNegativeZ ? -1 : 1);
        camera->SetViewUp(0, 0, 1);
    }
    fitView();
}

void ViewportWidget::setRepresentation(const QString &representation)
{
    for (vtkActor *actor : {m_meshActor.Get(), m_resultActor.Get()}) {
        if (!actor)
            continue;
        vtkProperty *prop = actor->GetProperty();
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
    }
    if (m_renderWindow)
        render();
}

void ViewportWidget::setAxesVisible(bool visible)
{
    if (m_axesWidget)
        m_axesWidget->SetEnabled(visible ? 1 : 0);
    if (m_renderWindow)
        render();
}

bool ViewportWidget::projectToPlate(int x, int y, QPointF &point)
{
    // In a perspective side view, off-axis rays can intersect the load plane at the
    // camera itself. Reject an edge-on camera before accepting such a ray.
    const double *direction = m_renderer->GetActiveCamera()->GetDirectionOfProjection();
    if (std::abs(direction[2]) <= 1e-6) return false;
    double ends[2][3];
    for (int i = 0; i < 2; ++i) {
        m_renderer->SetDisplayPoint(x, y, i);
        m_renderer->DisplayToWorld();
        const double *world = m_renderer->GetWorldPoint();
        if (std::abs(world[3]) < 1e-15) return false;
        for (int d = 0; d < 3; ++d) ends[i][d] = world[d] / world[3];
    }
    const double dx = ends[1][0] - ends[0][0], dy = ends[1][1] - ends[0][1], dz = ends[1][2] - ends[0][2];
    if (std::abs(dz) <= 1e-6 * std::hypot(dx, dy, dz)) return false;
    const double t = (m_loadPlaneZ - ends[0][2]) / dz;
    point = QPointF(ends[0][0] + t * dx, ends[0][1] + t * dy);
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

void ViewportWidget::setEditingEnabled(bool enabled)
{
    m_editingEnabled = enabled;
    if (!enabled) cancelRectangleDrawing();
}

void ViewportWidget::startRectangleDrawing()
{
    if (m_mode != ViewMode::Model || !m_editingEnabled) return;
    if (!m_plateMesh || m_plateMesh->GetNumberOfCells() == 0) { emit statusMessage(tr("Define a valid model grid before drawing.")); return; }
    clearSelectionHighlight();
    setCameraPreset(tr("Top"));
    m_drawing = true;
    m_draggingRectangle = false;
    m_draftLabel->setText(tr("Drag a pressure rectangle on the loaded XY face. Escape cancels."));
    m_draftLabel->show();
    m_vtkWidget->setFocus();
}

void ViewportWidget::cancelRectangleDrawing()
{
    m_drawing = m_draggingRectangle = false;
    if (m_draftActor) m_draftActor->VisibilityOff();
    if (m_draftLabel) m_draftLabel->hide();
    render();
}

void ViewportWidget::beginRectangle(int x, int y)
{
    if (!projectToPlate(x, y, m_rectangleStart)) {
        m_draftLabel->setText(tr("The view is parallel to the loaded face. Return to Top view and draw again."));
        return;
    }
    m_rectangleBounds = QRectF(m_rectangleStart, m_rectangleStart);
    m_draggingRectangle = true;
}

void ViewportWidget::showRectangle(const QRectF &bounds, bool valid)
{
    vtkNew<vtkPoints> points;
    points->SetDataTypeToDouble();
    const double z = m_loadPlaneZ + (m_loadFromNegativeZ ? -2.0 * m_overlayOffset : 2.0 * m_overlayOffset);
    points->InsertNextPoint(bounds.left(), bounds.top(), z);
    points->InsertNextPoint(bounds.right(), bounds.top(), z);
    points->InsertNextPoint(bounds.right(), bounds.bottom(), z);
    points->InsertNextPoint(bounds.left(), bounds.bottom(), z);
    vtkNew<vtkPolyData> quad;
    quad->SetPoints(points);
    vtkNew<vtkCellArray> cells;
    vtkIdType ids[4] = {0, 1, 2, 3}; cells->InsertNextCell(4, ids);
    quad->SetPolys(cells);
    m_draftMapper->SetInputData(quad);
    m_draftActor->GetProperty()->SetColor(1.0, valid ? 0.85 : 0.15, 0.05);
    m_draftActor->VisibilityOn();
    render();
}

void ViewportWidget::moveRectangle(int x, int y)
{
    if (!m_draggingRectangle) return;
    QPointF end;
    if (!projectToPlate(x, y, end)) {
        m_draggingRectangle = false;
        m_draftActor->VisibilityOff();
        m_draftLabel->setText(tr("Return to Top view and draw again."));
        render();
        return;
    }
    m_rectangleBounds = QRectF(m_rectangleStart, end).normalized();
    double domain[6]; m_plateMesh->GetBounds(domain);
    const auto &b = m_rectangleBounds;
    const bool inside = b.left() >= domain[0] && b.right() <= domain[1] && b.top() >= domain[2] && b.bottom() <= domain[3];
    showRectangle(b, inside);
    m_draftLabel->setText(tr("X: %1 .. %2 mm | Y: %3 .. %4 mm | Area: %5 mm²%6")
        .arg(b.left(), 0, 'g', 6).arg(b.right(), 0, 'g', 6).arg(b.top(), 0, 'g', 6).arg(b.bottom(), 0, 'g', 6)
        .arg(b.width() * b.height(), 0, 'g', 6).arg(inside ? QString() : tr(" — Outside model bounds")));
}

void ViewportWidget::finishRectangle(int x, int y)
{
    if (!m_draggingRectangle) return;
    moveRectangle(x, y);
    if (!m_draggingRectangle) return;
    double domain[6]; m_plateMesh->GetBounds(domain);
    const double tolerance = 1e-9 * std::max(domain[1] - domain[0], domain[3] - domain[2]);
    if (m_rectangleBounds.width() <= tolerance || m_rectangleBounds.height() <= tolerance) {
        m_draggingRectangle = false;
        m_draftLabel->setText(tr("Rectangle needs positive width and height. Draw again in Top view."));
        return;
    }
    const auto bounds = m_rectangleBounds;
    cancelRectangleDrawing();
    emit rectangleDrafted(bounds);
}

void ViewportWidget::highlightPatch(const QUuid &id)
{
    if (m_mode != ViewMode::Model) return;
    for (const auto &p : m_patches)
        if (p.id == id && p.visible) { showRectangle(QRectF(QPointF(p.x1, p.y1), QPointF(p.x2, p.y2)), true); return; }
    m_draftActor->VisibilityOff();
    render();
}

void ViewportWidget::goToExtremum(int kind)
{
    if (!m_resultSet || !m_resultSet->isValid()) { emit statusMessage(tr("Load results before navigating extrema.")); return; }
    const auto extrema = m_resultSet->computeExtrema(m_activeField);
    if (!extrema.valid) return;
    const auto id = kind == 0 ? extrema.signedMinNodeId : kind == 1 ? extrema.signedMaxNodeId : extrema.maxAbsoluteNodeId;
    highlightNode(id);
    double point[3]; m_resultMapper->GetInput()->GetPoint(id, point);
    auto *camera = m_renderer->GetActiveCamera();
    double position[3], focal[3]; camera->GetPosition(position); camera->GetFocalPoint(focal);
    camera->SetPosition(position[0] + point[0] - focal[0], position[1] + point[1] - focal[1], position[2] + point[2] - focal[2]);
    camera->SetFocalPoint(point);
    m_renderer->ResetCameraClippingRange();
    render();
    emit nodePicked(id);
}

bool ViewportWidget::exportScreenshot(const QString &path, int revision, bool stale, QString *error)
{
    if (m_mode != ViewMode::Results || !m_resultSet || !m_vtkWidget->isValid()) {
        if (error) *error = tr("Open the Results viewport with a completed result before exporting.");
        return false;
    }
    render();
    const QImage view = m_vtkWidget->grabFramebuffer();
    if (view.isNull()) { if (error) *error = tr("Could not capture the rendered viewport."); return false; }
    QImage report(view.width(), view.height() + 145, QImage::Format_RGB32);
    report.fill(Qt::white);
    QPainter painter(&report);
    painter.setPen(Qt::black);
    QFont font = painter.font(); font.setPixelSize(14); painter.setFont(font);
    QString fieldLabel = m_fieldCombo->currentText();
    QString unit, averaging;
    for (const auto &field : m_resultSet->availableFields()) if (field.arrayName == m_activeField) { unit = field.unit; averaging = field.averagingNote; }
    const bool solid = m_resultSet->modelKind() == ModelKind::SolidC3D8;
    const QString identity = femgui::backendId(m_resultSet->modelKind()) + " / " + modelKindId(m_resultSet->modelKind());
    const QString semantics = solid ? tr("Source Cartesian X/Y/Z | full C3D8 integration | engineering shear gamma")
                                    : tr("Source plate frame");
    const QString deformation = m_deformed ? QString::number(m_deformationScale) + "x" : tr("Undeformed");
    const QString deformationLine = averaging.isEmpty()
        ? tr("Deformation scale: %1").arg(deformation)
        : tr("Deformation scale: %1 | %2").arg(deformation, averaging);
    painter.drawText(QRect(10, 5, report.width() - 20, 135), Qt::TextWordWrap,
        tr("%1 [%2] — Node data\nRun: %3 | Model revision: %4 | %5\n%6\n%7\n%8\n%9")
        .arg(fieldLabel, unit, m_resultSet->runId()).arg(revision)
        .arg(stale ? tr("Previous model revision") : tr("Current model revision"))
        .arg(identity, semantics, sectionContext())
        .arg(deformationLine));
    painter.drawImage(0, 145, view);
    painter.end();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !report.save(&file, "PNG") || !file.commit()) {
        if (error) *error = file.errorString(); return false;
    }
    return true;
}

void ViewportWidget::refreshThemeColors()
{
    const QPalette palette = QApplication::palette();
    auto vtkColor = [](const QColor &color) {
        return std::array<double, 3>{color.redF(), color.greenF(), color.blueF()};
    };
    const auto background = vtkColor(UiStyle::viewportBackground(palette));
    const auto backgroundAccent = vtkColor(UiStyle::viewportBackgroundAccent(palette));
    const auto mesh = vtkColor(UiStyle::meshColor(palette));
    const auto edge = vtkColor(UiStyle::edgeColor(palette));
    const auto load = vtkColor(UiStyle::loadColor(palette));
    const auto selection = vtkColor(UiStyle::selectionColor(palette));

    if (m_renderer) {
        m_renderer->SetBackground(background.data());
        m_renderer->SetBackground2(backgroundAccent.data());
    }
    if (m_meshActor) {
        m_meshActor->GetProperty()->SetColor(mesh[0], mesh[1], mesh[2]);
        m_meshActor->GetProperty()->SetEdgeColor(edge[0], edge[1], edge[2]);
    }
    if (m_patchActor) {
        m_patchActor->GetProperty()->SetColor(load[0], load[1], load[2]);
    }
    if (m_patchOutlineActor) {
        m_patchOutlineActor->GetProperty()->SetColor(load[0], load[1], load[2]);
    }
    if (m_highlightActor) {
        m_highlightActor->GetProperty()->SetColor(selection[0], selection[1], selection[2]);
    }
    render();
}

} // namespace femgui
