#include "dialog_helpers.h"

#include "frontendutils.h"
#include "pipeline_runner.h"

#include <QCoreApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QPixmap>
#include <QStringList>
#include <QTimer>
#include <QTransform>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
QString translate(const char *text)
{
    return QCoreApplication::translate("DialogHelpers", text);
}

// Widget d'animation des crédits avec perspective fuyante façon générique d'ouverture Star Wars.
class CreditsCrawlWidget final : public QWidget
{
public:
    explicit CreditsCrawlWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(640, 440);
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this]() {
            scrollY -= 0.5;
            if (scrollY + textLogicalHeight < -40.0)
                scrollY = static_cast<double>(height());
            update();
        });
        timer->start(16);
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        scrollY = static_cast<double>(height());
        rebuildTextBuffer();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        rebuildTextBuffer();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const int w = width();
        const int h = height();
        painter.fillRect(rect(), Qt::black);

        // Champ d'étoiles déterministe en arrière-plan.
        painter.setPen(Qt::NoPen);
        for (int i = 0; i < 110; ++i) {
            const int starX = (i * 197 + 43) % qMax(1, w);
            const int starY = (i * 313 + 71) % qMax(1, h);
            const int size = (i % 7 == 0) ? 2 : 1;
            const int brightness = 130 + (i * 37) % 125;
            painter.setBrush(QColor(brightness, brightness, brightness));
            painter.drawRect(starX, starY, size, size);
        }

        if (textBuffer.isNull())
            rebuildTextBuffer();

        // Projection en trapèze vers le haut pour reproduire la perspective inclinée.
        const QPolygonF sourceQuad({
            QPointF(0.0, 0.0),
            QPointF(static_cast<double>(w), 0.0),
            QPointF(static_cast<double>(w), static_cast<double>(h)),
            QPointF(0.0, static_cast<double>(h))
        });
        const QPolygonF targetQuad({
            QPointF(w * 0.34, h * 0.06),
            QPointF(w * 0.66, h * 0.06),
            QPointF(w * 1.15, h * 1.05),
            QPointF(-w * 0.15, h * 1.05)
        });

        QTransform perspective;
        if (QTransform::quadToQuad(sourceQuad, targetQuad, perspective)) {
            painter.save();
            painter.setTransform(perspective);
            const QRectF destination(0.0, scrollY, static_cast<double>(w), textLogicalHeight);
            painter.drawImage(destination, textBuffer);
            painter.restore();
        }

        // Fondu progressif au noir en haut pour effacer le texte dans le lointain.
        QLinearGradient fade(0.0, 0.0, 0.0, h * 0.42);
        fade.setColorAt(0.0, QColor(0, 0, 0, 255));
        fade.setColorAt(0.45, QColor(0, 0, 0, 220));
        fade.setColorAt(1.0, QColor(0, 0, 0, 0));
        painter.fillRect(QRect(0, 0, w, static_cast<int>(h * 0.42)), fade);
    }

