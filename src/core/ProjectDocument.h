#pragma once

#include <functional>

#include <QObject>
#include <QString>
#include <QVector>
#include <QUndoStack>

#include "core/ModelDefinition.h"
#include "core/ResultSet.h"
#include "core/RunRecord.h"

namespace femgui {

// Versioned editable model (section 4.2). Owns the active typed model, run
// history, and the active result set; owns no widgets. Every physical edit
// bumps modelRevision(), which is how stale results get flagged.
class ProjectDocument : public QObject
{
    Q_OBJECT

public:
    static constexpr int kSchemaVersion = 2;

    explicit ProjectDocument(QObject *parent = nullptr);

    const QString &name() const { return m_name; }
    void setName(const QString &name);

    const QString &description() const { return m_description; }
    void setDescription(const QString &description);

    const QString &filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }

    const ModelDefinition &model() const { return m_model; }
    ModelKind modelKind() const { return femgui::modelKind(m_model); }
    QString modelKindId() const { return femgui::modelKindId(modelKind()); }
    QString backendId() const { return femgui::backendId(modelKind()); }
    bool isPlate() const { return std::holds_alternative<PlateDefinition>(m_model); }
    bool isSolid() const { return std::holds_alternative<SolidDefinition>(m_model); }
    const PlateDefinition &plate() const { return std::get<PlateDefinition>(m_model); }
    const SolidDefinition &solid() const { return std::get<SolidDefinition>(m_model); }
    // Mutates the active definition through its typed callback, then bumps
    // modelRevision() and marks the project dirty. Centralizing the bump
    // here means every editor gets stale-result invalidation for free.
    void editPlate(const std::function<void(PlateDefinition &)> &mutator, const QString &description = "Edit model");
    void editSolid(const std::function<void(SolidDefinition &)> &mutator, const QString &description = "Edit solid model");
    // Transactional project loading commits a fully parsed typed model here.
    void replaceModelForLoad(ModelDefinition model);
    QUndoStack *undoStack() { return &m_undoStack; }

    int modelRevision() const { return m_modelRevision; }
    // Used only after a complete, validated project-file parse.
    void restoreModelRevision(int revision) { m_undoStack.clear(); m_modelRevision = revision; m_nextRevision = revision; }
    void replaceRuns(QVector<RunRecord> runs) {
        m_runs = std::move(runs);
        for (const auto &run : m_runs) m_nextRevision = std::max(m_nextRevision, run.modelRevisionAtLaunch);
        emit runsChanged();
    }

    bool isDirty() const { return m_nonUndoDirty || !m_undoStack.isClean(); }
    void markClean();

    const QVector<RunRecord> &runs() const { return m_runs; }
    void addRun(const RunRecord &run);
    void updateRun(const QString &runId, const std::function<void(RunRecord &)> &mutator);
    const RunRecord *findRun(const QString &runId) const;

    // The result set currently shown in the Results tab, if any, and the
    // model revision it was computed against (section 4.4: never attach an
    // old array set to a new mesh just because counts match).
    bool hasResultSet() const { return m_resultSet.isValid(); }
    const ResultSet &resultSet() const { return m_resultSet; }
    int resultSetModelRevision() const { return m_resultSetModelRevision; }
    bool isResultSetStale() const { return hasResultSet() && m_resultSetModelRevision != m_modelRevision; }
    void setResultSet(ResultSet result, int atModelRevision);
    void clearResultSet();

signals:
    void nameChanged(const QString &name);
    void modelChanged();      // active typed model edited or replaced
    void dirtyChanged(bool dirty);
    void runsChanged();
    void resultSetChanged();

private:
    friend class ModelEditCommand;
    void editModel(ModelDefinition edited, const QString &description);
    void applyHistory(const ModelDefinition &model, int revision);
    QUndoStack m_undoStack;
    int m_nextRevision = 0;
    QString m_name = "Untitled project";
    QString m_description;
    QString m_filePath;
    ModelDefinition m_model;
    int m_modelRevision = 0;
    bool m_nonUndoDirty = false;

    QVector<RunRecord> m_runs;
    ResultSet m_resultSet;
    int m_resultSetModelRevision = -1;
};

} // namespace femgui
