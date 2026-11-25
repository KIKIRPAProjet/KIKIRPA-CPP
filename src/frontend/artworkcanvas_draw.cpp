#include "artworkcanvas.h"
#include "frontendutils.h"

#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QLineF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace {
constexpr int kGridGapX = 18;
constexpr int kGridGapY = 20;
constexpr int kLabelBarHeight = 21;
constexpr int kMinCellWidthForFileName = 145;

// Côté des aperçus mis en cache : la taille affichée (zoom compris), arrondie à la puissance de deux supérieure,
// entre ces deux bornes. Une grille de 50 tuiles tient alors en quelques dizaines de Mo, et zoomer de 10 % ne
// relit pas l'image.
constexpr int kPreviewMinDimension = 256;
constexpr int kPreviewMaxDimension = 2048;
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
constexpr qreal POINT_RADIUS = 5.5;
constexpr qreal MANUAL_POINT_RADIUS = 4.8;
constexpr qreal MANUAL_LINE_WIDTH = 1.2;
constexpr qreal PAIR_HIT_TOLERANCE_PX = 5.0;   // distance d'écran à laquelle un clic touche un lien ou une jonction
const QColor MANUAL_PENDING_COLOR(251, 191, 36);     // ambre : point en attente de son correspondant dans B
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
    QPen pen(color, width, style, Qt::RoundCap, Qt::RoundJoin);
    pen.setCosmetic(true);   // même épaisseur à l'écran quel que soit le zoom
    p.setPen(pen);
    p.drawPath(path);
}

