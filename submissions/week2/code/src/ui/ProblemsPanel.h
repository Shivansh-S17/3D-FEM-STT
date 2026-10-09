#pragma once

#include <QWidget>

class QTreeWidget;
class QPlainTextEdit;
class QTabWidget;

namespace femgui {

// Bottom panel: Problems / Solver Log / Output tabs.
class ProblemsPanel : public QWidget
{
    Q_OBJECT

public:
    enum class Severity { Error, Warning, Information };

    explicit ProblemsPanel(QWidget *parent = nullptr);

public slots:
    void addProblem(Severity severity, const QString &message, const QString &sourceField = {});
    void clearProblems();
    void appendSolverLog(const QString &line);
    void appendOutput(const QString &line);

signals:
    void problemCountsChanged(int errors, int warnings);
    void navigateToField(const QString &sourceField);

private:
    QTabWidget *m_tabs = nullptr;
    QTreeWidget *m_problems = nullptr;
    QPlainTextEdit *m_solverLog = nullptr;
    QPlainTextEdit *m_output = nullptr;
    int m_errorCount = 0;
    int m_warningCount = 0;
};

} // namespace femgui
