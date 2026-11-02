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
    return m_state;
}

void ProjectGrid::setState(const State &state)
{
    m_state = state;
    m_state.gridRows = qBound(1, m_state.gridRows, 20);
    m_state.gridColumns = qBound(1, m_state.gridColumns, 20);
    m_state.captureOrder = qBound(0, m_state.captureOrder, 5);
    m_state.selectedGridCell = qBound(0, m_state.selectedGridCell,
                                      m_state.gridRows * m_state.gridColumns - 1);
    while (m_state.gridTiles.size() < m_state.gridRows * m_state.gridColumns)
        m_state.gridTiles.append(GridTile{});
    emit gridUpdated();
}

void ProjectGrid::createNewProject(const QString &tileDirectory, const QStringList &tilePaths)
{
    m_state = State{};
    m_state.tileDirectory = tileDirectory;
    m_state.tilePaths = tilePaths;
    const int tileCount = static_cast<int>(tilePaths.size());
    m_state.gridRows = qMax(1, qMin(20, (tileCount + m_state.gridColumns - 1)
                                          / m_state.gridColumns));
    initializeGridFromPaths();
}

void ProjectGrid::initializeGridFromPaths()
{
    if (m_state.tilePaths.size() > m_state.gridRows * m_state.gridColumns) {
        const int tileCount = static_cast<int>(m_state.tilePaths.size());
        m_state.gridRows = qMin(20, (tileCount + m_state.gridColumns - 1)
                                      / m_state.gridColumns);
    }

    const int cellCount = qMax(m_state.gridRows * m_state.gridColumns,
                               static_cast<int>(m_state.tilePaths.size()));
    m_state.gridTiles = QVector<GridTile>(cellCount);
    const QVector<int> positions = captureOrderCells(
        m_state.gridRows, m_state.gridColumns, m_state.captureOrder);
    for (int index = 0; index < m_state.tilePaths.size(); ++index) {
        const int cell = index < positions.size() ? positions.at(index) : index;
        m_state.gridTiles[cell].path = m_state.tilePaths.at(index);
    }
    m_state.selectedGridCell = 0;
    m_state.gridValidated = false;
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
        if (!m_state.tilePaths.contains(path))
            paths.append(path);
    }

    if (paths.isEmpty()) {
        emit messageRequested(tr("Photos déjà importées"),
            tr("Toutes les photos de ce dossier sont déjà dans le projet."), false);
        return;
    }

    m_state.tilePaths.append(paths);
    m_state.tileDirectory = directoryPath;
    const int requiredRows = (static_cast<int>(m_state.tilePaths.size())
                              + m_state.gridColumns - 1)
                             / m_state.gridColumns;
    m_state.gridRows = qMax(m_state.gridRows, qMin(20, requiredRows));
    while (m_state.gridTiles.size() < m_state.gridRows * m_state.gridColumns)
        m_state.gridTiles.append(GridTile{});

    const QVector<int> positions = captureOrderCells(
        m_state.gridRows, m_state.gridColumns, m_state.captureOrder);
    int positionIndex = 0;
    for (const QString &path : paths) {
        while (positionIndex < positions.size() &&
               !m_state.gridTiles.at(positions.at(positionIndex)).path.isEmpty()) {
            ++positionIndex;
        }
        if (positionIndex >= positions.size()) {
            GridTile overflowTile;
            overflowTile.path = path;
            m_state.gridTiles.append(overflowTile);
            continue;
        }
        m_state.gridTiles[positions.at(positionIndex)].path = path;
        ++positionIndex;
    }

    m_state.gridValidated = false;
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

    m_state.referenceImagePath = QFileInfo(path).absoluteFilePath();
    m_state.gridValidated = false;
    emit referenceImageLoaded(m_state.referenceImagePath);
    emit statusRequested(tr("Image de référence importée : %1").arg(QFileInfo(path).fileName()));
}

void ProjectGrid::compareReferenceImage()
{
    if (m_state.referenceImagePath.isEmpty()) {
        emit referenceImageImportRequested();
        return;
    }
    if (m_state.selectedGridCell < 0 ||
        m_state.selectedGridCell >= m_state.gridTiles.size() ||
        m_state.gridTiles.at(m_state.selectedGridCell).path.isEmpty() ||
        m_state.gridTiles.at(m_state.selectedGridCell).empty) {
        emit messageRequested(tr("Aucune tuile sélectionnée"),
            tr("Sélectionnez une tuile non vide dans la grille avant de la comparer."), false);
        return;
    }

    const QString tilePath = m_state.gridTiles.at(m_state.selectedGridCell).path;
    const QImage reference = readPreviewImage(m_state.referenceImagePath, QSize(420, 400));
    QImage tile = readPreviewImage(tilePath, QSize(420, 400));
    if (reference.isNull() || tile.isNull()) {
        emit messageRequested(tr("Comparaison impossible"),
            tr("L'image de référence ou la tuile sélectionnée ne peut pas être lue."), true);
        return;
    }

    const GridTile selectedTile = m_state.gridTiles.at(m_state.selectedGridCell);
    if (selectedTile.flipped)
        tile = tile.mirrored(true, false);
    if (selectedTile.rotation != 0)
        tile = tile.transformed(QTransform().rotate(selectedTile.rotation),
                                Qt::SmoothTransformation);
    emit comparisonReady(m_state.referenceImagePath, tilePath, reference, tile);
}

