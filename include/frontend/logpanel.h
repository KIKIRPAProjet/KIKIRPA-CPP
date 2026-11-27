// include/frontend/logpanel.h
/**
 * @class LogPanel
 * @brief Panneau de journalisation pour afficher les messages du backend
 */
#pragma once

#include <QDockWidget>
#include <QString>
#include <QVector>
#include <QTabWidget>

class MainWindow;
class QCheckBox;
class QTextBrowser;

class LogPanel : public QTabWidget
{
    Q_OBJECT

public:
    explicit LogPanel(MainWindow *mainWindow, QWidget *parent = nullptr);

public slots:
    void appendLog(const QString &message, const QString &level);
/**
 *  @brief reçoit un message du backend de l'étape en cours et l'ajouté à l'affichage du journal
 * @param message le message à afficher
 * @param level le niveau du message (info, warning, error)
 */

signals:
    // Demande uniquement de masquer le panneau ; les messages et le calcul sont conservés.
    void minimizeRequested();

private:
    struct LogEntry
    {
        QString message;
        QString level;
        QString time;
    };

    bool isEntryVisible(const LogEntry &entry) const;
    QString formatEntry(const LogEntry &entry) const;
    void refreshLog();

    QCheckBox *infoCheckBox = nullptr;
    QCheckBox *warningCheckBox = nullptr;
    QCheckBox *errorCheckBox = nullptr;
    QTextBrowser *textBrowser = nullptr;
    QVector<LogEntry> entries;

	MainWindow *m_mainWindow;
};
