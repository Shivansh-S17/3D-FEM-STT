#include "io/ResultExporter.h"
#include "core/ResultSet.h"
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>
#include <QLocale>
#include <vtkPointData.h>
#include <vtkDataArray.h>

namespace femgui {
bool ResultExporter::writeCsv(const ResultSet &results, int revision, bool stale, const QString &path, QString *error)
{
    if (!results.isValid()) { if (error) *error = "Load a completed result before exporting."; return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
    auto quote = [](QString value) { value.replace('"', "\"\""); return '"' + value + '"'; };
    const auto fields = results.availableFields();
    const auto magnitude = results.computeDisplacementMagnitude();
    QStringList header = {"run_id", "model_revision", "result_status", "association", "node_id", "x_mm", "y_mm", "z_mm"};
    for (const auto &field : fields)
        if (field.componentCount == 1) header << quote(field.arrayName + " [" + field.unit + "]");
    QTextStream stream(&file);
    stream.setLocale(QLocale::c());
    stream << header.join(',') << '\n';
    for (vtkIdType i = 0; i < results.grid()->GetNumberOfPoints(); ++i) {
        double xyz[3]; results.grid()->GetPoint(i, xyz);
        QStringList row = {quote(results.runId()), QString::number(revision), stale ? "Previous model revision" : "Current model revision", "Node", QString::number(i)};
        for (double value : xyz) row << QString::number(value, 'g', 17);
        for (const auto &field : fields) {
            if (field.componentCount != 1) continue;
            double value = field.isDerived ? magnitude[static_cast<int>(i)]
                : results.grid()->GetPointData()->GetArray(field.arrayName.toUtf8().constData())->GetTuple1(i);
            row << QString::number(value, 'g', 17);
        }
        stream << row.join(',') << '\n';
    }
    stream.flush();
    if (stream.status() != QTextStream::Ok || !file.commit()) { if (error) *error = file.errorString(); return false; }
    return true;
}
}
