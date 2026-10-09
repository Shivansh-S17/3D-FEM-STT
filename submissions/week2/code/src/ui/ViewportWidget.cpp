#include "ui/ViewportWidget.h"

#include "core/PlateDefinition.h"
#include "core/ResultSet.h"

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
#include <vtkDataSetMapper.h>
#include <vtkDoubleArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkIdTypeArray.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkLookupTable.h>
#include <vtkNamedColors.h>
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
#include <vtkSphereSource.h>
#include <vtkUnstructuredGrid.h>
#include <vtkWarpVector.h>

#include <algorithm>
#include <cmath>

namespace femgui {

namespace {
constexpr int kClickDragThresholdPixels = 4;
}

// Forwards a plain (small-movement) left click to ViewportWidget::handleClickPick
// while still letting the base class drive normal camera interaction for drags
// (dragging beyond a small threshold navigates, it does not select).
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
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(4, 2, 4, 2);

    auto *fitButton = new QToolButton(toolbar);
    fitButton->setText(tr("Fit View"));
    connect(fitButton, &QToolButton::clicked, this, &ViewportWidget::fitView);
    toolbarLayout->addWidget(fitButton);

    auto *viewCombo = new QComboBox(toolbar);
    m_viewCombo = viewCombo;
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

    if (m_mode == ViewMode::Model) {
        m_drawButton = new QToolButton(toolbar);
        m_drawButton->setText(tr("Draw Patch"));
        connect(m_drawButton, &QToolButton::clicked, this, &ViewportWidget::startRectangleDrawing);
        toolbarLayout->addWidget(m_drawButton);
    }

    if (m_mode == ViewMode::Mesh || m_mode == ViewMode::Results) {
        m_pickCombo = new QComboBox(toolbar);
        m_pickCombo->addItem(tr("Navigate"));
        m_pickCombo->addItem(tr("Select Node"));
        m_pickCombo->addItem(tr("Select Element"));
        connect(m_pickCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ViewportWidget::setPickMode);
        toolbarLayout->addWidget(m_pickCombo);
    }

    QWidget *resultControls = nullptr;
    if (m_mode == ViewMode::Results) {
        resultControls = new QWidget(this);
        auto *resultLayout = new QHBoxLayout(resultControls);
        resultLayout->setContentsMargins(4, 2, 4, 2);
        m_fieldCombo = new QComboBox(resultControls);
        m_fieldCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_fieldCombo->setMinimumContentsLength(20);
        connect(m_fieldCombo, &QComboBox::currentTextChanged, this, [this](const QString &) {
            if (m_fieldCombo->currentIndex() >= 0)
                setActiveField(m_fieldCombo->currentData().toString());
        });
        resultLayout->addWidget(m_fieldCombo, 1);

        m_deformedCheck = new QCheckBox(tr("Deformed"), resultControls);
        connect(m_deformedCheck, &QCheckBox::toggled, this, [this](bool checked) {
            m_deformed = checked;
            updateDeformationControlsEnabled();
            refreshResultActor();
        });
        resultLayout->addWidget(m_deformedCheck);

        resultLayout->addWidget(new QLabel(tr("Scale:"), resultControls));
        m_deformationSlider = new QSlider(Qt::Horizontal, resultControls);
        m_deformationSlider->setRange(1, 500); // 0.1x .. 50x, see mapping below
        m_deformationSlider->setValue(10);
        m_deformationSlider->setMaximumWidth(100);
        connect(m_deformationSlider, &QSlider::valueChanged, this, [this](int value) {
            m_deformationScale = value / 10.0;
            m_scaleLabel->setText(QString::number(m_deformationScale, 'g', 4) + "x");
            refreshResultActor();
        });
        resultLayout->addWidget(m_deformationSlider);
        m_scaleLabel = new QLabel("1x", resultControls);
        resultLayout->addWidget(m_scaleLabel);
        updateDeformationControlsEnabled();

        m_extremaLabel = new QLabel(this);
        m_extremaLabel->setWordWrap(true);
        m_extremaLabel->setContentsMargins(4, 2, 4, 2);
    }

