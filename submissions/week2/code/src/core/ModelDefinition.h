#pragma once

#include "core/PlateDefinition.h"
#include "core/SolidDefinition.h"

#include <variant>

namespace femgui {

enum class ModelKind { Plate, SolidC3D8 };
using ModelDefinition = std::variant<PlateDefinition, SolidDefinition>;

inline ModelKind modelKind(const ModelDefinition &model)
{
    return std::holds_alternative<PlateDefinition>(model) ? ModelKind::Plate : ModelKind::SolidC3D8;
}

inline QString modelKindId(ModelKind kind)
{
    return kind == ModelKind::Plate ? "rectangular_plate_5dof" : "structured_solid_c3d8";
}

inline QString backendId(ModelKind kind)
{
    return kind == ModelKind::Plate ? "plate-python-v1" : "c3d8-python-v1";
}

inline bool modelKindFromId(const QString &id, ModelKind *kind)
{
    if (id == "rectangular_plate_5dof") { *kind = ModelKind::Plate; return true; }
    if (id == "structured_solid_c3d8") { *kind = ModelKind::SolidC3D8; return true; }
    return false;
}

inline qint64 modelNodeCount(const ModelDefinition &model)
{
    return std::visit([](const auto &value) { return value.nodeCount(); }, model);
}

inline qint64 modelElementCount(const ModelDefinition &model)
{
    return std::visit([](const auto &value) { return value.elementCount(); }, model);
}

} // namespace femgui
