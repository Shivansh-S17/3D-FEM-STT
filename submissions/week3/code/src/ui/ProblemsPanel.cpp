#include "ui/ProblemsPanel.h"

#include <QDateTime>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QStyle>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace femgui {

ProblemsPanel::ProblemsPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tabs = new QTabWidget(this);

    m_problems = new QTreeWidget(m_tabs);
    m_problems->setHeaderLabels({tr("Severity"), tr("Message"), tr("Go to")});
    m_problems->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    connect(m_problems, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        emit navigateToField(item->data(0, Qt::UserRole).toString());
    });
    m_tabs->addTab(m_problems, tr("Problems"));

    m_solverLog = new QPlainTextEdit(m_tabs);
    m_solverLog->setReadOnly(true);
    m_solverLog->setMaximumBlockCount(2000);
    m_solverLog->setPlaceholderText(tr("Solver progress and convergence messages appear here."));
    m_tabs->addTab(m_solverLog, tr("Solver Log"));

    m_output = new QPlainTextEdit(m_tabs);
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(2000);
    m_output->setPlaceholderText(tr("Import, export, and application diagnostics appear here."));
    m_tabs->addTab(m_output, tr("Output"));

    layout->addWidget(m_tabs);
}

void ProblemsPanel::addProblem(Severity severity, const QString &message, const QString &sourceField)
{
    QString severityText;
    QIcon icon;
    switch (severity) {
    case Severity::Error:
        severityText = tr("Error");
        icon = style()->standardIcon(QStyle::SP_MessageBoxCritical);
        ++m_errorCount;
        break;
    case Severity::Warning:
        severityText = tr("Warning");
        icon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
        ++m_warningCount;
        break;
    case Severity::Information:
        severityText = tr("Information");
        icon = style()->standardIcon(QStyle::SP_MessageBoxInformation);
        break;
    }

    QString destination = tr("Overview");
    if (sourceField.startsWith("material") || sourceField.startsWith("slab")) destination = tr("Material");
    else if (sourceField.startsWith("foundation")) destination = tr("Foundation");
    else if (sourceField.startsWith("coordinates") || sourceField.startsWith("mesh") || sourceField.startsWith("options")) destination = tr("Model geometry");
    else if (sourceField.startsWith("loads")) destination = tr("Pressure loads");
    else if (sourceField.startsWith("backend")) destination = tr("Advanced backend");
    else if (sourceField.startsWith("run")) destination = tr("Validate & Run");
    auto *item = new QTreeWidgetItem(m_problems, QStringList{severityText, message, destination});
    item->setIcon(0, icon);
    item->setData(0, Qt::UserRole, sourceField);
    item->setToolTip(1, tr("Double-click to open the related input"));

    m_tabs->setTabText(0, tr("Problems (%1)").arg(m_problems->topLevelItemCount()));
    emit problemCountsChanged(m_errorCount, m_warningCount);
}

void ProblemsPanel::clearProblems()
{
    m_problems->clear();
    m_errorCount = 0;
    m_warningCount = 0;
    m_tabs->setTabText(0, tr("Problems"));
    emit problemCountsChanged(0, 0);
}

void ProblemsPanel::appendSolverLog(const QString &line)
{
    const QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_solverLog->appendPlainText(QString("[%1] %2").arg(timestamp, line));
}

void ProblemsPanel::appendOutput(const QString &line)
{
    const QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_output->appendPlainText(QString("[%1] %2").arg(timestamp, line));
}

} // namespace femgui
