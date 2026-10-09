#include "io/ProjectSerializer.h"

#include "core/ProjectDocument.h"
#include "core/ModelDefinition.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <cmath>
#include <initializer_list>

namespace femgui {

namespace {

QJsonArray toJsonArray(const QVector<double> &values)
{
    QJsonArray array;
    for (double v : values)
        array.append(v);
    return array;
}

QVector<double> fromJsonArray(const QJsonArray &array, bool *ok)
{
    QVector<double> values;
    for (const QJsonValue &v : array) {
        if (!v.isDouble()) {
            *ok = false;
            return {};
        }
        values.append(v.toDouble());
    }
    return values;
}

bool hasExactKeys(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    if (object.size() != static_cast<qsizetype>(keys.size())) return false;
    for (const char *key : keys)
        if (!object.contains(key)) return false;
    return true;
}

bool readC3D8SolverInput(const QJsonObject &root, SolidDefinition &solid)
{
    if (!hasExactKeys(root, {"nodes", "material", "subgrade", "loads", "options"}) ||
        !root.value("nodes").isObject() || !root.value("material").isObject() ||
        !root.value("subgrade").isObject() || !root.value("loads").isArray() ||
        !root.value("options").isObject()) return false;
    const auto nodes = root.value("nodes").toObject();
    const auto material = root.value("material").toObject();
    const auto subgrade = root.value("subgrade").toObject();
    const auto options = root.value("options").toObject();
    if (!hasExactKeys(nodes, {"x", "y", "z"}) || !nodes.value("x").isArray() ||
        !nodes.value("y").isArray() || !nodes.value("z").isArray() ||
        !hasExactKeys(material, {"Emod", "nu"}) ||
        !hasExactKeys(subgrade, {"Kx", "Ky", "Kz"}) ||
        !hasExactKeys(options, {"reduced_integration"}) ||
        !options.value("reduced_integration").isBool()) return false;
    bool ok = true;
    solid.xNodes = fromJsonArray(nodes.value("x").toArray(), &ok);
    solid.yNodes = fromJsonArray(nodes.value("y").toArray(), &ok);
    solid.zNodes = fromJsonArray(nodes.value("z").toArray(), &ok);
    for (const char *key : {"Emod", "nu"})
        if (!material.value(key).isDouble() || !std::isfinite(material.value(key).toDouble())) ok = false;
    for (const char *key : {"Kx", "Ky", "Kz"})
        if (!subgrade.value(key).isDouble() || !std::isfinite(subgrade.value(key).toDouble())) ok = false;
    if (!ok) return false;
    solid.material = {material.value("Emod").toDouble(), material.value("nu").toDouble()};
    solid.foundation = {subgrade.value("Kx").toDouble(), subgrade.value("Ky").toDouble(), subgrade.value("Kz").toDouble()};
    solid.reducedIntegration = options.value("reduced_integration").toBool();
    solid.coordinateConvention = "source-z-positive-toward-foundation";
    int index = 0;
    for (const auto &value : root.value("loads").toArray()) {
        if (!value.isObject()) return false;
        const auto load = value.toObject();
        if (!hasExactKeys(load, {"q", "x1", "x2", "y1", "y2"})) return false;
        PressurePatch patch;
        patch.name = QString("Imported load %1").arg(++index);
        for (const char *key : {"q", "x1", "x2", "y1", "y2"})
            if (!load.value(key).isDouble() || !std::isfinite(load.value(key).toDouble())) return false;
        patch.q = load.value("q").toDouble();
        patch.x1 = load.value("x1").toDouble(); patch.x2 = load.value("x2").toDouble();
        patch.y1 = load.value("y1").toDouble(); patch.y2 = load.value("y2").toDouble();
        solid.patches.append(std::move(patch));
    }
    return true;
}

QJsonObject patchToJson(const PressurePatch &patch)
{
    QJsonObject obj;
    obj["id"] = patch.id.toString();
    obj["name"] = patch.name;
    obj["x1"] = patch.x1;
    obj["x2"] = patch.x2;
    obj["y1"] = patch.y1;
    obj["y2"] = patch.y2;
    obj["q"] = patch.q;
    obj["enabledForAnalysis"] = patch.enabledForAnalysis;
    obj["visible"] = patch.visible;
    return obj;
}

bool patchFromJson(const QJsonObject &obj, PressurePatch *out)
{
    for (const char *key : {"x1", "x2", "y1", "y2", "q"})
        if (!obj.value(key).isDouble() || !std::isfinite(obj.value(key).toDouble()))
            return false;
    for (const char *key : {"enabledForAnalysis", "visible"})
        if (obj.contains(key) && !obj.value(key).isBool()) return false;
    QUuid id = QUuid(obj.value("id").toString());
    if (obj.contains("id") && id.isNull()) return false;
    out->id = id.isNull() ? QUuid::createUuid() : id;
    out->name = obj.value("name").toString();
    out->x1 = obj.value("x1").toDouble();
    out->x2 = obj.value("x2").toDouble();
    out->y1 = obj.value("y1").toDouble();
    out->y2 = obj.value("y2").toDouble();
    out->q = obj.value("q").toDouble();
    out->enabledForAnalysis = obj.value("enabledForAnalysis").toBool(true);
    out->visible = obj.value("visible").toBool(true);
    return true;
}

QJsonObject plateToJson(const PlateDefinition &plate)
{
    QJsonArray patches;
    for (const auto &patch : plate.patches) patches.append(patchToJson(patch));
    return {{"xNodes", toJsonArray(plate.xNodes)}, {"yNodes", toJsonArray(plate.yNodes)},
            {"slab", QJsonObject{{"Emod", plate.slab.emod}, {"nu", plate.slab.nu}, {"t", plate.slab.thickness}}},
            {"foundation", QJsonObject{{"Kx", plate.foundation.kx}, {"Ky", plate.foundation.ky}, {"Kz", plate.foundation.kz}}},
            {"patches", patches}};
}

bool readPlate(const QJsonObject &obj, PlateDefinition &plate)
{
    if (!obj.value("xNodes").isArray() || !obj.value("yNodes").isArray() ||
        !obj.value("patches").isArray() || !obj.value("slab").isObject() ||
        !obj.value("foundation").isObject()) return false;
    bool ok = true;
    plate.xNodes = fromJsonArray(obj.value("xNodes").toArray(), &ok);
    plate.yNodes = fromJsonArray(obj.value("yNodes").toArray(), &ok);
    if (!ok) return false;
    const auto slab = obj.value("slab").toObject();
    const auto foundation = obj.value("foundation").toObject();
    for (const char *key : {"Emod", "nu", "t"})
        if (!slab.value(key).isDouble() || !std::isfinite(slab.value(key).toDouble())) return false;
    for (const char *key : {"Kx", "Ky", "Kz"})
        if (!foundation.value(key).isDouble() || !std::isfinite(foundation.value(key).toDouble())) return false;
    plate.slab = {slab.value("Emod").toDouble(), slab.value("nu").toDouble(), slab.value("t").toDouble()};
    plate.foundation = {foundation.value("Kx").toDouble(), foundation.value("Ky").toDouble(), foundation.value("Kz").toDouble()};
    QSet<QUuid> ids;
    for (const auto &value : obj.value("patches").toArray()) {
        PressurePatch patch;
        if (!value.isObject() || !patchFromJson(value.toObject(), &patch) || ids.contains(patch.id)) return false;
        ids.insert(patch.id);
        plate.patches.append(patch);
    }
    return true;
}

QJsonObject solidToJson(const SolidDefinition &solid)
{
    QJsonArray patches;
    for (const auto &patch : solid.patches) patches.append(patchToJson(patch));
    return {{"xNodes", toJsonArray(solid.xNodes)}, {"yNodes", toJsonArray(solid.yNodes)},
            {"zNodes", toJsonArray(solid.zNodes)},
            {"material", QJsonObject{{"Emod", solid.material.emod}, {"nu", solid.material.nu}}},
            {"foundation", QJsonObject{{"Kx", solid.foundation.kx}, {"Ky", solid.foundation.ky}, {"Kz", solid.foundation.kz}}},
            {"patches", patches}, {"reducedIntegration", solid.reducedIntegration},
            {"coordinateConvention", solid.coordinateConvention}};
}

bool readSolid(const QJsonObject &obj, SolidDefinition &solid)
{
    if (!obj.value("xNodes").isArray() || !obj.value("yNodes").isArray() || !obj.value("zNodes").isArray() ||
        !obj.value("patches").isArray() || !obj.value("material").isObject() || !obj.value("foundation").isObject() ||
        !obj.value("reducedIntegration").isBool() || !obj.value("coordinateConvention").isString()) return false;
    bool ok = true;
    solid.xNodes = fromJsonArray(obj.value("xNodes").toArray(), &ok);
    solid.yNodes = fromJsonArray(obj.value("yNodes").toArray(), &ok);
    solid.zNodes = fromJsonArray(obj.value("zNodes").toArray(), &ok);
    if (!ok) return false;
    const auto material = obj.value("material").toObject();
    const auto foundation = obj.value("foundation").toObject();
    for (const char *key : {"Emod", "nu"})
        if (!material.value(key).isDouble() || !std::isfinite(material.value(key).toDouble())) return false;
    for (const char *key : {"Kx", "Ky", "Kz"})
        if (!foundation.value(key).isDouble() || !std::isfinite(foundation.value(key).toDouble())) return false;
    solid.material = {material.value("Emod").toDouble(), material.value("nu").toDouble()};
    solid.foundation = {foundation.value("Kx").toDouble(), foundation.value("Ky").toDouble(), foundation.value("Kz").toDouble()};
    solid.reducedIntegration = obj.value("reducedIntegration").toBool();
    solid.coordinateConvention = obj.value("coordinateConvention").toString();
    QSet<QUuid> ids;
    for (const auto &value : obj.value("patches").toArray()) {
        PressurePatch patch;
        if (!value.isObject() || !patchFromJson(value.toObject(), &patch) || ids.contains(patch.id)) return false;
        ids.insert(patch.id);
        solid.patches.append(patch);
    }
    return true;
}

QJsonObject modelSnapshotToJson(const ModelDefinition &model)
{
    const auto kind = femgui::modelKind(model);
    return {{"modelKind", modelKindId(kind)}, {"backendId", backendId(kind)},
            {"model", std::holds_alternative<PlateDefinition>(model)
                ? plateToJson(std::get<PlateDefinition>(model)) : solidToJson(std::get<SolidDefinition>(model))}};
}

bool readTypedModel(const QString &kindId, const QString &backend, const QJsonObject &obj, ModelDefinition *model)
{
    ModelKind kind;
    if (!modelKindFromId(kindId, &kind) || backend != backendId(kind)) return false;
    if (kind == ModelKind::Plate) {
        PlateDefinition plate;
        if (!readPlate(obj, plate)) return false;
        *model = std::move(plate);
    } else {
        SolidDefinition solid;
        if (!readSolid(obj, solid)) return false;
        *model = std::move(solid);
    }
    return true;
}

QJsonObject runToJson(const RunRecord &run, const QDir &projectDir)
{
    QJsonObject obj;
    obj["runId"] = run.runId;
    obj["startedAt"] = run.startedAt.toString(Qt::ISODate);
    obj["finishedAt"] = run.finishedAt.toString(Qt::ISODate);
    obj["status"] = toString(run.status);
    obj["runDirectory"] = run.runDirectory.isEmpty() ? QString() : projectDir.relativeFilePath(run.runDirectory);
    obj["resultVtkPath"] = run.resultVtkPath.isEmpty() ? QString() : projectDir.relativeFilePath(run.resultVtkPath);
    obj["modelRevisionAtLaunch"] = run.modelRevisionAtLaunch;
    obj["exitCode"] = run.exitCode;
    obj["errorMessage"] = run.errorMessage;
    obj["backendId"] = run.backendId;
    obj["modelKind"] = run.modelKind;
    if (run.hasInputSnapshot) obj["inputSnapshot"] = modelSnapshotToJson(run.inputSnapshot);
    obj["inputSha256"] = run.inputSha256;
    obj["driverSha256"] = run.driverSha256;
    obj["elementSha256"] = run.elementSha256;
    obj["pythonExecutable"] = run.pythonExecutable;
    obj["driverPath"] = run.driverPath;
    return obj;
}

RunRecord runFromJson(const QJsonObject &obj, const QDir &projectDir, int schemaVersion, bool *ok)
{
    *ok = true;
    RunRecord run;
    run.runId = obj.value("runId").toString();
    run.startedAt = QDateTime::fromString(obj.value("startedAt").toString(), Qt::ISODate);
    run.finishedAt = QDateTime::fromString(obj.value("finishedAt").toString(), Qt::ISODate);
    run.runDirectory = obj.value("runDirectory").toString();
    run.resultVtkPath = obj.value("resultVtkPath").toString();
    if (!run.runDirectory.isEmpty()) run.runDirectory = projectDir.absoluteFilePath(run.runDirectory);
    if (!run.resultVtkPath.isEmpty()) run.resultVtkPath = projectDir.absoluteFilePath(run.resultVtkPath);
    run.modelRevisionAtLaunch = obj.value("modelRevisionAtLaunch").toInt(-1);
    run.exitCode = obj.value("exitCode").toInt(-1);
    run.errorMessage = obj.value("errorMessage").toString();
    run.backendId = obj.value("backendId").toString();
    run.modelKind = obj.value("modelKind").toString();
    if (!run.backendId.isEmpty() || !run.modelKind.isEmpty()) {
        ModelKind declaredKind;
        if (!modelKindFromId(run.modelKind, &declaredKind) || run.backendId != backendId(declaredKind)) {
            *ok = false;
            return run;
        }
    }
    if (obj.value("inputSnapshot").isObject()) {
        const auto snapshot = obj.value("inputSnapshot").toObject();
        if (schemaVersion == 1) {
            PlateDefinition plate;
            run.hasInputSnapshot = readPlate(snapshot, plate);
            if (run.hasInputSnapshot) run.inputSnapshot = std::move(plate);
        } else if (snapshot.value("model").isObject()) {
            run.hasInputSnapshot = readTypedModel(snapshot.value("modelKind").toString(), snapshot.value("backendId").toString(),
                                                   snapshot.value("model").toObject(), &run.inputSnapshot);
        }
        if (run.hasInputSnapshot) {
            const auto kind = femgui::modelKind(run.inputSnapshot);
            if (run.backendId.isEmpty()) run.backendId = backendId(kind);
            if (run.modelKind.isEmpty()) run.modelKind = modelKindId(kind);
            if (run.backendId != backendId(kind) || run.modelKind != modelKindId(kind)) run.hasInputSnapshot = false;
        }
        if (!run.hasInputSnapshot) {
            *ok = false;
            return run;
        }
    } else if (obj.contains("inputSnapshot")) {
        *ok = false;
        return run;
    }
    run.inputSha256 = obj.value("inputSha256").toString();
    run.driverSha256 = obj.value("driverSha256").toString();
    run.elementSha256 = obj.value("elementSha256").toString();
    run.pythonExecutable = obj.value("pythonExecutable").toString();
    run.driverPath = obj.value("driverPath").toString();
    // Any run reopened from disk is, by construction, no longer actively
    // running (section 10.3: mark an unfinished previous run Interrupted).
    QString statusText = obj.value("status").toString();
    if (statusText == "Completed")
        run.status = RunStatus::Completed;
    else if (statusText == "Failed")
        run.status = RunStatus::Failed;
    else if (statusText == "Cancelled")
        run.status = RunStatus::Cancelled;
    else
        run.status = RunStatus::Interrupted;
    return run;
}

} // namespace

ProjectSerializer::Result ProjectSerializer::save(const ProjectDocument &document, const QString &path)
{
    Result result;
    QJsonArray runs;
    for (const RunRecord &run : document.runs())
        runs.append(runToJson(run, QFileInfo(path).absoluteDir()));

    QJsonObject project;
    project["name"] = document.name();
    project["description"] = document.description();

    QJsonObject solver;
    solver["backendId"] = document.backendId();

    QJsonObject root;
    root["schemaVersion"] = ProjectDocument::kSchemaVersion;
    root["modelKind"] = document.modelKindId();
    root["unitSystem"] = "mm-N-MPa";
    root["project"] = project;
    root["solver"] = solver;
    root["model"] = document.isPlate() ? plateToJson(document.plate()) : solidToJson(document.solid());
    root["runs"] = runs;
    root["modelRevision"] = document.modelRevision();
    root["activeRunId"] = document.hasResultSet() ? document.resultSet().runId() : QString();

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = QString("Could not open '%1' for writing: %2").arg(path, file.errorString());
        return result;
    }
    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        result.error = QString("Could not save '%1': %2").arg(path, file.errorString());
        return result;
    }
    result.success = true;
    return result;
}

