// src/frontend/pair_edit_session.cpp
#include "pair_edit_session.h"

#include <QLineF>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace {

double median(std::vector<double> values)
{
    const std::size_t middle = values.size() / 2;
    std::sort(values.begin(), values.end());
    return values.size() % 2 == 1 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0;
}

}

void PairEditSession::start(const QVector<QPointF> &pointsInA, const QVector<QPointF> &pointsInB, const QPointF &offset)
{
    const auto count = std::min(pointsInA.size(), pointsInB.size());
    m_state = {pointsInA.mid(0, count), pointsInB.mid(0, count), offset};
    m_selected = -1;
    m_undo.clear();
    m_redo.clear();
    m_active = true;
}

void PairEditSession::stop()
{
    m_state = {};
    m_selected = -1;
    m_undo.clear();
    m_redo.clear();
    m_active = false;
}

QPointF PairEditSession::offset() const
{
    if (m_state.pointsInA.isEmpty())
        return m_state.typedOffset;
    std::vector<double> offsetsX;
    std::vector<double> offsetsY;
    for (int index = 0; index < count(); ++index) {
        offsetsX.push_back(m_state.pointsInA.at(index).x() - m_state.pointsInB.at(index).x());
        offsetsY.push_back(m_state.pointsInA.at(index).y() - m_state.pointsInB.at(index).y());
    }
    return {median(std::move(offsetsX)), median(std::move(offsetsY))};
}

double PairEditSession::residual(int index) const
{
    if (index < 0 || index >= count())
        return 0.0;
    return QLineF(m_state.pointsInA.at(index), m_state.pointsInB.at(index) + offset()).length();
}

QVector<int> PairEditSession::indicesByResidual() const
{
    QVector<int> indices(count());
    std::vector<double> residuals(static_cast<std::size_t>(count()));
    for (int index = 0; index < count(); ++index) {
        indices[index] = index;
        residuals[static_cast<std::size_t>(index)] = residual(index);
    }
    std::stable_sort(indices.begin(), indices.end(), [&residuals](int left, int right) {
        return residuals[static_cast<std::size_t>(left)] > residuals[static_cast<std::size_t>(right)];
    });
    return indices;
}

void PairEditSession::select(int index)
{
    m_selected = (index >= 0 && index < count()) ? index : -1;
}

void PairEditSession::add(const QPointF &pointInA, const QPointF &pointInB)
{
    record();
    m_state.pointsInA.append(pointInA);
    m_state.pointsInB.append(pointInB);
    m_selected = count() - 1;
}

void PairEditSession::remove(int index)
{
    if (index < 0 || index >= count())
        return;
    record();
    if (count() == 1)
        m_state.typedOffset = offset();   // sans point, B reste là où elle était
    m_state.pointsInA.removeAt(index);
    m_state.pointsInB.removeAt(index);
    m_selected = -1;
}

void PairEditSession::beginMove()
{
    record();
}

void PairEditSession::moveTo(int index, bool inA, const QPointF &point)
{
    if (index < 0 || index >= count())
        return;
    (inA ? m_state.pointsInA : m_state.pointsInB)[index] = point;
}

void PairEditSession::nudge(const QPointF &delta)
{
    record();
    for (QPointF &point : m_state.pointsInB)
        point -= delta;
    m_state.typedOffset += delta;
}

void PairEditSession::record()
{
    m_undo.append(m_state);
    m_redo.clear();
}

bool PairEditSession::undo()
{
    if (m_undo.isEmpty())
        return false;
    m_redo.append(m_state);
    m_state = m_undo.takeLast();
    m_selected = -1;
    return true;
}

bool PairEditSession::redo()
{
    if (m_redo.isEmpty())
        return false;
    m_undo.append(m_state);
    m_state = m_redo.takeLast();
    m_selected = -1;
    return true;
}
