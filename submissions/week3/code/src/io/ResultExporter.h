#pragma once
#include <QString>
namespace femgui {
class ResultSet;
class ResultExporter
{
public:
    static bool writeCsv(const ResultSet &results, int modelRevision, bool stale, const QString &path, QString *error);
};
}
