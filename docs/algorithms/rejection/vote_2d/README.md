# Vote 2D (VoteRejector)

Documentation de l'algorithme de rejet des aberrants par Vote de Hough 2D et Médiane Itérative :

📄 **[Accéder à la documentation complète : Vote2D.md](Vote2D.md)**

### Résumé rapide
- **Fichiers source** :
  - Header : [`include/backend/algorithms/rejection/vote_2d/vote_rejector.h`](../../../../include/backend/algorithms/rejection/vote_2d/vote_rejector.h)
  - Implémentation : [`src/backend/algorithms/rejection/vote_2d/vote_rejector.cpp`](../../../../src/backend/algorithms/rejection/vote_2d/vote_rejector.cpp)
- **Objectif** : Rejet déterministe en $\mathcal{O}(N)$ des faux appariements de points sans fausse rotation parasite de bras de levier sur les tuiles gigapixels.
- **Modèle physique** : Pure translation 2D ($t_x, t_y$), adapté au statif rigide motorisé du KIK-IRPA.
