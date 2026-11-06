/**
 * @class LogPanel
 * @brief Panneau de journalisation pour afficher les messages du backend
 */
#ifndef LOGPANEL_H
#define LOGPANEL_H

#include <QDockWidget>
#include <QString>
#include <QVector>

class QCheckBox;
class QTextBrowser;

class LogPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit LogPanel(QWidget *parent = nullptr);

public slots:
    void appendLog(const QString &message, const QString &level);
/**
 *  @brief reçoit un message du backend de l'étape en cours et l'ajouté à l'affichage du journal
 * @param message le message à afficher
 * @param level le niveau du message (info, warning, error)
 */

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
};

#endif