// Côté d'aperçu à lire pour le rectangle r dessiné avec ce facteur d'écran (zoom de la vue × densité de l'écran).
int previewBucket(const QRect &r, qreal screenScale)
{
    const int needed = qCeil(qMax(r.width(), r.height()) * screenScale);
    int bucket = kPreviewMinDimension;
    while (bucket < needed && bucket < kPreviewMaxDimension)
        bucket *= 2;
    return bucket;
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
    QPointF unmap(const QPointF &scenePoint) const { return (scenePoint - rect.topLeft()) / qMax(MIN_SCALE, scale); }
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

// Rayon dans le repère scène : diminue doucement à l'écran quand on zoome (plancher 4 px).
qreal scaledPointRadius(qreal baseRadius, int zoomPercent)
{
    const qreal zoomScale = qMax(1.0, zoomPercent / 100.0);
    const qreal screenRadius = qMax(4.0, baseRadius / std::pow(zoomScale, 0.25));
    return screenRadius / zoomScale;
}

// Couleur d'un point selon son écart une fois B posée sur A : vert sous 1 px, ambre jusqu'à 3 px, rose au-delà.
constexpr double GOOD_RESIDUAL_PX = 1.0;
constexpr double FAIR_RESIDUAL_PX = 3.0;
constexpr int MAX_NUMBERED_POINTS = 80;   // au-delà, les numéros encombreraient la vue
const QColor GOOD_COLOR(78, 201, 120);
const QColor REJECTED_COLOR(170, 170, 170);
const QColor SELECTED_RING(255, 255, 255);

QColor residualColor(double residual)
{
    if (residual < GOOD_RESIDUAL_PX) return GOOD_COLOR;
    if (residual <= FAIR_RESIDUAL_PX) return MANUAL_PENDING_COLOR;
    return BOUNDARY_COLOR;
}

// Points d'une paire ouverte (côte à côte ou superposée), colorés selon leur écart au décalage offset. En vue
// côte à côte, chaque point de A est relié à son point de B ; en vue superposée, le trait relie le point de A à celui
// de B posé sur A : sa longueur montre l'écart. Les points rejetés par l'appariement sont des cercles gris vides.
struct PairPointsStyle {
    bool overlay = false;
    int zoomPercent = 100;
    int selected = -1;
    bool numbered = false;
};

void drawResidualPoints(QPainter &p, const QVector<QPointF> &pointsInA, const QVector<QPointF> &pointsInB,
                        const QVector<bool> &isInlier, const QPointF &offset, const TilePlacement &placementA,
                        const TilePlacement &placementB, const PairPointsStyle &style)
{
    const int count = static_cast<int>(qMin(pointsInA.size(), pointsInB.size()));
    const int stride = style.numbered ? 1 : qMax(1, count / MAX_DRAWN_POINTS);
    const qreal radius = scaledPointRadius(POINT_RADIUS, style.zoomPercent);
    const qreal zoomScale = qMax(1.0, style.zoomPercent / 100.0);
    QFont indexFont = p.font();
    indexFont.setPointSizeF(qMax(2.0, 8.5 / zoomScale));
    p.setFont(indexFont);
    const QPointF labelOffset(radius + 2.0 / zoomScale, -radius);
    const bool numbered = style.numbered && count <= MAX_NUMBERED_POINTS;

    QPen ring(POINT_RING, LINE_WIDTH);
    ring.setCosmetic(true);
    QPen selectedRing(SELECTED_RING, BOUNDARY_WIDTH);
    selectedRing.setCosmetic(true);
    for (int index = 0; index < count; index += stride) {
        const bool inlier = index >= isInlier.size() || isInlier.at(index);
        const QPointF pointA = placementA.map(pointsInA.at(index));
        const QPointF pointB = placementB.map(pointsInB.at(index));
        const double residual = QLineF(pointsInA.at(index), pointsInB.at(index) + offset).length();
        const QColor color = inlier ? residualColor(residual) : REJECTED_COLOR;
        QPainterPath link;
        link.moveTo(pointA);
        link.lineTo(pointB);
        QColor linkColor = color;
        linkColor.setAlpha(style.overlay ? 255 : 150);
        strokePath(p, link, linkColor, LINE_WIDTH, inlier ? Qt::SolidLine : Qt::DashLine);

        const QVector<QPointF> ends = style.overlay ? QVector<QPointF>{pointA} : QVector<QPointF>{pointA, pointB};
        for (const QPointF &end : ends) {
            p.setPen(ring);
            p.setBrush(inlier ? QBrush(color) : QBrush(Qt::NoBrush));
            if (!inlier) p.setPen(QPen(REJECTED_COLOR, LINE_WIDTH));
            p.drawEllipse(end, radius, radius);
            if (index == style.selected) {
                p.setPen(selectedRing);
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(end, radius * 1.9, radius * 1.9);
            }
            if (numbered) {
                p.setPen(color);
                p.drawText(end + labelOffset, QString::number(index + 1));
            }
        }
        if (style.overlay) {   // extrémité dans B : petit cercle vide au bout du trait
            p.setPen(QPen(color, LINE_WIDTH));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(pointB, radius * 0.6, radius * 0.6);
        }
    }
    p.setBrush(Qt::NoBrush);
}

// Paire en échec : la jonction entre les deux tuiles est soulignée d'un trait rose, au milieu de l'espace qui
// les sépare (au-dessus de la bande du nom de la tuile du dessous), sur toute la longueur du bord commun.
QLineF failedJunctionLine(const QRectF &rectA, const QRectF &rectB)
{
    if (rectB.left() >= rectA.right()) {   // B à droite de A
        const qreal x = (rectA.right() + rectB.left()) / 2.0;
        return {x, qMax(rectA.top(), rectB.top()), x, qMin(rectA.bottom(), rectB.bottom())};
    }
    const qreal y = (rectA.bottom() + rectB.top() - LABEL_BAND) / 2.0;   // B sous A
    return {qMax(rectA.left(), rectB.left()), y, qMin(rectA.right(), rectB.right()), y};
}

void drawFailedJunction(QPainter &p, const QRectF &rectA, const QRectF &rectB)
{
    QPainterPath junction;
    const QLineF line = failedJunctionLine(rectA, rectB);
    junction.moveTo(line.p1());
    junction.lineTo(line.p2());
    strokePath(p, junction, BOUNDARY_COLOR, BOUNDARY_WIDTH);
}

qreal distanceToSegment(const QPointF &point, const QLineF &segment)
{
    const QPointF along = segment.p2() - segment.p1();
    const qreal lengthSquared = QPointF::dotProduct(along, along);
    const qreal t = lengthSquared > 0.0 ? qBound(0.0, QPointF::dotProduct(point - segment.p1(), along) / lengthSquared, 1.0) : 0.0;
    return QLineF(point, segment.p1() + t * along).length();
}

// Le point est-il sur un lien entre deux points appariés, ou sur la jonction d'une paire en échec ?
bool isOnPairLine(const FrontendUtils::PairAlignmentInfo &pair, const TilePlacement &placementA,
                  const TilePlacement &placementB, const QPointF &point, qreal tolerance)
{
    if (pair.totalMatches == 0)
        return distanceToSegment(point, failedJunctionLine(placementA.rect, placementB.rect)) <= tolerance;
    const int count = static_cast<int>(qMin(pair.pointsInA.size(), pair.pointsInB.size()));
    const int stride = qMax(1, count / MAX_DRAWN_POINTS);
    for (int index = 0; index < count; index += stride) {
        const QLineF link(placementA.map(pair.pointsInA.at(index)), placementB.map(pair.pointsInB.at(index)));
        if (distanceToSegment(point, link) <= tolerance)
            return true;
    }
    return false;
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

std::pair<TilePlacement, TilePlacement> focusedPairPlacements(const FrontendUtils::PairAlignmentInfo &pair,
                                                              const QRectF &area)
{
    const bool vertical = pair.direction == QObject::tr("vertical");
    const QVector<ResultTile> twoTiles{
        {0, pair.tileA, pair.imagePathA, pair.tileSizeA, 0, 0},
        {1, pair.tileB, pair.imagePathB, pair.tileSizeB, vertical ? 1 : 0, vertical ? 0 : 1}
    };
    const std::map<int, TilePlacement> placements = layoutTiles(twoTiles, area);
    return {placements.at(0), placements.at(1)};
}

std::pair<TilePlacement, TilePlacement> superposedPairPlacements(const FrontendUtils::PairAlignmentInfo &pair,
                                                                 const QRectF &area)
{
    QPointF offset(pair.dx, pair.dy);
    if (pair.totalMatches == 0 && pair.overlapInA.isEmpty() && qFuzzyIsNull(pair.dx) && qFuzzyIsNull(pair.dy)) {
        offset = (pair.direction == QObject::tr("vertical"))
                     ? QPointF(0.0, pair.tileSizeA.height() * 0.8)
                     : QPointF(pair.tileSizeA.width() * 0.8, 0.0);
    }
    const QRectF bounds = QRectF(QPointF(0, 0), QSizeF(pair.tileSizeA)) | QRectF(offset, QSizeF(pair.tileSizeB));
    const double scale = qMax(MIN_SCALE, qMin(area.width() / bounds.width(), area.height() / bounds.height()));
    const QPointF topLeftA =
        area.center() - QPointF(bounds.width(), bounds.height()) * scale / 2.0 - bounds.topLeft() * scale;
    const TilePlacement placementA{QRectF(topLeftA, QSizeF(pair.tileSizeA) * scale), scale};
    const TilePlacement placementB{QRectF(placementA.map(offset), QSizeF(pair.tileSizeB) * scale), scale};
    return {placementA, placementB};
}
}

bool ArtworkCanvas::drawMosaicPreview(QPainter &p, bool withSeams)
{
    // Les coutures des vues Ligne de couture et Image finale utilisent le repère de la mosaïque entière.
    if (m_originalMosaicPreview.isNull())
        return false;
    const QRect target = rect().adjusted(PREVIEW_MARGIN, PREVIEW_MARGIN, -PREVIEW_MARGIN, -PREVIEW_MARGIN);
    // Réduction en conservant le ratio, puis dessin centré à la taille obtenue.
    // (drawImage(target, ...) ré-étirerait l'image pour remplir tout le rectangle.)
    const QImage fitted = m_originalMosaicPreview.scaled(target.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint topLeft(target.left() + (target.width() - fitted.width()) / 2,
                         target.top() + (target.height() - fitted.height()) / 2);
    p.drawImage(topLeft, fitted);
    if (withSeams) drawSeamLines(p, QRectF(topLeft, fitted.size()), m_seamLines);
    return true;
}

void ArtworkCanvas::drawSeamOverlay(QPainter &p, const QRectF &imageRect)
{
    drawSeamLines(p, imageRect, m_seamLines);
}

bool ArtworkCanvas::drawAssemblyLayers(QPainter &p)
{
    if (imageLayers.isEmpty()) return false;
    QRectF bounds = imageLayerBounds;
    if (bounds.isEmpty()) {
        for (const ImageLayer &layer : imageLayers)
            bounds = bounds.united(layer.worldRect);
    }
    if (bounds.isEmpty()) return false;

    const QRectF area = rect().adjusted(PREVIEW_MARGIN, PREVIEW_MARGIN, -PREVIEW_MARGIN, -PREVIEW_MARGIN);
    const qreal scale = qMin(area.width() / bounds.width(), area.height() / bounds.height());
    const QSizeF size = bounds.size() * scale;
    const QRectF frame(area.center() - QPointF(size.width(), size.height()) / 2, size);
    p.save();
    p.setClipRect(frame);
    for (const ImageLayer &layer : imageLayers) {
        if (!layer.visible || layer.image.isNull()) continue;
        const QRectF target(frame.topLeft() + (layer.worldRect.topLeft() - bounds.topLeft()) * scale,
                            layer.worldRect.size() * scale);
        p.setOpacity(globalImageLayerOpacity * layer.opacity);
        p.drawImage(target, layer.image);
    }
    p.restore();
    if (seamVisible) drawSeamLines(p, frame, m_seamLines);
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
    if (pairOverlayMode && hasBackendData(currentPair)) {
        drawSuperposedPair(p);
        return;
    }
    if (m_pairIsolated && hasBackendData(currentPair)) {
        drawFocusedPair(p);
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
            else if (pass == 1) drawResidualPoints(p, pair.pointsInA, pair.pointsInB, pair.isInlier,
                                                   QPointF(pair.dx, pair.dy), placementA->second, placementB->second,
                                                   {false, zoomPercent, -1, false});
            else if (pair.totalMatches == 0) drawFailedJunction(p, placementA->second.rect, placementB->second.rect);
        }
    }
}

// Paire sous le point (repère de la vue non zoomée) : un lien ou une jonction d'abord, sinon la plus petite zone de
// recouvrement qui le contient (deux zones se chevauchent dans les coins). -1 si aucune.
int ArtworkCanvas::pairAt(const QPointF &scenePos) const
{
    const QRectF area = QRectF(rect()).adjusted(PREVIEW_MARGIN, PREVIEW_MARGIN, -PREVIEW_MARGIN, -PREVIEW_MARGIN);
    const std::map<int, TilePlacement> placements = layoutTiles(m_resultTiles, area);
    const qreal tolerance = PAIR_HIT_TOLERANCE_PX * 100.0 / qMax(1, zoomPercent);   // px d'écran, quel que soit le zoom
    int smallestZonePair = -1;
    qreal smallestZoneArea = std::numeric_limits<qreal>::max();
    for (const FrontendUtils::PairAlignmentInfo &pair : m_allPairs) {
        const auto placementA = placements.find(pair.tileIndexA);
        const auto placementB = placements.find(pair.tileIndexB);
        if (placementA == placements.end() || placementB == placements.end()) continue;
        if (isOnPairLine(pair, placementA->second, placementB->second, scenePos, tolerance)) return pair.pairIndex;
        const auto [inA, inB] = overlapAreas(pair);
        if (inA.isEmpty()) continue;
        for (const QRectF &zone : {placementA->second.map(inA), placementB->second.map(inB)}) {
            const qreal zoneArea = zone.width() * zone.height();
            if (zone.contains(scenePos) && zoneArea < smallestZoneArea) {
                smallestZonePair = pair.pairIndex;
                smallestZoneArea = zoneArea;
            }
        }
    }
    return smallestZonePair;
}

namespace {

// Décalage de B dans A pour poser une paire jamais mesurée : à 80 % de la largeur (ou de la hauteur) de A.
QPointF unmeasuredOffset(const FrontendUtils::PairAlignmentInfo &pair)
{
    return (pair.direction == QObject::tr("vertical")) ? QPointF(0.0, pair.tileSizeA.height() * 0.8)
                                                       : QPointF(pair.tileSizeA.width() * 0.8, 0.0);
}

bool isUnmeasured(const FrontendUtils::PairAlignmentInfo &pair)
{
    return pair.totalMatches == 0 && pair.overlapInA.isEmpty() && qFuzzyIsNull(pair.dx) && qFuzzyIsNull(pair.dy);
}

QRectF focusedArea(const QRect &widgetRect)
{
    return QRectF(widgetRect).adjusted(PREVIEW_MARGIN, PREVIEW_MARGIN, -PREVIEW_MARGIN, -PREVIEW_MARGIN);
}

QRectF superposedArea(const QRect &widgetRect)
{
    return QRectF(widgetRect).adjusted(PREVIEW_MARGIN, PREVIEW_MARGIN + LABEL_BAND, -PREVIEW_MARGIN, -PREVIEW_MARGIN);
}

}

FrontendUtils::PairAlignmentInfo ArtworkCanvas::displayedPair() const
{
    if (!m_edit.isActive())
        return currentPair;
    FrontendUtils::PairAlignmentInfo pair = currentPair;
    const QPointF offset = m_edit.offset();
    pair.dx = offset.x();
    pair.dy = offset.y();
    pair.totalMatches = qMax(1, m_edit.count());   // B est posée au décalage saisi, même sans point
    pair.overlapInA = QRect();                       // la zone commune suit le décalage modifié
    pair.overlapInB = QRect();
    return pair;
}

QPointF ArtworkCanvas::defaultPairOffset() const
{
    return isUnmeasured(currentPair) ? unmeasuredOffset(currentPair) : QPointF(currentPair.dx, currentPair.dy);
}

// Uniquement les 2 tuiles de la paire courante, côte à côte ou l'une sous l'autre, avec leurs points.
void ArtworkCanvas::drawFocusedPair(QPainter &p)
{
    const FrontendUtils::PairAlignmentInfo pair = displayedPair();
    const auto [placementA, placementB] = focusedPairPlacements(pair, focusedArea(rect()));
    drawPhoto(p, placementA.rect.toRect(), pair.imagePathA);
    drawPhoto(p, placementB.rect.toRect(), pair.imagePathB);
    drawTileLabel(p, placementA.rect, pair.tileA, true);
    drawTileLabel(p, placementB.rect, pair.tileB, true);
    drawPairOverlap(p, pair, placementA, placementB, false);
    if (m_edit.isActive()) {
        drawResidualPoints(p, m_edit.pointsInA(), m_edit.pointsInB(), {}, m_edit.offset(), placementA, placementB,
                           {false, zoomPercent, m_edit.selected(), true});
        drawManualPickingOverlay(p, placementA.rect, placementA.scale, placementB.rect, placementB.scale);
        return;
    }
    drawResidualPoints(p, pair.pointsInA, pair.pointsInB, pair.isInlier, QPointF(pair.dx, pair.dy), placementA,
                       placementB, {false, zoomPercent, -1, false});
    if (pair.totalMatches == 0)
        drawFailedJunction(p, placementA.rect, placementB.rect);
}

// B posée sur A à son décalage (mesuré, ou modifié pendant une correction), le tout agrandi à la taille de la vue.
// B est dessinée avec l'opacité choisie, ou en différence : ce qui coïncide devient noir.
void ArtworkCanvas::drawSuperposedPair(QPainter &p)
{
    const FrontendUtils::PairAlignmentInfo pair = displayedPair();
    const auto [placementA, placementB] = superposedPairPlacements(pair, superposedArea(rect()));
    drawPhoto(p, placementA.rect.toRect(), pair.imagePathA);
    p.save();
    if (m_pairDifference)
        p.setCompositionMode(QPainter::CompositionMode_Difference);
    else
        p.setOpacity(m_overlayOpacityPercent / 100.0);
    drawPhoto(p, placementB.rect.toRect(), pair.imagePathB);
    p.restore();
    drawPairOverlap(p, pair, placementA, placementB, true);
    if (m_edit.isActive()) {
        drawResidualPoints(p, m_edit.pointsInA(), m_edit.pointsInB(), {}, m_edit.offset(), placementA, placementB,
                           {true, zoomPercent, m_edit.selected(), true});
    } else {
        drawResidualPoints(p, pair.pointsInA, pair.pointsInB, pair.isInlier, QPointF(pair.dx, pair.dy), placementA,
                           placementB, {true, zoomPercent, -1, false});
    }
    drawTileLabel(p, placementA.rect | placementB.rect, pair.pairLabel(), true);
    if (m_edit.isActive())
        drawManualPickingOverlay(p, placementA.rect, placementA.scale, placementB.rect, placementB.scale);
}

// Point placé dans A qui attend son correspondant dans B, relié au curseur.
void ArtworkCanvas::drawManualPickingOverlay(QPainter &p, const QRectF &rectA, double scaleA,
                                             const QRectF & /*rectB*/, double /*scaleB*/)
{
    if (!m_pendingPointInA.has_value())
        return;
    p.save();
    const qreal radius = scaledPointRadius(MANUAL_POINT_RADIUS, zoomPercent);
    const QPointF pending = rectA.topLeft() + (*m_pendingPointInA) * scaleA;
    QPainterPath guide;
    guide.moveTo(pending);
    guide.lineTo(m_hoverScenePos);
    strokePath(p, guide, MANUAL_PENDING_COLOR, MANUAL_LINE_WIDTH, Qt::DashLine);
    QPen ring(POINT_RING, LINE_WIDTH);
    ring.setCosmetic(true);
    p.setPen(ring);
    p.setBrush(MANUAL_PENDING_COLOR);
    p.drawEllipse(pending, radius, radius);
    p.restore();
}

std::pair<int, bool> ArtworkCanvas::pickedPointAt(const QPointF &scenePos) const
{
    if (!m_edit.isActive() || !hasBackendData(currentPair))
        return {-1, true};
    const FrontendUtils::PairAlignmentInfo pair = displayedPair();
    const auto [placementA, placementB] = pairOverlayMode ? superposedPairPlacements(pair, superposedArea(rect()))
                                                          : focusedPairPlacements(pair, focusedArea(rect()));
    const qreal tolerance = (scaledPointRadius(POINT_RADIUS, zoomPercent) * zoomPercent / 100.0 + PAIR_HIT_TOLERANCE_PX)
                            * 100.0 / qMax(1, zoomPercent);
    int best = -1;
    bool bestInA = true;
    qreal bestDistance = tolerance;
    for (int index = 0; index < m_edit.count(); ++index) {
        const qreal distanceA = QLineF(placementA.map(m_edit.pointsInA().at(index)), scenePos).length();
        const qreal distanceB = QLineF(placementB.map(m_edit.pointsInB().at(index)), scenePos).length();
        if (distanceA <= bestDistance) {
            best = index;
            bestInA = true;
            bestDistance = distanceA;
        }
        if (distanceB < bestDistance) {
            best = index;
            bestInA = false;
            bestDistance = distanceB;
        }
    }
    return {best, bestInA};
}

void ArtworkCanvas::dragPickedPoint(const QPointF &scenePos)
{
    const FrontendUtils::PairAlignmentInfo pair = displayedPair();
    const auto [placementA, placementB] = pairOverlayMode ? superposedPairPlacements(pair, superposedArea(rect()))
                                                          : focusedPairPlacements(pair, focusedArea(rect()));
    const TilePlacement &placement = m_dragPointInA ? placementA : placementB;
    const QSize size = m_dragPointInA ? currentPair.tileSizeA : currentPair.tileSizeB;
    const QPointF point = placement.unmap(scenePos);
    m_edit.moveTo(m_dragPointIndex, m_dragPointInA,
                  QPointF(qBound(0.0, point.x(), static_cast<double>(size.width())),
                          qBound(0.0, point.y(), static_cast<double>(size.height()))));
}

bool ArtworkCanvas::handlePointPickingClick(const QPointF &scenePos)
{
    if (!hasBackendData(currentPair))
        return false;
    const FrontendUtils::PairAlignmentInfo pair = displayedPair();
    const auto [placementA, placementB] = pairOverlayMode ? superposedPairPlacements(pair, superposedArea(rect()))
                                                          : focusedPairPlacements(pair, focusedArea(rect()));
    const auto clampToTile = [](const QPointF &point, const QSize &size) {
        return QPointF(qBound(0.0, point.x(), static_cast<double>(size.width())),
                       qBound(0.0, point.y(), static_cast<double>(size.height())));
    };

    if ((!pairOverlayMode || !m_pendingPointInA.has_value()) && placementA.rect.contains(scenePos)) {
        m_pendingPointInA = clampToTile(placementA.unmap(scenePos), currentPair.tileSizeA);
        emit manualPointsChanged();
        update();
        return true;
    }
    if (m_pendingPointInA.has_value() && placementB.rect.contains(scenePos)) {
        m_edit.add(*m_pendingPointInA, clampToTile(placementB.unmap(scenePos), currentPair.tileSizeB));
        m_pendingPointInA.reset();
        emit manualPointsChanged();
        update();
        return true;
    }
    return false;
}

void ArtworkCanvas::drawEmptyState(QPainter &p)
{
    const QRect card = emptyImportCardRect();

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor bgColor = m_emptyCardHovered ? QColor("#3e4852") : QColor("#3a3a3a");
    const QColor borderColor = m_emptyCardHovered ? QColor("#4b9bea") : QColor("#6d6d6d");
    p.setBrush(bgColor);
    p.setPen(QPen(borderColor, 1.5, Qt::DashLine));
    p.drawRoundedRect(card, 10, 10);

    const int circleSize = 48;
    const QRect circleRect(card.center().x() - circleSize / 2,
                           card.top() + 34, circleSize, circleSize);
    p.setPen(Qt::NoPen);
    p.setBrush(m_emptyCardHovered ? QColor("#4b91cd") : QColor("#397fb8"));
    p.drawEllipse(circleRect);

    p.setPen(QPen(Qt::white, 2.6, Qt::SolidLine, Qt::RoundCap));
    const QPoint c = circleRect.center();
    p.drawLine(QPoint(c.x() - 10, c.y()), QPoint(c.x() + 10, c.y()));
    p.drawLine(QPoint(c.x(), c.y() - 10), QPoint(c.x(), c.y() + 10));

    QFont titleFont = p.font();
    if (titleFont.pointSize() > 0)
        titleFont.setPointSize(titleFont.pointSize() + 1);
    else if (titleFont.pixelSize() > 0)
        titleFont.setPixelSize(titleFont.pixelSize() + 2);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(QColor("#ececec"));
    p.drawText(QRect(card.left() + 16, circleRect.bottom() + 16, card.width() - 32, 24),
               Qt::AlignCenter, tr("Ajouter des images"));

    QFont subFont = p.font();
    subFont.setBold(false);
    if (subFont.pointSize() > 0)
        subFont.setPointSize(qMax(8, subFont.pointSize() - 2));
    else if (subFont.pixelSize() > 0)
        subFont.setPixelSize(qMax(10, subFont.pixelSize() - 2));
    p.setFont(subFont);
    p.setPen(QColor("#aaaaaa"));
    p.drawText(QRect(card.left() + 18, circleRect.bottom() + 42, card.width() - 36, 36),
               Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
               tr("Cliquez ici pour importer un dossier de tuiles"));

    p.restore();
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

        const bool isDragTarget = (dragStartCell >= 0 && index == dragTargetCell && index != dragStartCell);
        if (isDragTarget)
            p.fillRect(cell, QColor(75, 155, 234, 55));

        p.setPen(QPen((index == selectedGridCell || isDragTarget) ? QColor("#4b9bea") : QColor("#e8c638"),
                      (index == selectedGridCell || isDragTarget) ? 3 : 1, Qt::DashLine));
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
        // Taille à lire : celle qui sera vraiment affichée, zoom de la vue et densité de l'écran compris.
    const QTransform &world = p.worldTransform();
    const qreal screenScale = qMax(qAbs(world.m11()), qAbs(world.m12())) * devicePixelRatioF();
    const int bucket = previewBucket(r, screenScale);
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(path).arg(bucket).arg(rotation).arg(flipped ? 1 : 0);

    QImage image;
    if (const QImage *cached = m_imageCache.object(key)) {
        image = *cached;
    } else {
        image = readPreviewImage(path, QSize(bucket, bucket));
        if (!image.isNull()) {
            // Miroir et rotation appliqués une fois, à la mise en cache, et non à chaque dessin.
            if (flipped)
                image = image.mirrored(true, false);
            if (rotation != 0)
                image = image.transformed(QTransform().rotate(rotation), Qt::SmoothTransformation);
            const qsizetype cost = std::max<qsizetype>(1, image.sizeInBytes());
            m_imageCache.insert(key, new QImage(image), cost);
        }
    }
    if (image.isNull()) {
        p.fillRect(r, QColor("#262626"));
        p.setPen(QColor("#d35b55"));
        p.drawText(r, Qt::AlignCenter, tr("Image illisible"));
        return;
    }

    const QSize targetSize = image.size().scaled(r.size(), Qt::KeepAspectRatio);
    const QRect targetRect(r.left() + (r.width() - targetSize.width()) / 2,
                           r.top() + (r.height() - targetSize.height()) / 2,
                           targetSize.width(), targetSize.height());

    p.save();
    p.setClipRect(r);
    p.drawImage(targetRect, image);
    p.restore();
}
