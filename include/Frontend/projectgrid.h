#ifndef PROJECTGRID_H
#define PROJECTGRID_H

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
        QString modality = QStringLiteral("Infrarouge");
        double scaleMmPerPixel = 0.0;
        double uncertaintyMargin = 0.0;
        bool automaticUncertainty = true;
        bool gridValidated = false;
    };

    explicit ProjectGrid(QObject *parent = nullptr);

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
    void setModality(const QString &modality);
    void setScaleMmPerPixel(double scale);
    void setAutomaticUncertainty(bool automatic);
    void setUncertaintyMargin(double margin);
    void rotateSelectedTile();
    void flipSelectedTile();
    void toggleSelectedCellEmpty();

signals:
    void gridUpdated();
    void gridValidated(bool valid);
    void gridValidationCompleted(bool valid, int activeTileCount, const QStringList &errors);
    void referenceImageLoaded(const QString &path);
    void referenceImageImportRequested();
    void tilesImported(const QString &directory, int importedCount);
    void messageRequested(const QString &title, const QString &message, bool warning);
    void statusRequested(const QString &message);
    void comparisonReady(const QString &referencePath, const QString &tilePath,
                         const QImage &referenceImage, const QImage &tileImage);

private:
    State m_state;
};

#endif