    QString modeLabel;
    switch (m_mode) {
    case ViewMode::Model: modeLabel = tr("Model"); break;
    case ViewMode::Mesh: modeLabel = tr("Mesh"); break;
    case ViewMode::Results: modeLabel = tr("Results"); break;
    }
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(new QLabel(modeLabel, toolbar));

    layout->addWidget(toolbar);
    if (resultControls) {
        layout->addWidget(resultControls);
        auto *extremaRow = new QHBoxLayout;
        extremaRow->addWidget(m_extremaLabel, 1);
        for (int kind = 0; kind < 3; ++kind) {
            auto *button = new QToolButton(this);
            button->setText(QStringList{tr("Min"), tr("Max"), tr("|Max|")}[kind]);
            connect(button, &QToolButton::clicked, this, [this, kind] { goToExtremum(kind); });
            extremaRow->addWidget(button);
        }
        layout->addLayout(extremaRow);
    }
    if (m_mode == ViewMode::Model) {
        m_draftLabel = new QLabel(this);
        m_draftLabel->setWordWrap(true);
        m_draftLabel->hide();
        layout->addWidget(m_draftLabel);
    }
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { cancelRectangleDrawing(); emit selectionCleared(); });

    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
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
    m_renderer->AddActor(m_draftActor);

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
    vtkNew<vtkNamedColors> colors;

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(colors->GetColor3d("SlateGray").GetData());
    m_renderer->SetBackground2(colors->GetColor3d("LightSteelBlue").GetData());
    m_renderer->GradientBackgroundOn();
    m_renderWindow->AddRenderer(m_renderer);

    m_meshMapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_meshActor = vtkSmartPointer<vtkActor>::New();
    m_meshActor->SetMapper(m_meshMapper);
    m_meshActor->GetProperty()->SetColor(colors->GetColor3d("SteelBlue").GetData());
    m_meshActor->GetProperty()->SetEdgeColor(colors->GetColor3d("Black").GetData());
    m_meshActor->VisibilityOff();
    m_renderer->AddActor(m_meshActor);

    m_patchAppend = vtkSmartPointer<vtkAppendPolyData>::New();
    m_patchMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_patchActor = vtkSmartPointer<vtkActor>::New();
    m_patchActor->SetMapper(m_patchMapper);
    m_patchActor->GetProperty()->SetColor(colors->GetColor3d("Tomato").GetData());
    m_patchActor->GetProperty()->SetOpacity(0.55);
    m_patchActor->GetProperty()->EdgeVisibilityOn();
    m_patchActor->PickableOff();
    m_patchActor->VisibilityOff();
    m_renderer->AddActor(m_patchActor);

    m_resultMapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_resultActor = vtkSmartPointer<vtkActor>::New();
    m_resultActor->SetMapper(m_resultMapper);
    m_resultActor->VisibilityOff();
    m_renderer->AddActor(m_resultActor);

    m_scalarBar = vtkSmartPointer<vtkScalarBarActor>::New();
    m_scalarBar->SetNumberOfLabels(5);
    m_scalarBar->VisibilityOff();
    m_renderer->AddViewProp(m_scalarBar);

    m_highlightSphere = vtkSmartPointer<vtkSphereSource>::New();
    m_highlightSphere->SetRadius(1.0); // sized relative to mesh extent in highlightNode()
    m_highlightMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_highlightMapper->SetInputConnection(m_highlightSphere->GetOutputPort());
    m_highlightActor = vtkSmartPointer<vtkActor>::New();
    m_highlightActor->SetMapper(m_highlightMapper);
    m_highlightActor->GetProperty()->SetColor(colors->GetColor3d("Yellow").GetData());
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
        m_overlayOffset = std::max(1e-9, diagonal * 1e-5);
        m_loadFromNegativeZ = bounds[5] > bounds[4] && std::abs(loadPlaneZ - bounds[4]) <= m_overlayOffset;
    }
    refreshMeshActor();
    fitView();
}

