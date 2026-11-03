/*
 * .cpp : le POURQUOI
 * - choix de conception : centraliser l'état de la grille garantit une organisation cohérente des tuiles ;
 * - cas limite : les dimensions et la cellule sélectionnée sont bornées avant utilisation ;
 * - unité ou repère : les lignes et colonnes commencent à l'index zéro ;
 * - raison non évidente : compléter la grille avec des cellules vides permet de conserver une taille stable.
 */
/*
 * Header : le QUOI
 * Gère les tuiles et leur organisation dans la grille.
 * - ce que ça fait : maintient l'état de la grille, ses dimensions et les tuiles sélectionnées ;
 * - ce que ça renvoie : l'état courant de la grille ou un signal après sa modification ;
 * - quand ça lève une exception : si la création ou la manipulation d'une tuile échoue.
 */
//  gere les tuiles et leur organisation dans la grille
#include "projectgrid.h"

#include "frontendutils.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSet>
#include <QTransform>

#include <utility>

ProjectGrid::ProjectGrid(QObject *parent)
    : QObject(parent)
{
}

const ProjectGrid::State &ProjectGrid::state() const
{
    return gridState;
}

void ProjectGrid::setState(const State &state)
{
    gridState = state;
    gridState.gridRows = qBound(1, gridState.gridRows, 20);
    gridState.gridColumns = qBound(1, gridState.gridColumns, 20);
    gridState.captureOrder = qBound(0, gridState.captureOrder, 5);
    gridState.selectedGridCell = qBound(0, gridState.selectedGridCell,
                                      gridState.gridRows * gridState.gridColumns - 1);
    while (gridState.gridTiles.size() < gridState.gridRows * gridState.gridColumns)
        gridState.gridTiles.append(GridTile{});
    emit gridUpdated();
}

void ProjectGrid::createNewProject(const QString &tileDirectory, const QStringList &tilePaths)
{
    state = State{};
    gridState.tileDirectory = tileDirectory;
    gridState.tilePaths = tilePaths;
    const int tileCount = static_cast<int>(tilePaths.size());
    gridState.gridRows = qMax(1, qMin(20, (tileCount + gridState.gridColumns - 1)
                                          / gridState.gridColumns));
    initializeGridFromPaths();
}

