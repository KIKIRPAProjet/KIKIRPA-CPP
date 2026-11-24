#include "artworkcanvas.h"
#include "frontendutils.h"

#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>

#include <map>
#include <utility>

namespace {
constexpr int kGridGapX = 18;
constexpr int kGridGapY = 20;
constexpr int kLabelBarHeight = 21;
constexpr int kMinCellWidthForFileName = 145;

constexpr int PREVIEW_MARGIN = 24;
constexpr int MAX_DRAWN_POINTS = 500;   // par paire ; au-delà (SIFT, LightGlue), un point sur N pour rester lisible
constexpr int LABEL_BAND = 20;   // bande au-dessus de chaque tuile pour son nom : le nom ne cache pas l'image
constexpr int LABEL_PADDING = 4;
constexpr double TILE_GAP_FRACTION = 0.04;   // espace entre deux tuiles voisines, en fraction de la plus grande tuile
constexpr double MIN_SCALE = 1e-3;
constexpr qreal OVERLAY_OPACITY = 0.65;

// Langage visuel commun aux vues Alignement et Ligne de couture : des traits fins, une couleur par information, choisie
// pour ressortir sur des images en niveaux de gris sans crier. Les limites (coutures, jonction en échec) sont un peu
// plus épaisses que les liens et les zones. Toutes les paires sont dessinées de la même façon ; la paire sélectionnée
// se reconnaît au nom de ses tuiles.
constexpr qreal LINE_WIDTH = 1.0;       // liens entre points, bord des zones de recouvrement
constexpr qreal BOUNDARY_WIDTH = 1.5;   // coutures, jonction d'une paire en échec
constexpr qreal POINT_RADIUS = 2.5;
const QColor POINT_COLOR(56, 189, 248);              // bleu ciel : points appariés et leurs liens
const QColor LINK_COLOR(56, 189, 248, 190);
const QColor BOUNDARY_COLOR(251, 113, 133);          // rose corail : couture, point rejeté, jonction en échec
const QColor OVERLAP_COLOR(251, 191, 36, 200);       // ambre : bord de la zone de recouvrement
const QColor OVERLAP_FILL(251, 191, 36, 22);
const QColor POINT_RING(0, 0, 0, 110);               // fin cerne sombre : le point reste visible sur fond clair
const QColor LABEL_COLOR(168, 168, 168);
const QColor SELECTED_LABEL_COLOR(255, 255, 255);

void strokePath(QPainter &p, const QPainterPath &path, const QColor &color, qreal width,
                Qt::PenStyle style = Qt::SolidLine)
{
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color, width, style, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
}

// Coutures en vecteur, mises à l'échelle de l'image affichée.
void drawSeamLines(QPainter &p, const QRectF &imageRect, const SeamLines &seams)
{
    if (seams.isEmpty() || seams.frame.isEmpty()) return;
    QTransform toScreen = QTransform::fromTranslate(imageRect.left(), imageRect.top());
    toScreen.scale(imageRect.width() / seams.frame.width(), imageRect.height() / seams.frame.height());
    QPainterPath path;
    for (const QPolygonF &line : seams.lines) path.addPolygon(toScreen.map(line));
    strokePath(p, path, BOUNDARY_COLOR, BOUNDARY_WIDTH);
}

// Placement d'une tuile dans la vue : rectangle dessiné et facteur pleine résolution -> écran.
struct TilePlacement {
    QRectF rect;
    double scale = 1.0;
    QPointF map(const QPointF &tilePoint) const { return rect.topLeft() + tilePoint * scale; }
    QRectF map(const QRectF &tileRect) const { return {map(tileRect.topLeft()), map(tileRect.bottomRight())}; }
};

// Rectangle où drawPhoto dessine une image de taille imageSize dans cell : ratio conservé, centrée.
TilePlacement fitTile(const QRectF &cell, const QSize &imageSize)
{
    const QSizeF size = QSizeF(imageSize).scaled(cell.size(), Qt::KeepAspectRatio);
    const QRectF rect(cell.left() + (cell.width() - size.width()) / 2.0, cell.top() + (cell.height() - size.height()) / 2.0,
                      size.width(), size.height());
    return {rect, size.width() / qMax(1, imageSize.width())};
}

bool hasBackendData(const FrontendUtils::PairAlignmentInfo &pair)
{
    return !pair.imagePathA.isEmpty() && !pair.imagePathB.isEmpty() && !pair.tileSizeA.isEmpty() &&
           !pair.tileSizeB.isEmpty();
}

// Vue d'ensemble : une case par (ligne, colonne), toutes de la taille de la plus grande tuile, avec la bande du nom
// au-dessus de chaque ligne.
std::map<int, TilePlacement> layoutTiles(const QVector<ResultTile> &tiles, const QRectF &area)
{
    int rows = 1;
    int columns = 1;
    QSize cell(1, 1);
    for (const ResultTile &tile : tiles) {
        rows = qMax(rows, tile.row + 1);
        columns = qMax(columns, tile.column + 1);
        cell = cell.expandedTo(tile.size);
    }
    const double gap = TILE_GAP_FRACTION * qMax(cell.width(), cell.height());
    const QSizeF total(columns * cell.width() + (columns - 1) * gap, rows * cell.height() + (rows - 1) * gap);
    const double scale =
        qMax(MIN_SCALE, qMin(area.width() / total.width(), (area.height() - rows * LABEL_BAND) / total.height()));
    const QSizeF drawn(total.width() * scale, total.height() * scale + rows * LABEL_BAND);
    const QPointF origin(area.center().x() - drawn.width() / 2.0, area.center().y() - drawn.height() / 2.0);
    std::map<int, TilePlacement> placements;
    for (const ResultTile &tile : tiles) {
        const QPointF corner = origin + QPointF(tile.column * (cell.width() + gap) * scale,
                                                tile.row * (cell.height() + gap) * scale + (tile.row + 1) * LABEL_BAND);
        placements[tile.id] = fitTile(QRectF(corner, QSizeF(cell) * scale), tile.size);
    }
    return placements;
}

// Zone commune dans A et dans B (pixels pleine résolution) : celle estimée par overlap-estimation, sinon celle déduite
// de la translation mesurée. Vide si la paire n'a ni l'une ni l'autre.
std::pair<QRectF, QRectF> overlapAreas(const FrontendUtils::PairAlignmentInfo &pair)
{
    if (!pair.overlapInA.isEmpty() && !pair.overlapInB.isEmpty()) return {QRectF(pair.overlapInA), QRectF(pair.overlapInB)};
    if (pair.totalMatches == 0 || pair.tileSizeA.isEmpty() || pair.tileSizeB.isEmpty()) return {};
    const QRectF inA =
        QRectF(QPointF(0, 0), QSizeF(pair.tileSizeA)) & QRectF(QPointF(pair.dx, pair.dy), QSizeF(pair.tileSizeB));
    return {inA, inA.translated(-pair.dx, -pair.dy)};
}

// Zone commune de la paire dans A et dans B (dans A seulement en vue superposée) : remplissage très léger, bord fin.
void drawPairOverlap(QPainter &p, const FrontendUtils::PairAlignmentInfo &pair, const TilePlacement &placementA,
                     const TilePlacement &placementB, bool overlay)
{
    const auto [inA, inB] = overlapAreas(pair);
    if (inA.isEmpty()) return;
    QPainterPath zones;
    zones.addRect(placementA.map(inA));
    if (!overlay) zones.addRect(placementB.map(inB));
    p.fillPath(zones, OVERLAP_FILL);
    strokePath(p, zones, OVERLAP_COLOR, LINE_WIDTH);
}

// Tous les points appariés de la paire (bleu : retenus, rose : rejetés), reliés d'une tuile à l'autre.
void drawPairPoints(QPainter &p, const FrontendUtils::PairAlignmentInfo &pair, const TilePlacement &placementA,
                    const TilePlacement &placementB, bool overlay)
{
    const int count = static_cast<int>(qMin(pair.pointsInA.size(), pair.pointsInB.size()));
    const int stride = qMax(1, count / MAX_DRAWN_POINTS);
    QPainterPath retainedLinks;
    QPainterPath rejectedLinks;
    QVector<std::pair<QPointF, bool>> dots;
    for (int index = 0; index < count; index += stride) {
        const bool inlier = index >= pair.isInlier.size() || pair.isInlier.at(index);
        const QPointF pointA = placementA.map(pair.pointsInA.at(index));
        const QPointF pointB = placementB.map(pair.pointsInB.at(index));
        QPainterPath &links = inlier ? retainedLinks : rejectedLinks;
        links.moveTo(pointA);
        links.lineTo(pointB);
        dots.append({pointA, inlier});
        dots.append({pointB, inlier});
    }
    if (!overlay) {
        strokePath(p, retainedLinks, LINK_COLOR, LINE_WIDTH);
        strokePath(p, rejectedLinks, BOUNDARY_COLOR, LINE_WIDTH, Qt::DashLine);
    }
    p.setPen(QPen(POINT_RING, LINE_WIDTH));
    for (const auto &[point, inlier] : dots) {
        p.setBrush(inlier ? POINT_COLOR : BOUNDARY_COLOR);
        p.drawEllipse(point, POINT_RADIUS, POINT_RADIUS);
    }
    p.setBrush(Qt::NoBrush);
}

// Paire en échec : la jonction entre les deux tuiles est soulignée d'un trait rose, au milieu de l'espace qui
// les sépare (au-dessus de la bande du nom de la tuile du dessous), sur toute la longueur du bord commun.
void drawFailedJunction(QPainter &p, const QRectF &rectA, const QRectF &rectB)
{
    QPainterPath junction;
    if (rectB.left() >= rectA.right()) {   // B à droite de A
        const qreal x = (rectA.right() + rectB.left()) / 2.0;
        junction.moveTo(x, qMax(rectA.top(), rectB.top()));
        junction.lineTo(x, qMin(rectA.bottom(), rectB.bottom()));
    } else {                               // B sous A
        const qreal y = (rectA.bottom() + rectB.top() - LABEL_BAND) / 2.0;
        junction.moveTo(qMax(rectA.left(), rectB.left()), y);
        junction.lineTo(qMin(rectA.right(), rectB.right()), y);
    }
    strokePath(p, junction, BOUNDARY_COLOR, BOUNDARY_WIDTH);
}

// Nom au-dessus de la tuile, dans la bande réservée : rien n'est dessiné sur l'image. En blanc et en gras pour les
// deux tuiles de la paire sélectionnée. Dessiné avant les tracés, sans fond : les liens qui traversent la bande restent
// entiers.
void drawTileLabel(QPainter &p, const QRectF &tileRect, const QString &name, bool highlighted)
{
    const QRect label(qRound(tileRect.left()) + LABEL_PADDING, qRound(tileRect.top()) - LABEL_BAND,
                      qRound(tileRect.width()) - 2 * LABEL_PADDING, LABEL_BAND);
    p.save();
    QFont font = p.font();
    font.setBold(highlighted);
    p.setFont(font);
    p.setPen(highlighted ? SELECTED_LABEL_COLOR : LABEL_COLOR);
    p.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(name, Qt::ElideMiddle, label.width()));
    p.restore();
}
}

