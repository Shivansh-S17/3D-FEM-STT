#pragma once

#include <QString>
#include <QUuid>
#include <QVector>

namespace femgui {

enum class Severity { Error, Warning, Information };

// A structured validation finding: enough to route the Problems panel's
// double-click straight to the offending editor .
struct ValidationIssue
{
    Severity severity = Severity::Error;
    QString message;
    QString fieldKey;   // stable key, e.g. "slab.emod", "coordinates.x", "loads.<uuid>.q"
    QUuid entityId;      // patch id, if applicable
    QString suggestion;  // optional corrective hint
};

using ValidationIssues = QVector<ValidationIssue>;

inline bool hasErrors(const ValidationIssues &issues)
{
    for (const auto &issue : issues)
        if (issue.severity == Severity::Error)
            return true;
    return false;
}

} // namespace femgui
