#pragma once

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;
class QLabel;

namespace femgui {

// Left panel: C3D8 input-schema navigation and explicitly non-editable
// example values for the Week 1 intake prototype.
class ProjectPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectPanel(QWidget *parent = nullptr);

signals:
    void statusMessage(const QString &message);

private:
    void populateTree();
    QWidget *makePlaceholderEditor(const QString &title, const QString &description);
    void onCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);

    QTreeWidget *m_tree = nullptr;
    QStackedWidget *m_details = nullptr;
};

} // namespace femgui