void ProjectGrid::validateGrid()
{
    QStringList errors;
    const int cellCount = m_state.gridRows * m_state.gridColumns;
    QSet<QString> assignedPaths;
    int activeTiles = 0;
    for (int index = cellCount; index < m_state.gridTiles.size(); ++index) {
        if (!m_state.gridTiles.at(index).path.isEmpty()) {
            errors.append(tr("La tuile %1 est hors de la grille.")
                              .arg(QFileInfo(m_state.gridTiles.at(index).path).fileName()));
        }
    }

    for (int index = 0;
         index < qMin(cellCount, static_cast<int>(m_state.gridTiles.size())); ++index) {
        const GridTile &tile = m_state.gridTiles.at(index);
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

    for (const QString &path : m_state.tilePaths) {
        if (!assignedPaths.contains(QFileInfo(path).absoluteFilePath())) {
            errors.append(tr("Image non placée dans la grille : %1")
                              .arg(QFileInfo(path).fileName()));
        }
    }
    if (activeTiles == 0)
        errors.append(tr("La grille doit contenir au moins une tuile active."));

    m_state.gridValidated = errors.isEmpty();
    emit gridValidated(m_state.gridValidated);
    emit gridValidationCompleted(m_state.gridValidated, activeTiles, errors);
}

void ProjectGrid::swapGridTiles(int first, int second)
{
    const int cellCount = m_state.gridRows * m_state.gridColumns;
    if (first < 0 || second < 0 || first >= cellCount || second >= cellCount)
        return;
    while (m_state.gridTiles.size() <= qMax(first, second))
        m_state.gridTiles.append(GridTile{});
    std::swap(m_state.gridTiles[first], m_state.gridTiles[second]);
    m_state.selectedGridCell = second;
    m_state.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setSelectedGridCell(int cell)
{
    m_state.selectedGridCell = qBound(0, cell, m_state.gridRows * m_state.gridColumns - 1);
    emit gridUpdated();
}

void ProjectGrid::setGridRows(int rows)
{
    m_state.gridRows = qBound(1, rows, 20);
    while (m_state.gridTiles.size() < m_state.gridRows * m_state.gridColumns)
        m_state.gridTiles.append(GridTile{});
    m_state.selectedGridCell = qMin(m_state.selectedGridCell,
                                    m_state.gridRows * m_state.gridColumns - 1);
    m_state.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setGridColumns(int columns)
{
    m_state.gridColumns = qBound(1, columns, 20);
    while (m_state.gridTiles.size() < m_state.gridRows * m_state.gridColumns)
        m_state.gridTiles.append(GridTile{});
    m_state.selectedGridCell = qMin(m_state.selectedGridCell,
                                    m_state.gridRows * m_state.gridColumns - 1);
    m_state.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setCaptureOrder(int order)
{
    m_state.captureOrder = qBound(0, order, 5);
    m_state.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setHorizontalOverlap(int overlap)
{
    m_state.horizontalOverlap = qBound(0, overlap, 90);
    if (m_state.overlapsLinked)
        m_state.verticalOverlap = m_state.horizontalOverlap;
    m_state.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setVerticalOverlap(int overlap)
{
    m_state.verticalOverlap = qBound(0, overlap, 90);
    if (m_state.overlapsLinked)
        m_state.horizontalOverlap = m_state.verticalOverlap;
    m_state.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setOverlapsLinked(bool linked)
{
    m_state.overlapsLinked = linked;
    if (linked)
        m_state.verticalOverlap = m_state.horizontalOverlap;
    m_state.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setModality(const QString &modality)
{
    if (m_state.modality == modality)
        return;
    m_state.modality = modality;
    m_state.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setScaleMmPerPixel(double scale)
{
    m_state.scaleMmPerPixel = scale;
    m_state.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setAutomaticUncertainty(bool automatic)
{
    m_state.automaticUncertainty = automatic;
    m_state.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::setUncertaintyMargin(double margin)
{
    m_state.uncertaintyMargin = margin;
    m_state.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::rotateSelectedTile()
{
    const int cell = m_state.selectedGridCell;
    if (cell < 0 || cell >= m_state.gridTiles.size() ||
        m_state.gridTiles.at(cell).path.isEmpty() || m_state.gridTiles.at(cell).empty) {
        emit statusRequested(tr("Sélectionnez d'abord une tuile non vide."));
        return;
    }
    m_state.gridTiles[cell].rotation = (m_state.gridTiles.at(cell).rotation + 90) % 360;
    m_state.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::flipSelectedTile()
{
    const int cell = m_state.selectedGridCell;
    if (cell < 0 || cell >= m_state.gridTiles.size() ||
        m_state.gridTiles.at(cell).path.isEmpty() || m_state.gridTiles.at(cell).empty) {
        emit statusRequested(tr("Sélectionnez d'abord une tuile non vide."));
        return;
    }
    m_state.gridTiles[cell].flipped = !m_state.gridTiles.at(cell).flipped;
    m_state.gridValidated = false;
    emit gridUpdated();
}

void ProjectGrid::toggleSelectedCellEmpty()
{
    const int cell = m_state.selectedGridCell;
    if (cell < 0 || cell >= m_state.gridRows * m_state.gridColumns) {
        emit statusRequested(tr("Sélectionnez une case de la grille."));
        return;
    }
    while (m_state.gridTiles.size() <= cell)
        m_state.gridTiles.append(GridTile{});
    m_state.gridTiles[cell].empty = !m_state.gridTiles.at(cell).empty;
    m_state.gridValidated = false;
    emit gridUpdated();
}