ProjectSerializer::Result ProjectSerializer::load(ProjectDocument &document, const QString &path)
{
    Result result;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QString("Could not open '%1': %2").arg(path, file.errorString());
        return result;
    }
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull()) {
        result.error = QString("'%1' is not valid JSON: %2").arg(path, parseError.errorString());
        return result;
    }
    if (!doc.isObject()) {
        result.error = QString("'%1' does not contain a JSON object.").arg(path);
        return result;
    }
    QJsonObject root = doc.object();

    int schemaVersion = root.value("schemaVersion").toInt(-1);
    if (schemaVersion < 1 || root.value("schemaVersion").toDouble(-1) != schemaVersion) {
        result.error = QString("'%1' is missing a schemaVersion.").arg(path);
        return result;
    }
    if (schemaVersion > ProjectDocument::kSchemaVersion) {
        result.error = QString("'%1' was saved with a newer project schema (version %2) than this "
                                "application supports (version %3). Update the application rather than "
                                "opening this file with a version that would silently drop fields.")
                            .arg(path).arg(schemaVersion).arg(ProjectDocument::kSchemaVersion);
        return result;
    }

    if (root.value("unitSystem").toString() != "mm-N-MPa") {
        result.error = "Unsupported unit system; no conversion was performed.";
        return result;
    }

    ModelDefinition model;
    const QString kindId = root.value("modelKind").toString();
    const QString backend = root.value("solver").toObject().value("backendId").toString();
    const QJsonValue modelValue = schemaVersion == 1 ? root.value("plate") : root.value("model");
    if (!modelValue.isObject() || !readTypedModel(kindId, backend, modelValue.toObject(), &model)) {
        result.error = QString("Malformed or mismatched model/backend identity: '%1' / '%2'.").arg(kindId, backend);
        return result;
    }

    QVector<RunRecord> runs;
    if (root.contains("runs") && !root.value("runs").isArray()) {
        result.error = "Run history must be a JSON array.";
        return result;
    }
    for (const QJsonValue &v : root.value("runs").toArray()) {
        if (!v.isObject()) {
            result.error = "Malformed run history entry.";
            return result;
        }
        bool runOk = false;
        auto run = runFromJson(v.toObject(), QFileInfo(path).absoluteDir(), schemaVersion, &runOk);
        if (!runOk) {
            result.error = QString("Malformed or mismatched run/backend identity for run '%1'.").arg(run.runId);
            return result;
        }
        runs.append(std::move(run));
    }
    int revision = root.value("modelRevision").toInt(-1);
    if (revision < 0) {
        // Older files did not preserve the revision: never call an old result current.
        revision = 1;
        for (const auto &run : runs) revision = std::max(revision, run.modelRevisionAtLaunch + 1);
    }

    // Only commit to the document once the whole file has parsed
    // successfully, so a failed load never leaves a partially-replaced
    // project in memory.
    document.setName(root.value("project").toObject().value("name").toString("Untitled project"));
    document.setDescription(root.value("project").toObject().value("description").toString());
    document.replaceModelForLoad(std::move(model));
    document.restoreModelRevision(revision);
    document.replaceRuns(std::move(runs));
    document.clearResultSet();
    document.setFilePath(QFileInfo(path).absoluteFilePath());
    document.markClean();

    result.success = true;
    result.activeRunId = root.value("activeRunId").toString();
    return result;
}

ProjectSerializer::Result ProjectSerializer::loadC3D8SolverInput(SolidDefinition &solid, const QString &path)
{
    Result result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QString("Could not open '%1': %2").arg(path, file.errorString());
        return result;
    }
    if (file.size() > 16 * 1024 * 1024) {
        result.error = "C3D8 input exceeds the 16 MiB import limit.";
        return result;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    SolidDefinition parsed;
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        !readC3D8SolverInput(document.object(), parsed)) {
        result.error = error.error == QJsonParseError::NoError
            ? "The file does not match the strict C3D8 solver input schema."
            : QString("The C3D8 input is not valid JSON: %1").arg(error.errorString());
        return result;
    }
    solid = std::move(parsed);
    result.success = true;
    return result;
}

} // namespace femgui
