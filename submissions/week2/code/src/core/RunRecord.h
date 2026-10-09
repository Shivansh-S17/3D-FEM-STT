#pragma once

#include <QDateTime>
#include <QString>
#include <QUuid>
#include "core/ModelDefinition.h"

namespace femgui {

enum class RunStatus { Idle, Validating, Starting, Running, Cancelling, Cancelled, LoadingResults, Completed, Failed, Interrupted };

inline QString toString(RunStatus status)
{
    switch (status) {
    case RunStatus::Idle: return "Idle";
    case RunStatus::Validating: return "Validating";
    case RunStatus::Starting: return "Starting";
    case RunStatus::Running: return "Running";
    case RunStatus::Cancelling: return "Cancelling";
    case RunStatus::Cancelled: return "Cancelled";
    case RunStatus::LoadingResults: return "Loading Results";
    case RunStatus::Completed: return "Completed";
    case RunStatus::Failed: return "Failed";
    case RunStatus::Interrupted: return "Interrupted";
    }
    return "Unknown";
}

// One solver invocation's provenance . The run directory
// holds input.json, run.json, stdout.log, stderr.log, and the driver's own
// output_python_square/ artifacts.
struct RunRecord
{
    QString runId;
    QDateTime startedAt;
    QDateTime finishedAt;
    RunStatus status = RunStatus::Idle;
    QString runDirectory;
    QString resultVtkPath;
    int modelRevisionAtLaunch = -1;
    int exitCode = -1;
    QString errorMessage;
    QString backendId;
    QString modelKind;
    ModelDefinition inputSnapshot;
    bool hasInputSnapshot = false;
    QString inputSha256;
    QString driverSha256;
    QString elementSha256;
    QString pythonExecutable;
    QString driverPath;
};

} // namespace femgui
