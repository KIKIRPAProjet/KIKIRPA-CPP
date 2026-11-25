// include/frontend/pair_edit_session.h
#pragma once

#include <QPointF>
#include <QVector>

/*
 * Points d'une paire en cours de correction dans le mode Alignement, sans dépendance à l'affichage.
 * - chaque point est un couple (point dans A, point dans B), chacun dans le repère pleine résolution de sa tuile ;
 * - le décalage de B dans A est la médiane de (A - B) sur les points : il suit chaque modification et sert à poser B
 *   sur A en vue superposée. Sans point, c'est un décalage saisi (au clavier) ;
 * - l'écart d'un point est la distance entre son point dans A et son point dans B une fois B posée à ce décalage :
 *   un point faux a un grand écart ;
 * - chaque modification peut être défaite puis refaite, jusqu'à ce que la session soit arrêtée.
 */
class PairEditSession
{
public:
    void start(const QVector<QPointF> &pointsInA, const QVector<QPointF> &pointsInB, const QPointF &offset);
    void stop();
    bool isActive() const { return m_active; }

    int count() const { return static_cast<int>(m_state.pointsInA.size()); }
    const QVector<QPointF> &pointsInA() const { return m_state.pointsInA; }
    const QVector<QPointF> &pointsInB() const { return m_state.pointsInB; }
    QPointF offset() const;
    double residual(int index) const;
    // Indices des points, du plus grand écart au plus petit.
    QVector<int> indicesByResidual() const;

    int selected() const { return m_selected; }
    void select(int index);

    void add(const QPointF &pointInA, const QPointF &pointInB);
    void remove(int index);
    // Déplacement à la souris : beginMove enregistre l'état à défaire, moveTo suit la souris sans rien enregistrer.
    void beginMove();
    void moveTo(int index, bool inA, const QPointF &point);
    // Décale B de delta dans A : tous les points de B reculent de delta, le décalage avance d'autant.
    void nudge(const QPointF &delta);

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool undo();
    bool redo();

private:
    struct State {
        QVector<QPointF> pointsInA;
        QVector<QPointF> pointsInB;
        QPointF typedOffset;   // décalage utilisé quand il n'y a aucun point
    };
    void record();

    bool m_active = false;
    State m_state;
    int m_selected = -1;
    QVector<State> m_undo;
    QVector<State> m_redo;
};
