Comme demandé, les fichiers .h et .cpp ont correctement été séparés, pour l'utilisation, il faut commencer par donner l'overlap, puis le chemin vers chaque tuile. On suppose que les tuiles suivent la nomenclature ***_r**_c**.tif.

Il faut donc utiliser la fonction imagePretreatment, mettre les bons arguments (à part l'overlap et les chemins qui doivent être définis en arguments, le reste est automatiquement, il faut juste les appeler en dehors de la fonction.
Puis, il faut appeler la fonction computeSeamline (idem), et enfin extractPolygons. Cette derrière renvoie une structure qui donne les points du polygone de la seamline.