void ProjectGrid::initializeGridFromPaths()
{
    if (gridState.tilePaths.size() > gridState.gridRows * gridState.gridColumns) {
        const int tileCount = static_cast<int>(gridState.tilePaths.size());
        gridState.gridRows = qMin(20, (tileCount + gridState.gridColumns - 1)
                                      / gridState.gridColumns);
    }

    const int cellCount = qMax(gridState.gridRows * gridState.gridColumns,
                               static_cast<int>(gridState.tilePaths.size()));
    gridState.gridTiles = QVector<GridTile>(cellCount);
    const QVector<int> positions = captureOrderCells(
        gridState.gridRows, gridState.gridColumns, gridState.captureOrder);
    for (int index = 0; index < gridState.tilePaths.size(); ++index) {
        const int cell = index < positions.size() ? positions.at(index) : index;
        gridState.gridTiles[cell].path = gridState.tilePaths.at(index);
    }
    gridState.selectedGridCell = 0;
    gridState.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::importTiles(const QString &directoryPath)
{
    const QDir directory(directoryPath);
    const QStringList imageFilters = {QStringLiteral("*.tif"), QStringLiteral("*.tiff")};
    const QFileInfoList imageFiles = directory.entryInfoList(
        imageFilters, QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    if (imageFiles.isEmpty()) {
        emit messageRequested(tr("Aucune photo trouvée"),
            tr("Le dossier sélectionné ne contient aucune image TIFF compatible "
               "(.tif ou .tiff)."), true);
        return;
    }

    QStringList paths;
    paths.reserve(imageFiles.size());
    for (const QFileInfo &imageFile : imageFiles) {
        const QString path = imageFile.absoluteFilePath();
        if (!gridState.tilePaths.contains(path))
            paths.append(path);
    }

    if (paths.isEmpty()) {
        emit messageRequested(tr("Photos déjà importées"),
            tr("Toutes les photos de ce dossier sont déjà dans le projet."), false);
        return;
    }

    gridState.tilePaths.append(paths);
    gridState.tileDirectory = directoryPath;
    const int requiredRows = (static_cast<int>(gridState.tilePaths.size())
                              + gridState.gridColumns - 1)
                             / gridState.gridColumns;
    gridState.gridRows = qMax(gridState.gridRows, qMin(20, requiredRows));
    while (gridState.gridTiles.size() < gridState.gridRows * gridState.gridColumns)
        gridState.gridTiles.append(GridTile{});

    const QVector<int> positions = captureOrderCells(
        gridState.gridRows, gridState.gridColumns, gridState.captureOrder);
    int positionIndex = 0;
    for (const QString &path : paths) {
        while (positionIndex < positions.size() &&
               !gridState.gridTiles.at(positions.at(positionIndex)).path.isEmpty()) {
            ++positionIndex;
        }
        if (positionIndex >= positions.size()) {
            GridTile overflowTile;
            overflowTile.path = path;
            gridState.gridTiles.append(overflowTile);
            continue;
        }
        gridState.gridTiles[positions.at(positionIndex)].path = path;
        ++positionIndex;
    }

    gridState.gridValidated = false;
    emit gridUpdated();
    emit tilesImported(directoryPath, paths.size());
}

void ProjectGrid::importReferenceImage(const QString &path)
{
    if (path.isEmpty())
        return;
    if (readPreviewImage(path, QSize(600, 500)).isNull()) {
        emit messageRequested(tr("Image illisible"),
            tr("L'image de référence ne peut pas être ouverte."), true);
        return;
    }

    gridState.referenceImagePath = QFileInfo(path).absoluteFilePath();
    gridState.gridValidated = false;
    emit referenceImageLoaded(gridState.referenceImagePath);
    emit statusRequested(tr("Image de référence importée : %1").arg(QFileInfo(path).fileName()));
}

void ProjectGrid::compareReferenceImage()
{
    if (gridState.referenceImagePath.isEmpty()) {
        emit referenceImageImportRequested();
        return;
    }
    if (gridState.selectedGridCell < 0 ||
        gridState.selectedGridCell >= gridState.gridTiles.size() ||
        gridState.gridTiles.at(gridState.selectedGridCell).path.isEmpty() ||
        gridState.gridTiles.at(gridState.selectedGridCell).empty) {
        emit messageRequested(tr("Aucune tuile sélectionnée"),
            tr("Sélectionnez une tuile non vide dans la grille avant de la comparer."), false);
        return;
    }

    const QString tilePath = gridState.gridTiles.at(gridState.selectedGridCell).path;
    const QImage reference = readPreviewImage(gridState.referenceImagePath, QSize(420, 400));
    QImage tile = readPreviewImage(tilePath, QSize(420, 400));
    if (reference.isNull() || tile.isNull()) {
        emit messageRequested(tr("Comparaison impossible"),
            tr("L'image de référence ou la tuile sélectionnée ne peut pas être lue."), true);
        return;
    }

    const GridTile selectedTile = gridState.gridTiles.at(gridState.selectedGridCell);
    if (selectedTile.flipped)
        tile = tile.mirrored(true, false);
    if (selectedTile.rotation != 0)
        tile = tile.transformed(QTransform().rotate(selectedTile.rotation),
                                Qt::SmoothTransformation);
    emit comparisonReady(gridState.referenceImagePath, tilePath, reference, tile);
}

void ProjectGrid::validateGrid()
{
    QStringList errors;
    const int cellCount = gridState.gridRows * gridState.gridColumns;
    QSet<QString> assignedPaths;
    int activeTiles = 0;
    for (int index = cellCount; index < gridState.gridTiles.size(); ++index) {
        if (!gridState.gridTiles.at(index).path.isEmpty()) {
            errors.append(tr("La tuile %1 est hors de la grille.")
                              .arg(QFileInfo(gridState.gridTiles.at(index).path).fileName()));
        }
    }

    for (int index = 0;
         index < qMin(cellCount, static_cast<int>(gridState.gridTiles.size())); ++index) {
        const GridTile &tile = gridState.gridTiles.at(index);
        if (tile.path.isEmpty())
            continue;
        const QString absolutePath = QFileInfo(tile.path).absoluteFilePath();
        if (assignedPaths.contains(absolutePath)) {
            errors.append(tr("La même image est affectée à plusieurs cases : %1")
                              .arg(QFileInfo(tile.path).fileName()));
        }
        assignedPaths.insert(absolutePath);
        if (!QFileInfo::exists(tile.path))
            errors.append(tr("Fichier introuvable : %1").arg(tile.path));
        else if (readPreviewImage(tile.path, QSize(32, 32)).isNull())
            errors.append(tr("Image illisible : %1").arg(tile.path));
        if (!tile.empty)
            ++activeTiles;
    }

    for (const QString &path : gridState.tilePaths) {
        if (!assignedPaths.contains(QFileInfo(path).absoluteFilePath())) {
            errors.append(tr("Image non placée dans la grille : %1")
                              .arg(QFileInfo(path).fileName()));
        }
    }
    if (activeTiles == 0)
        errors.append(tr("La grille doit contenir au moins une tuile active."));

    gridState.gridValidated = errors.isEmpty();
    emit gridValidated(gridState.gridValidated);
    emit gridValidationCompleted(gridState.gridValidated, activeTiles, errors);
}

void ProjectGrid::swapGridTiles(int first, int second)
{
    const int cellCount = gridState.gridRows * gridState.gridColumns;
    if (first < 0 || second < 0 || first >= cellCount || second >= cellCount)
        return;
    while (gridState.gridTiles.size() <= qMax(first, second))
        gridState.gridTiles.append(GridTile{});
    std::swap(gridState.gridTiles[first], gridState.gridTiles[second]);
    gridState.selectedGridCell = second;
    gridState.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setSelectedGridCell(int cell)
{
    gridState.selectedGridCell = qBound(0, cell, gridState.gridRows * gridState.gridColumns - 1);
    emit gridUpdated();
}

void ProjectGrid::setGridRows(int rows)
{
    gridState.gridRows = qBound(1, rows, 20);
    while (gridState.gridTiles.size() < gridState.gridRows * gridState.gridColumns)
        gridState.gridTiles.append(GridTile{});
    gridState.selectedGridCell = qMin(gridState.selectedGridCell,
                                    gridState.gridRows * gridState.gridColumns - 1);
    gridState.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setGridColumns(int columns)
{
    gridState.gridColumns = qBound(1, columns, 20);
    while (gridState.gridTiles.size() < gridState.gridRows * gridState.gridColumns)
        gridState.gridTiles.append(GridTile{});
    gridState.selectedGridCell = qMin(gridState.selectedGridCell,
                                    gridState.gridRows * gridState.gridColumns - 1);
    gridState.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setCaptureOrder(int order)
{
    gridState.captureOrder = qBound(0, order, 5);
    gridState.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setHorizontalOverlap(int overlap)
{
    gridState.horizontalOverlap = qBound(0, overlap, 90);
    if (gridState.overlapsLinked)
        gridState.verticalOverlap = gridState.horizontalOverlap;
    gridState.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setVerticalOverlap(int overlap)
{
    gridState.verticalOverlap = qBound(0, overlap, 90);
    if (gridState.overlapsLinked)
        gridState.horizontalOverlap = gridState.verticalOverlap;
    gridState.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setOverlapsLinked(bool linked)
{
    gridState.overlapsLinked = linked;
    if (linked)
        gridState.verticalOverlap = gridState.horizontalOverlap;
    gridState.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setModality(const QString &modality)
{
    if (gridState.modality == modality)
        return;
    gridState.modality = modality;
    gridState.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setScaleMmPerPixel(double scale)
{
    gridState.scaleMmPerPixel = scale;
    gridState.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setAutomaticUncertainty(bool automatic)
{
    gridState.automaticUncertainty = automatic;
    gridState.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setUncertaintyMargin(double margin)
{
    gridState.uncertaintyMargin = margin;
    gridState.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::rotateSelectedTile()
{
    const int cell = gridState.selectedGridCell;
    if (cell < 0 || cell >= gridState.gridTiles.size() ||
        gridState.gridTiles.at(cell).path.isEmpty() || gridState.gridTiles.at(cell).empty) {
        emit statusRequested(tr("Sélectionnez d'abord une tuile non vide."));
        return;
    }
    gridState.gridTiles[cell].rotation = (gridState.gridTiles.at(cell).rotation + 90) % 360;
    gridState.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::flipSelectedTile()
{
    const int cell = gridState.selectedGridCell;
    if (cell < 0 || cell >= gridState.gridTiles.size() ||
        gridState.gridTiles.at(cell).path.isEmpty() || gridState.gridTiles.at(cell).empty) {
        emit statusRequested(tr("Sélectionnez d'abord une tuile non vide."));
        return;
    }
    gridState.gridTiles[cell].flipped = !gridState.gridTiles.at(cell).flipped;
    gridState.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::toggleSelectedCellEmpty()
{
    const int cell = gridState.selectedGridCell;
    if (cell < 0 || cell >= gridState.gridRows * gridState.gridColumns) {
        emit statusRequested(tr("Sélectionnez une case de la grille."));
        return;
    }
    while (gridState.gridTiles.size() <= cell)
        gridState.gridTiles.append(GridTile{});
    gridState.gridTiles[cell].empty = !gridState.gridTiles.at(cell).empty;
    gridState.gridValidated = false;
    emit gridUpdated();
}
