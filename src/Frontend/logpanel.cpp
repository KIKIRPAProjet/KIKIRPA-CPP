#include "logpanel.h"

#include <QCheckBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWidget>

LogPanel::LogPanel(QWidget *parent)
    : QDockWidget(tr("Journal du backend"), parent)
{
    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    auto *filtersLayout = new QHBoxLayout;

    infoCheckBox = new QCheckBox(tr("Info"), content);
    warningCheckBox = new QCheckBox(tr("Avertissement"), content);
    errorCheckBox = new QCheckBox(tr("Erreur"), content);
    infoCheckBox->setChecked(true);
    warningCheckBox->setChecked(true);
    errorCheckBox->setChecked(true);

    filtersLayout->addWidget(infoCheckBox);
    filtersLayout->addWidget(warningCheckBox);
    filtersLayout->addWidget(errorCheckBox);
    filtersLayout->addStretch();
    layout->addLayout(filtersLayout);

    textBrowser = new QTextBrowser(content);
    textBrowser->setReadOnly(true);
    layout->addWidget(textBrowser);
    setWidget(content);

    connect(infoCheckBox, &QCheckBox::toggled, this, &LogPanel::refreshLog);
    connect(warningCheckBox, &QCheckBox::toggled, this, &LogPanel::refreshLog);
    connect(errorCheckBox, &QCheckBox::toggled, this, &LogPanel::refreshLog);
}

void LogPanel::appendLog(const QString &message, const QString &level)
{
    entries.append({message, level.trimmed(), QDateTime::currentDateTime().toString(
                        QStringLiteral("HH:mm:ss"))});
    const LogEntry &entry = entries.constLast();
    if (isEntryVisible(entry))
        textBrowser->append(formatEntry(entry));
}
//affichage du message recu avec l'heure exacte et le niveau du message (info, warning, error)

bool LogPanel::isEntryVisible(const LogEntry &entry) const
{
    const QString normalizedLevel = entry.level.toLower();
    if (normalizedLevel == QStringLiteral("avertissement") ||
        normalizedLevel == QStringLiteral("warning"))
        return warningCheckBox->isChecked();
    if (normalizedLevel == QStringLiteral("erreur") ||
        normalizedLevel == QStringLiteral("error"))
        return errorCheckBox->isChecked();
    return infoCheckBox->isChecked();
}

QString LogPanel::formatEntry(const LogEntry &entry) const
{
    const QString normalizedLevel = entry.level.toLower();
    const QString color = normalizedLevel == QStringLiteral("avertissement") ||
                                  normalizedLevel == QStringLiteral("warning")
                              ? QStringLiteral("#ff9800")
                          : normalizedLevel == QStringLiteral("erreur") ||
                                    normalizedLevel == QStringLiteral("error")
                              ? QStringLiteral("#f44336")
                              : QStringLiteral("#808080");
    return QStringLiteral("<span style=\"color:%1\">[%2] [%3] %4</span>")
        .arg(color, entry.time, entry.level.toHtmlEscaped(), entry.message.toHtmlEscaped());
}

void LogPanel::refreshLog()
{
    textBrowser->clear();
    for (const LogEntry &entry : entries) {
        if (isEntryVisible(entry))
            textBrowser->append(formatEntry(entry));
    }
}
