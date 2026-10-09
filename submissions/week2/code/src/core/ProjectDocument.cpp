#include "core/ProjectDocument.h"

namespace femgui {

namespace {
bool equalPlate(const PlateDefinition &a, const PlateDefinition &b, bool physicalOnly)
{
    if (a.xNodes != b.xNodes || a.yNodes != b.yNodes ||
        a.slab.emod != b.slab.emod || a.slab.nu != b.slab.nu || a.slab.thickness != b.slab.thickness ||
        a.foundation.kx != b.foundation.kx || a.foundation.ky != b.foundation.ky || a.foundation.kz != b.foundation.kz ||
        a.patches.size() != b.patches.size()) return false;
    for (int i = 0; i < a.patches.size(); ++i) {
        const auto &x = a.patches[i], &y = b.patches[i];
        if (x.id != y.id || x.x1 != y.x1 || x.x2 != y.x2 || x.y1 != y.y1 || x.y2 != y.y2 ||
            x.q != y.q || x.enabledForAnalysis != y.enabledForAnalysis ||
            (!physicalOnly && (x.name != y.name || x.visible != y.visible))) return false;
    }
    return true;
}

bool equalSolid(const SolidDefinition &a, const SolidDefinition &b, bool physicalOnly)
{
    if (a.xNodes != b.xNodes || a.yNodes != b.yNodes || a.zNodes != b.zNodes ||
        a.material.emod != b.material.emod || a.material.nu != b.material.nu ||
        a.foundation.kx != b.foundation.kx || a.foundation.ky != b.foundation.ky || a.foundation.kz != b.foundation.kz ||
        a.reducedIntegration != b.reducedIntegration || a.coordinateConvention != b.coordinateConvention ||
        a.patches.size() != b.patches.size()) return false;
    for (int i = 0; i < a.patches.size(); ++i) {
        const auto &x = a.patches[i], &y = b.patches[i];
        if (x.id != y.id || x.x1 != y.x1 || x.x2 != y.x2 || x.y1 != y.y1 || x.y2 != y.y2 ||
            x.q != y.q || x.enabledForAnalysis != y.enabledForAnalysis ||
            (!physicalOnly && (x.name != y.name || x.visible != y.visible))) return false;
    }
    return true;
}

bool equalModel(const ModelDefinition &a, const ModelDefinition &b, bool physicalOnly)
{
    if (a.index() != b.index()) return false;
    if (const auto *plate = std::get_if<PlateDefinition>(&a))
        return equalPlate(*plate, std::get<PlateDefinition>(b), physicalOnly);
    return equalSolid(std::get<SolidDefinition>(a), std::get<SolidDefinition>(b), physicalOnly);
}
}

class ModelEditCommand : public QUndoCommand
{
public:
    ModelEditCommand(ProjectDocument *doc, ModelDefinition before, ModelDefinition after,
                     int beforeRevision, int afterRevision, const QString &text)
        : QUndoCommand(text), m_doc(doc), m_before(std::move(before)), m_after(std::move(after)),
          m_beforeRevision(beforeRevision), m_afterRevision(afterRevision) {}
    void undo() override { m_doc->applyHistory(m_before, m_beforeRevision); }
    void redo() override { m_doc->applyHistory(m_after, m_afterRevision); }
private:
    ProjectDocument *m_doc;
    ModelDefinition m_before, m_after;
    int m_beforeRevision, m_afterRevision;
};

ProjectDocument::ProjectDocument(QObject *parent)
    : QObject(parent)
{
    // Seed a small default plate so the app never opens with an empty,
    // unmeshable model. This is an editable starting point.
    PlateDefinition plate;
    plate.xNodes = {0.0, 1000.0, 2000.0, 3000.0};
    plate.yNodes = {0.0, 1000.0, 2000.0};
    PressurePatch patch;
    patch.name = "Patch 1";
    patch.x1 = 500.0;
    patch.x2 = 1500.0;
    patch.y1 = 500.0;
    patch.y2 = 1500.0;
    patch.q = 0.7;
    plate.patches.append(patch);
    m_model = std::move(plate);
}

void ProjectDocument::setName(const QString &name)
{
    if (m_name == name)
        return;
    m_name = name;
    m_dirty = true;
    emit nameChanged(m_name);
    emit dirtyChanged(true);
}

void ProjectDocument::setDescription(const QString &description)
{
    if (m_description == description)
        return;
    m_description = description;
    m_dirty = true;
    emit dirtyChanged(true);
}

void ProjectDocument::editPlate(const std::function<void(PlateDefinition &)> &mutator, const QString &description)
{
    if (!isPlate()) return;
    auto edited = plate();
    mutator(edited);
    editModel(std::move(edited), description);
}

void ProjectDocument::editSolid(const std::function<void(SolidDefinition &)> &mutator, const QString &description)
{
    if (!isSolid()) return;
    auto edited = solid();
    mutator(edited);
    editModel(std::move(edited), description);
}

void ProjectDocument::editModel(ModelDefinition edited, const QString &description)
{
    if (equalModel(m_model, edited, false)) return;
    const int revision = equalModel(m_model, edited, true) ? m_modelRevision : ++m_nextRevision;
    m_undoStack.push(new ModelEditCommand(this, m_model, std::move(edited), m_modelRevision, revision, description));
}

void ProjectDocument::replaceModelForLoad(ModelDefinition model)
{
    m_undoStack.clear();
    m_model = std::move(model);
    emit modelChanged();
}

void ProjectDocument::applyHistory(const ModelDefinition &model, int revision)
{
    m_model = model;
    m_modelRevision = revision;
    m_dirty = true;
    emit modelChanged();
    emit dirtyChanged(true);
}

void ProjectDocument::addRun(const RunRecord &run)
{
    m_runs.append(run);
    m_dirty = true;
    emit dirtyChanged(true);
    emit runsChanged();
}

void ProjectDocument::updateRun(const QString &runId, const std::function<void(RunRecord &)> &mutator)
{
    for (RunRecord &run : m_runs) {
        if (run.runId == runId) {
            mutator(run);
            m_dirty = true;
            emit dirtyChanged(true);
            emit runsChanged();
            return;
        }
    }
}

const RunRecord *ProjectDocument::findRun(const QString &runId) const
{
    for (const RunRecord &run : m_runs)
        if (run.runId == runId)
            return &run;
    return nullptr;
}

void ProjectDocument::setResultSet(ResultSet result, int atModelRevision)
{
    m_resultSet = std::move(result);
    m_resultSetModelRevision = atModelRevision;
    emit resultSetChanged();
}

void ProjectDocument::clearResultSet()
{
    m_resultSet = ResultSet();
    m_resultSetModelRevision = -1;
    emit resultSetChanged();
}

} // namespace femgui
