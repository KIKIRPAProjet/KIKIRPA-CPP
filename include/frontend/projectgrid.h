#pragma once

#include "project_setup_model.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QImage;

struct GridTile
{
    QString path;
    int rotation = 0;
    bool flipped = false;
    bool empty = false;
};

// Résultat de la préparation d'une pyramide en arrière-plan (une par image), remis au thread de l'interface.
struct PyramidGenerationResult
{
    QString fileName;       // nom de l'image source, pour le Journal
    QString errorMessage;   // vide si la pyramide est prête
};

class ProjectGrid : public QObject
{
    Q_OBJECT

public:
    struct State
    {
        QString tileDirectory;
        QStringList tilePaths;
        QVector<GridTile> gridTiles;
        QString referenceImagePath;
        int gridRows = 3;
        int gridColumns = 2;
        int captureOrder = 0;
        int selectedGridCell = 0;
        int horizontalOverlap = 20;
        int verticalOverlap = 20;
        bool overlapsLinked = true;
        double uncertaintyMargin = 0.0;
        bool automaticUncertainty = true;
        bool gridValidated = false;
        FrontendNs::PipelineChoices pipelineChoices;   // méthodes choisies dans l'onglet Chaîne, enregistrées avec le projet
    };

    explicit ProjectGrid(QObject *parent = nullptr);
    ~ProjectGrid() override;

    const State &state() const;
    void setState(const State &state);
    void createNewProject(const QString &tileDirectory, const QStringList &tilePaths);

    void initializeGridFromPaths();
    void importTiles(const QString &directoryPath);
    void importReferenceImage(const QString &path);
    void compareReferenceImage();
    void validateGrid();
    void swapGridTiles(int first, int second);
    void setSelectedGridCell(int cell);
    void setGridRows(int rows);
    void setGridColumns(int columns);
    void setCaptureOrder(int order);
    void setHorizontalOverlap(int overlap);
    void setVerticalOverlap(int overlap);
    void setOverlapsLinked(bool linked);
    void setAutomaticUncertainty(bool automatic);
    void setUncertaintyMargin(double margin);
    void setPipelineChoices(const FrontendNs::PipelineChoices &choices);
    void rotateSelectedTile();
    void flipSelectedTile();
    void toggleSelectedCellEmpty();
    void setPyramidCacheDirectory(const QString &directory);
    // Arrête la préparation des pyramides en arrière-plan après l'image en cours, par exemple quand le calcul démarre :
    // l'import du backend crée lui-même les pyramides manquantes, sans deux lectures d'image en même temps.
    void cancelBackgroundPyramidGeneration();

signals:
    void gridUpdated();
    void gridChanged();
    void gridValidated(bool valid);
    void gridValidationCompleted(bool valid, int activeTileCount, const QStringList &errors);
    void referenceImageLoaded(const QString &path);
    void referenceImageImportRequested();
    void tilesImported(const QString &directory, int importedCount);
    void messageRequested(const QString &title, const QString &message, bool warning);
    void statusRequested(const QString &message);
    void logRequested(const QString &message);
    void comparisonReady(const QString &referencePath, const QString &tilePath,
                         const QImage &referenceImage, const QImage &tileImage);

private:
    void startBackgroundPyramidGeneration();
    void handlePyramidResult(int resultIndex);
    void handlePyramidGenerationFinished();

    State gridState;
    QFutureWatcher<PyramidGenerationResult> pyramidWatcher;   // tâche en cours ; ses signaux arrivent dans ce thread
    int pyramidTotal = 0;      // pyramides à préparer dans la tâche en cours
    int pyramidSuccesses = 0;
    int pyramidFailures = 0;
    bool pyramidCancelRequested = false;   // annulée par cancelBackgroundPyramidGeneration (lancement du calcul)
    QString pyramidCacheDirectory;
};