void ViewportWidget::refreshMeshActor()
{
    bool hasMesh = m_plateMesh && m_plateMesh->GetNumberOfCells() > 0;
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

    if (!resultSet || !resultSet->isValid()) {
        m_resultDisplayGrid = nullptr;
        m_resultActor->SetVisibility(false);
        m_scalarBar->VisibilityOff();
        m_resultMapper->SetInputData(nullptr);
        m_activeField.clear();
        updateDeformationControlsEnabled();
        if (m_extremaLabel)
            m_extremaLabel->clear();
        m_fieldCombo->blockSignals(false);
        if (m_renderWindow)
            render();
        return;
    }

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
    for (const FieldDescriptor &field : plateFieldCatalog()) {
        if (field.arrayName == arrayName) {
            label = field.label;
            unit = field.unit;
            break;
        }
    }
    m_scalarBar->SetLookupTable(lut);
    m_scalarBar->SetTitle((label + " [" + unit + "]\nRun: " + m_resultSet->runId()).toUtf8().constData());
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

    if (m_deformed && m_resultDisplayGrid->GetPointData()->GetArray("displacement")) {
        m_resultDisplayGrid->GetPointData()->SetActiveVectors("displacement");
        vtkNew<vtkWarpVector> warp;
        warp->SetInputData(m_resultDisplayGrid);
        warp->SetScaleFactor(m_deformationScale);
        warp->Update();
        m_resultMapper->SetInputData(vtkUnstructuredGrid::SafeDownCast(warp->GetOutput()));
        // Re-apply the active scalar selection: warp preserves point data,
        // but SetInputData above swapped which dataset the mapper reads.
        if (!m_activeField.isEmpty()) {
            m_resultMapper->SetScalarModeToUsePointFieldData();
            m_resultMapper->SelectColorArray(m_activeField.toUtf8().constData());
        }
    } else {
        m_resultMapper->SetInputData(m_resultDisplayGrid);
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
    auto *grid = (m_mode == ViewMode::Results) ? vtkUnstructuredGrid::SafeDownCast(m_resultMapper->GetInput()) : m_plateMesh.Get();
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
    auto *grid = (m_mode == ViewMode::Results) ? vtkUnstructuredGrid::SafeDownCast(m_resultMapper->GetInput()) : m_plateMesh.Get();
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
        vtkUnstructuredGrid *grid = (m_mode == ViewMode::Results) ? m_resultDisplayGrid.Get() : m_plateMesh.Get();
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
        vtkUnstructuredGrid *grid = (m_mode == ViewMode::Results) ? m_resultDisplayGrid.Get() : m_plateMesh.Get();
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
        camera->SetPosition(1, -1, 1);
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
    if (m_drawButton) m_drawButton->setEnabled(enabled);
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
    QImage report(view.width(), view.height() + 110, QImage::Format_RGB32);
    report.fill(Qt::white);
    QPainter painter(&report);
    painter.setPen(Qt::black);
    QFont font = painter.font(); font.setPixelSize(14); painter.setFont(font);
    QString fieldLabel = m_fieldCombo->currentText();
    QString unit, averaging;
    for (const auto &field : plateFieldCatalog()) if (field.arrayName == m_activeField) { unit = field.unit; averaging = field.averagingNote; }
    painter.drawText(QRect(10, 5, report.width() - 20, 100), Qt::TextWordWrap,
        tr("%1 [%2] — Node data\nRun: %3 | Model revision: %4 | %5\nDeformation scale: %6 | %7")
        .arg(fieldLabel, unit, m_resultSet->runId()).arg(revision)
        .arg(stale ? tr("Previous model revision") : tr("Current model revision"))
        .arg(m_deformed ? QString::number(m_deformationScale) + "x" : tr("Undeformed"), averaging));
    painter.drawImage(0, 110, view);
    painter.end();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !report.save(&file, "PNG") || !file.commit()) {
        if (error) *error = file.errorString(); return false;
    }
    return true;
}

} // namespace femgui