bool ArtworkCanvas::drawMosaicPreview(QPainter &p, bool withSeams)
{
    if (m_mosaicPreview.isNull())
        return false;
    const QRect target = rect().adjusted(PREVIEW_MARGIN, PREVIEW_MARGIN, -PREVIEW_MARGIN, -PREVIEW_MARGIN);
    // Réduction en conservant le ratio, puis dessin centré à la taille obtenue.
    // (drawImage(target, ...) ré-étirerait l'image pour remplir tout le rectangle.)
    const QImage fitted = m_mosaicPreview.scaled(target.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint topLeft(target.left() + (target.width() - fitted.width()) / 2,
                         target.top() + (target.height() - fitted.height()) / 2);
    p.drawImage(topLeft, fitted);
    if (withSeams) drawSeamLines(p, QRectF(topLeft, fitted.size()), m_seamLines);
    // On affiche le pourcentage de zoom en bas à gauche même pour la mosaïque
    p.setPen(QColor("#dddddd"));
    p.drawText(QRect(14, height() - 30, 160, 20), Qt::AlignLeft, QString::number(zoomPercent) + QStringLiteral(" %"));
    return true;
}

void ArtworkCanvas::drawCenteredMessage(QPainter &p, const QString &message)
{
    p.setPen(QColor("#dddddd"));
    p.drawText(rect().adjusted(PREVIEW_MARGIN, PREVIEW_MARGIN, -PREVIEW_MARGIN, -PREVIEW_MARGIN),
               Qt::AlignCenter | Qt::TextWordWrap, message);
}

void ArtworkCanvas::drawPairView(QPainter &p)
{
    if (pairOverlayMode && hasBackendData(currentPair) && currentPair.totalMatches > 0) {
        drawSuperposedPair(p);
        return;
    }
    if (m_resultTiles.isEmpty()) {
        drawCenteredMessage(p, tr("Lancez l'assemblage pour voir les tuiles et les points trouvés entre elles."));
        return;
    }
    drawTilesOverview(p);
}

void ArtworkCanvas::drawTilesOverview(QPainter &p)
{
    const QRectF area = QRectF(rect()).adjusted(PREVIEW_MARGIN, PREVIEW_MARGIN, -PREVIEW_MARGIN, -PREVIEW_MARGIN);
    const std::map<int, TilePlacement> placements = layoutTiles(m_resultTiles, area);
    const bool hasSelection = !m_allPairs.isEmpty();
    for (const ResultTile &tile : m_resultTiles) {
        drawPhoto(p, placements.at(tile.id).rect.toRect(), tile.imagePath);
        const bool inSelectedPair = hasSelection && (tile.id == currentPair.tileIndexA || tile.id == currentPair.tileIndexB);
        drawTileLabel(p, placements.at(tile.id).rect, tile.name, inSelectedPair);
    }
    // Recouvrements d'abord, puis points et liens par-dessus, puis les jonctions des paires en échec.
    for (const int pass : {0, 1, 2}) {
        for (const FrontendUtils::PairAlignmentInfo &pair : m_allPairs) {
            const auto placementA = placements.find(pair.tileIndexA);
            const auto placementB = placements.find(pair.tileIndexB);
            if (placementA == placements.end() || placementB == placements.end()) continue;
            if (pass == 0) drawPairOverlap(p, pair, placementA->second, placementB->second, false);
            else if (pass == 1) drawPairPoints(p, pair, placementA->second, placementB->second, false);
            else if (pair.totalMatches == 0) drawFailedJunction(p, placementA->second.rect, placementB->second.rect);
        }
    }
}

// B posée sur A à sa translation mesurée, le tout agrandi à la taille de la vue, le nom de la paire au-dessus.
void ArtworkCanvas::drawSuperposedPair(QPainter &p)
{
    const QRectF bounds = QRectF(QPointF(0, 0), QSizeF(currentPair.tileSizeA)) |
                          QRectF(QPointF(currentPair.dx, currentPair.dy), QSizeF(currentPair.tileSizeB));
    const QRectF area =
        QRectF(rect()).adjusted(PREVIEW_MARGIN, PREVIEW_MARGIN + LABEL_BAND, -PREVIEW_MARGIN, -PREVIEW_MARGIN);
    const double scale = qMin(area.width() / bounds.width(), area.height() / bounds.height());
    const QPointF topLeftA = area.center() - QPointF(bounds.width(), bounds.height()) * scale / 2.0 - bounds.topLeft() * scale;
    const TilePlacement placementA{QRectF(topLeftA, QSizeF(currentPair.tileSizeA) * scale), scale};
    const TilePlacement placementB{QRectF(placementA.map(QPointF(currentPair.dx, currentPair.dy)),
                                          QSizeF(currentPair.tileSizeB) * scale), scale};
    drawPhoto(p, placementA.rect.toRect(), currentPair.imagePathA);
    p.setOpacity(OVERLAY_OPACITY);
    drawPhoto(p, placementB.rect.toRect(), currentPair.imagePathB);
    p.setOpacity(1.0);
    drawPairOverlap(p, currentPair, placementA, placementB, true);
    drawPairPoints(p, currentPair, placementA, placementB, true);
    drawTileLabel(p, placementA.rect | placementB.rect, currentPair.pairLabel(), true);
}

void ArtworkCanvas::drawGrid(QPainter &p)
{
    const int rows = qMax(1, gridRows);
    const int columns = qMax(1, gridColumns);
    const int cellCount = rows * columns;
    const int gapX = kGridGapX;
    const int gapY = kGridGapY;
    const QRect area = rect().adjusted(24, 48, -24, -24);
    const int cellW = qMax(1, (area.width() - (columns - 1) * gapX) / columns);
    const int cellH = qMax(1, (area.height() - (rows - 1) * gapY) / rows);
    const int gridW = columns * cellW + (columns - 1) * gapX;
    const int gridH = rows * cellH + (rows - 1) * gapY;
    const int left = area.left() + (area.width() - gridW) / 2;
    const int top = area.top() + (area.height() - gridH) / 2;
    gridCellRects.clear();
    gridCellRects.reserve(cellCount);

    int activeTileCount = 0;
    for (int index = 0; index < qMin(cellCount, static_cast<int>(gridTiles.size())); ++index) {
        const GridTile &tile = gridTiles.at(index);
        if (!tile.path.isEmpty() && !tile.empty)
            ++activeTileCount;
    }

    for (int index = 0; index < cellCount; ++index) {
        const int row = index / columns;
        const int column = index % columns;
        const QRect cell(left + column * (cellW + gapX),
                         top + row * (cellH + gapY), cellW, cellH);
        gridCellRects.append(cell);
        const GridTile tile = index < gridTiles.size() ? gridTiles.at(index) : GridTile{};

        if (tile.path.isEmpty() && !tile.empty) {
            if (index == selectedGridCell) {
                p.setPen(QPen(QColor("#4b9bea"), 2, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRect(cell.adjusted(1, 1, -2, -2));
            }
            continue;
        }

        if (!tile.path.isEmpty()) {
            drawPhoto(p, cell, tile.path, tile.rotation, tile.flipped);
            if (tile.empty)
                p.fillRect(cell, QColor(20, 20, 20, 170));
        } else {
            p.fillRect(cell, QColor("#282828"));
        }

        p.fillRect(QRect(cell.left(), cell.bottom() - kLabelBarHeight, cell.width(), kLabelBarHeight),
                   QColor(0, 0, 0, 170));
        p.setPen(QColor("#ffffff"));
        const QString cellLabel = QStringLiteral("r%1_c%2")
            .arg(row, 2, 10, QLatin1Char('0')).arg(column, 2, 10, QLatin1Char('0'));
        p.drawText(QRect(cell.left() + 6, cell.bottom() - 20, cell.width() - 12, 19),
                   Qt::AlignLeft | Qt::AlignVCenter, cellLabel);
        if (!tile.path.isEmpty() && !tile.empty && cell.width() > kMinCellWidthForFileName) {
            const QString fileName = QFontMetrics(p.font()).elidedText(
                QFileInfo(tile.path).fileName(), Qt::ElideRight, cell.width() / 2);
            p.drawText(QRect(cell.left() + cell.width() / 2, cell.bottom() - 20,
                             cell.width() / 2 - 5, 19),
                       Qt::AlignRight | Qt::AlignVCenter, fileName);
        } else if (tile.path.isEmpty() || tile.empty) {
            p.setPen(tile.empty ? QColor("#e8c638") : QColor("#aaaaaa"));
            p.drawText(cell.adjusted(5, 4, -5, -24), Qt::AlignCenter,
                       tile.empty ? tr("CASE VIDE") : tr("Déposer une tuile ici"));
        }

        p.setPen(QPen(index == selectedGridCell ? QColor("#4b9bea") : QColor("#e8c638"),
                      index == selectedGridCell ? 3 : 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(cell.adjusted(1, 1, -2, -2));  
    }

    p.setPen(QColor("#eeeeee"));
    p.drawText(QRect(18, 12, width() - 36, 24), Qt::AlignLeft,
               tr("Grille %1 × %2 : %3 tuile(s), glisser pour permuter")
                   .arg(rows).arg(columns).arg(activeTileCount));
}

void ArtworkCanvas::drawPainting(QPainter &p, const QRect &r)
{
    p.save();
    p.setClipRect(r);
    QLinearGradient bg(r.topLeft(), r.bottomRight());
    bg.setColorAt(0, QColor("#b6b5af"));
    bg.setColorAt(.55, QColor("#777872"));
    bg.setColorAt(1, QColor("#343532"));
    p.fillRect(r, bg);
    p.fillRect(r.adjusted(0, 0, 0, -int(r.height() * .80)), QColor("#171817"));

    const double sx = r.width() / 240.0;
    const double sy = r.height() / 360.0;
    p.translate(r.topLeft());
    p.scale(sx, sy);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#393a37"));
    QPainterPath body;
    body.moveTo(34, 360);
    body.cubicTo(38, 260, 64, 214, 101, 208);
    body.cubicTo(158, 198, 205, 276, 218, 360);
    body.closeSubpath();
    p.drawPath(body);
    p.setBrush(QColor("#b8b7b0"));
    p.drawEllipse(QRectF(88, 92, 68, 96));
    p.setBrush(QColor("#222321"));
    QPainterPath hair;
    hair.moveTo(84, 142);
    hair.cubicTo(69, 76, 82, 28, 132, 32);
    hair.cubicTo(184, 36, 193, 94, 168, 147);
    hair.cubicTo(166, 99, 140, 69, 104, 91);
    hair.closeSubpath();
    p.drawPath(hair);
    p.setBrush(QColor("#d4d1c7"));
    QPainterPath hat;
    hat.moveTo(68, 88);
    hat.cubicTo(75, 38, 158, 31, 181, 75);
    hat.lineTo(201, 102);
    hat.cubicTo(151, 85, 105, 83, 68, 101);
    hat.closeSubpath();
    p.drawPath(hat);
    p.setPen(QPen(QColor("#484945"), 2));
    p.drawLine(103, 137, 119, 135);
    p.drawLine(136, 135, 151, 137);
    p.drawLine(120, 140, 116, 159);
    p.drawLine(115, 169, 132, 170);
    p.setPen(QPen(QColor("#aaa9a2"), 1));
    for (int y = 210; y < 350; y += 14) {
        p.drawLine(48, y, 198, y - 8);
    }
    p.restore();

    p.setPen(QPen(QColor("#101010"), 5));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
}

void ArtworkCanvas::drawPhoto(QPainter &p, const QRect &r, const QString &path,
                              int rotation, bool flipped)
{
    QSize previewSize = r.size();
    if (rotation % 180 != 0)
        previewSize.transpose();
    QImage image = readPreviewImage(path, previewSize);
    if (image.isNull()) {
        p.fillRect(r, QColor("#262626"));
        p.setPen(QColor("#d35b55"));
        p.drawText(r, Qt::AlignCenter, tr("Image illisible"));
        return;
    }
    if (flipped)
        image = image.mirrored(true, false);
    if (rotation != 0)
        image = image.transformed(QTransform().rotate(rotation), Qt::SmoothTransformation);
    if (image.width() > r.width() || image.height() > r.height())
        image = image.scaled(r.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    p.save();
    p.setClipRect(r);
    const int x = r.left() + (r.width() - image.width()) / 2;
    const int y = r.top() + (r.height() - image.height()) / 2;
    p.drawImage(x, y, image);
    p.restore();
    p.setPen(QPen(QColor("#101010"), 5));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
}