private:
    void rebuildTextBuffer()
    {
        const int logicalWidth = qMax(320, width());
        const int scale = 2;
        const int margin = logicalWidth / 8;
        const int textWidth = logicalWidth - 2 * margin;

        const QString episodeTitle = translate("Nom in working");
        const QString subtitle = translate("CRÉDITS DU PROJET");
        const QString body = translate(
            "Projet d'assemblage d'images haute résolution\n"
            "par les étudiants de la faculté polytchnique de Mons\n"
            "Master 1 IG 2026\n\n"
            "Équipe du projet\n\n"
            "Abdellah Boumaiza\n"
            "Abdelouahed Sabbar\n"
            "Abderrahim Mghimimi\n"
            "Adam Dikri\n"
            "Adrien Poncelet\n"
            "Amory Devillez\n"
            "Anthony Debucqoy\n"
            "Christian Claus\n"
            "Eva Bovijn\n"
            "Hugo Honorez\n"
            "Jonas Spruyt\n"
            "Kevin Nya\n"
            "Ludovic Deleau\n"
            "Mathias Bartolas\n"
            "Matthias Giangreco\n"
            "Nathan Debilloez\n"
            "Nicolas Arnould\n"
            "Valérie Georis\n"
            "Zakaria Rezzouk\n\n"
            );

        QFont titleFont(QStringLiteral("Sans Serif"), 22, QFont::Bold);
        QFont subtitleFont(QStringLiteral("Sans Serif"), 28, QFont::Black);
        QFont bodyFont(QStringLiteral("Sans Serif"), 17, QFont::DemiBold);

        const QRect titleBounds = QFontMetrics(titleFont).boundingRect(
            QRect(0, 0, textWidth, 1000), Qt::AlignHCenter | Qt::TextWordWrap, episodeTitle);
        const QRect subtitleBounds = QFontMetrics(subtitleFont).boundingRect(
            QRect(0, 0, textWidth, 1000), Qt::AlignHCenter | Qt::TextWordWrap, subtitle);
        const QRect bodyBounds = QFontMetrics(bodyFont).boundingRect(
            QRect(0, 0, textWidth, 4000), Qt::AlignHCenter | Qt::TextWordWrap, body);

        textLogicalHeight = static_cast<double>(
            titleBounds.height() + 18 + subtitleBounds.height() + 36 + bodyBounds.height() + 40);

        textBuffer = QImage(logicalWidth * scale, static_cast<int>(textLogicalHeight) * scale,
                            QImage::Format_ARGB32_Premultiplied);
        textBuffer.fill(Qt::transparent);

        QPainter bufferPainter(&textBuffer);
        bufferPainter.setRenderHint(QPainter::TextAntialiasing);
        bufferPainter.scale(scale, scale);
        bufferPainter.setPen(QColor(QStringLiteral("#FFE81F")));

        int y = 0;
        bufferPainter.setFont(titleFont);
        bufferPainter.drawText(QRect(margin, y, textWidth, titleBounds.height() + 8),
                               Qt::AlignHCenter | Qt::TextWordWrap, episodeTitle);
        y += titleBounds.height() + 18;

        bufferPainter.setFont(subtitleFont);
        bufferPainter.drawText(QRect(margin, y, textWidth, subtitleBounds.height() + 8),
                               Qt::AlignHCenter | Qt::TextWordWrap, subtitle);
        y += subtitleBounds.height() + 36;

        bufferPainter.setFont(bodyFont);
        bufferPainter.drawText(QRect(margin, y, textWidth, bodyBounds.height() + 16),
                               Qt::AlignHCenter | Qt::TextWordWrap, body);
    }

    QImage textBuffer;
    double textLogicalHeight = 400.0;
    double scrollY = 440.0;
};
}

void DialogHelpers::showReferenceComparisonDialog(QWidget *parent,
                                                   const QString &referencePath,
                                                   const QString &tilePath,
                                                   const QImage &referenceImage,
                                                   const QImage &tileImage)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(translate("Comparer avec l'image de référence"));
    dialog.resize(980, 620);
    auto *layout = new QVBoxLayout(&dialog);
    auto *imagesLayout = new QHBoxLayout;
    const auto addImageColumn = [&imagesLayout](const QString &title, const QString &path,
                                                const QImage &image) {
        auto *column = new QWidget;
        auto *columnLayout = new QVBoxLayout(column);
        auto *name = new QLabel(title + QStringLiteral("\n") + QFileInfo(path).fileName(),
                                column);
        name->setWordWrap(true);
        auto *preview = new QLabel(column);
        preview->setAlignment(Qt::AlignCenter);
        preview->setMinimumSize(400, 440);
        preview->setStyleSheet(QStringLiteral("background:#202020;"));
        preview->setPixmap(QPixmap::fromImage(image));
        columnLayout->addWidget(name);
        columnLayout->addWidget(preview, 1);
        imagesLayout->addWidget(column, 1);
    };
    addImageColumn(translate("Image de référence"), referencePath, referenceImage);
    addImageColumn(translate("Tuile sélectionnée"), tilePath, tileImage);
    layout->addLayout(imagesLayout, 1);
    layout->addWidget(FrontendUtils::mutedLabel(
        translate("Comparaison visuelle côte à côte ; aucune similarité automatique n'est calculée."),
        &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void DialogHelpers::showComparisonDialog(QWidget *parent,
                                         PipelineRunnerClass *pipelineRunner)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(translate("Comparer deux exécutions"));
    dialog.resize(440, 240);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        translate("Comparer les sorties de deux algorithmes de couture."), &dialog));
    auto *algorithms = new QHBoxLayout;
    auto *a = new QComboBox(&dialog);
    auto *b = new QComboBox(&dialog);
    const QStringList choices = {
        translate("Programmation dynamique"),
        translate("Coupe de graphe"),
        translate("Dijkstra")
    };
    a->addItems(choices);
    b->addItems(choices);
    b->setCurrentIndex(1);
    algorithms->addWidget(a);
    algorithms->addWidget(b);
    layout->addLayout(algorithms);
    layout->addWidget(new QLabel(
        translate("Mesures : saut à la couture · détails conservés · durée"), &dialog));
    auto *buttons = new QDialogButtonBox(&dialog);
    auto *show = buttons->addButton(translate("Comparer"), QDialogButtonBox::ActionRole);
    auto *keepA = buttons->addButton(translate("Garder A"), QDialogButtonBox::ActionRole);
    auto *keepB = buttons->addButton(translate("Garder B"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    QObject::connect(show, &QPushButton::clicked, &dialog, [&dialog, pipelineRunner]() {
        pipelineRunner->toggleComparison();
        dialog.accept();
    });
    QObject::connect(keepA, &QPushButton::clicked, &dialog, [&dialog, pipelineRunner]() {
        pipelineRunner->keepComparisonA();
        dialog.accept();
    });
    QObject::connect(keepB, &QPushButton::clicked, &dialog, [&dialog, pipelineRunner]() {
        pipelineRunner->keepComparisonB();
        dialog.accept();
    });
    dialog.exec();
}

bool DialogHelpers::confirmRunRestart(QWidget *parent, int firstStep)
{
    QDialog confirmation(parent);
    confirmation.setWindowTitle(
        translate("Relancer à partir de l'étape %1").arg(firstStep));
    auto *layout = new QVBoxLayout(&confirmation);
    layout->addWidget(new QLabel(
        translate("Les étapes précédentes seront réutilisées sans recalcul."),
        &confirmation));
    auto *reused = new QListWidget(&confirmation);
    for (int step = 1; step < firstStep; ++step)
        reused->addItem(translate("✓  Étape %1 : résultat conservé").arg(step));
    layout->addWidget(reused);
    auto *recalculated = new QListWidget(&confirmation);
    for (int step = firstStep; step <= 8; ++step)
        recalculated->addItem(translate("↻  Étape %1 : résultat recalculé").arg(step));
    layout->addWidget(recalculated);
    layout->addWidget(FrontendUtils::mutedLabel(
        translate("Les résultats actuels seront conservés dans l'historique. "
                  "Cette maquette ne lance pas de calcul backend."),
        &confirmation));
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &confirmation);
    buttons->button(QDialogButtonBox::Ok)->setText(translate("Lancer"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted,
                     &confirmation, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected,
                     &confirmation, &QDialog::reject);
    return confirmation.exec() == QDialog::Accepted;
}

static void initCreditsLogosResource()
{
    Q_INIT_RESOURCE(credits_logos);
}

void DialogHelpers::showCreditsDialog(QWidget *parent)
{
    initCreditsLogosResource();

    QDialog dialog(parent);
    dialog.setWindowTitle(translate("Crédits"));
    dialog.resize(720, 520);
    dialog.setStyleSheet(QStringLiteral("QDialog { background:#000000; }"));

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 8);
    layout->setSpacing(6);
    layout->addWidget(new CreditsCrawlWidget(&dialog), 1);

    auto *logoBadge = new QWidget(&dialog);
    logoBadge->setStyleSheet(QStringLiteral(
        "QWidget { background:#121212; border:1px solid #2a2a2a; border-radius:6px; }"
        "QLabel { background:transparent; border:none; color:#d5d5d5; font-size:11px; }"));
    auto *logoLayout = new QHBoxLayout(logoBadge);
    logoLayout->setContentsMargins(20, 10, 20, 10);
    logoLayout->setSpacing(16);
    const QStringList logoResources = {
        QStringLiteral(":/logos/umons.png"),
        QStringLiteral(":/logos/polytech.png"),
        QStringLiteral(":/logos/faculte_sciences.png")
    };
    const QStringList logoCaptions = {
        translate("Université de Mons"),
        translate("Faculté Polytechnique"),
        translate("Faculté des Sciences")
    };
    logoLayout->addStretch(1);
    for (int i = 0; i < logoResources.size(); ++i) {
        const QPixmap pixmap(logoResources.at(i));
        if (pixmap.isNull())
            continue;
        auto *logoLabel = new QLabel(logoBadge);
        logoLabel->setAlignment(Qt::AlignCenter);
        logoLabel->setPixmap(pixmap.scaledToHeight(42, Qt::SmoothTransformation));
        logoLabel->setToolTip(logoCaptions.at(i));
        logoLayout->addWidget(logoLabel);
        logoLayout->addStretch(1);
    }

    auto *logoRow = new QHBoxLayout;
    logoRow->setContentsMargins(12, 0, 12, 0);
    logoRow->addWidget(logoBadge, 1);
    layout->addLayout(logoRow);

    dialog.exec();
}
