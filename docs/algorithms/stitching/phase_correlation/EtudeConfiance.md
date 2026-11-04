# Étude : indicateur de confiance de la corrélation de phase

**Question.** Sur les fonds sombres et les motifs répétitifs, peut-on savoir si une translation mesurée par `CorrelationPhase` est fiable ou ambiguë ?

**Réponse courte.**
- Le seul critère actuel, le **rapport pic / second pic ≥ 1,5**, laisse passer **20 mesures fausses sur 111** dans ce banc.
- En ajoutant deux contrôles sur les points locaux déjà calculés (**NCC médiane ≥ 0,3** et **dispersion ≤ 0,5 px**), on n'en laisse plus passer que 11, et toutes les erreurs sur fond sombre sont rattrapées.
- Avec en plus l'**auto-similarité** de la bande (nouvel indicateur), on descend à 6 : les motifs répétés sont signalés comme ambigus.
- Cette règle est **branchée dans `PhaseCorrelationStep`** (§6).
- Les 6 qui restent sont un cas **qu'aucun indicateur calculé sur la paire seule ne peut détecter** (§5).

---

## 1. Indicateurs comparés

| Indicateur | D'où il vient | Sens |
|---|---|---|
| `rapport` | Première passe (`OverlapEstimate::peakRatio`) | Pic / second pic. Proche de 1 : deux positions se valent. Seuil actuel : 1,5 |
| `ncc` | Médiane de `Matches::confidenceScores` | Ressemblance des patchs locaux une fois recalés (0 à 1) |
| `disp` | Médiane de \|`pointsA − pointsB − (dx, dy)`\| | Les points locaux sont-ils d'accord avec la translation globale ? (px) |
| `autosim` | **Nouveau** : `PhaseCorrelationStepClass::selfSimilarity` | La bande de A ressemble-t-elle à elle-même à un décalage non nul dans la marge ? 1 = motif répété, 0 = aucune répétition |

`autosim` : bande de A en passe-haut (image − flou gaussien σ = 4 px), écrêtée à ±3 écarts robustes pour qu'un bord net ne domine pas, puis corrélation normalisée de sa partie centrale avec la bande entière sur les décalages de la marge (±50 px), hors du voisinage ±10 px du décalage nul.

---

## 2. Le banc

```
etude_confiance [resultats.csv]      tests/phase_correlation/etude_confiance.cpp
```

Compilé avec les tests, mais pas lancé par `ctest` (~35 s en Release). **Reproductible** : les graines sont fixes, donc deux exécutions donnent une sortie identique, ce qui a été vérifié.

Paires de tuiles de 1000 px, B sous A, recouvrement nominal 20 % (marge du rail ±50 px), vérité exacte au sous-pixel (décalage par FFT), **bruit capteur indépendant** dans A et B (σ = 150 sur 16 bits). Chaque cas est mesuré **3 fois**, avec un bruit et un résidu (décalage restant par rapport à la position nominale) différents : (3,4 ; 12,6), (−8,7 ; −4,2) et (14,2 ; 27,9) px.

| Famille | Variantes |
|---|---|
| `reference` | Texture normale, 3 graines |
| `fond_noir` | Noir exact (hors de l'œuvre) sur 30 à 90 % de la bande |
| `fond_presque_noir` | Fond uni sombre non nul (pris pour de la peinture) + bruit, sur 30 à 95 % |
| `sombre_peu_contraste` | Peinture sombre, rapport contraste / bruit de 20 à 0,13 |
| `motif_vertical` | Motif répété dans le sens du déplacement, période 20 à 120 px |
| `motif_grille` | Motif répété dans les deux axes (toile, carrelage), 20 à 75 px |
| `motif_imparfait` | Grille 30 px + 5 à 30 % de texture non répétée |
| `motif_hors_marge` | Période 75 px, vraie translation **hors** de la marge (60 et 70 px) |

Une mesure est **juste** si son erreur est ≤ 0,5 px. Pour chaque règle, une mesure est classée :

| | Juste | Fausse |
|---|---|---|
| **Acceptée** | fiable | **FAUX ACCEPTÉ** (le cas grave) |
| **Rejetée** | rejet inutile | rejet justifié |

---

## 3. Résultats

**Toutes familles (111 mesures)**

| Règle | Fiable | **Faux accepté** | Rejet justifié | Rejet inutile |
|---|---|---|---|---|
| actuelle (rapport ≥ 1,5) | 62 | **20** | 27 | 2 |
| rapport ≥ 2 | 58 | 14 | 33 | 6 |
| rapport ≥ 3 | 55 | 12 | 35 | 9 |
| **actuelle + ncc ≥ 0,3 + disp ≤ 0,5** | 60 | **11** | 36 | 4 |
| **+ autosim < 0,6** | 45 | **6** | 41 | 19 |

Pour `autosim`, les seuils 0,6 et 0,8 donnent exactement les mêmes résultats : les textures restent sous 0,36, les motifs répétés dans la marge sont à 0,99–1,00.

**Par famille** (fiable / faux accepté / rejet justifié / rejet inutile)

| Famille | Actuelle | + ncc + disp | + autosim |
|---|---|---|---|
| reference | 9 / 0 / 0 / 0 | 9 / 0 / 0 / 0 | 9 / 0 / 0 / 0 |
| fond_noir | 9 / 0 / 9 / 0 | 9 / 0 / 9 / 0 | 9 / 0 / 9 / 0 |
| fond_presque_noir | 13 / **4** / 1 / 0 | 13 / 0 / 5 / 0 | 13 / 0 / 5 / 0 |
| sombre_peu_contraste | 5 / **5** / 11 / 0 | 3 / 0 / 16 / 2 | 3 / 0 / 16 / 2 |
| motif_vertical | 12 / **4** / 0 / 2 | 12 / **4** / 0 / 2 | 8 / 0 / 4 / 6 |
| motif_grille | 5 / **1** / 3 / 0 | 5 / **1** / 3 / 0 | 3 / 0 / 4 / 2 |
| motif_imparfait | 9 / 0 / 0 / 0 | 9 / 0 / 0 / 0 | 0 / 0 / 0 / 9 |
| motif_hors_marge | 0 / **6** / 3 / 0 | 0 / **6** / 3 / 0 | 0 / **6** / 3 / 0 |

---

## 4. Ce que montrent les cas

**Fond noir exact : bien géré.** Les pixels à 0 reliés au bord sont exclus. Jusqu'à 70 % de noir, la mesure reste juste à 0,01 px ; au-delà de 75 %, elle est rejetée (`fractionValideMin`). Aucune erreur.

**Fond presque noir et peinture peu contrastée : le rapport ne suffit pas, les points locaux oui.** Quand le bruit domine (fond uni sur ≥ 90 % de la bande, ou contraste/bruit ≤ 4), le pic reste « franc » (rapport de 1,6 à 6,3) mais il est décalé de 0,5 à 3,6 px. Dans ces cas, soit les patchs ne se ressemblent plus (**NCC ≈ 0,04 à 0,10**, contre 0,99 normalement), soit les points ne sont pas d'accord avec la translation (**dispersion ≈ erreur**, 0,5 à 3,6 px). Les deux contrôles rattrapent les 9 faux acceptés de ces familles, pour 2 rejets inutiles : des mesures justes à 0,16 et 0,40 px, mais avec des patchs trop bruités.

**Motif répété de période < marge : l'algorithme prend la copie la plus proche du nominal.** Avec un motif de période P, les positions *vraie + k·P* se valent. La corrélation de phase choisit toujours celle qui est la plus proche de la position nominale : la mesure est juste si le résidu réel est < P/2, sinon elle est fausse **d'exactement une période** (20, 30 ou 40 px). Le rapport peut alors être élevé (**7,96** pour une erreur de 30 px), parce que la copie choisie est la seule bien recouverte. NCC et dispersion sont parfaites (0,99 ; 0,01 px), puisque la copie correspond pixel à pixel. Seule `autosim` voit le problème (1,00) : elle signale toutes ces paires comme **ambiguës**, qu'elles soient tombées justes ou non. Sur les 8 « rejets inutiles » des motifs, 6 sont des mesures justes **par chance**, le résidu tirait du bon côté. Les 2 autres (périodes 30 et 60 px, rapport 1,41 et 1,17) étaient déjà rejetées par la règle actuelle.

---

## 5. Limites de l'indicateur

1. **Translation réelle hors de la marge + motif répété : indétectable.** Si le rail s'écarte du nominal de plus que la marge (ici 60 et 70 px pour ±50) et que le motif a une période un peu plus grande que l'écart, une copie tombe dans la marge. La mesure est fausse de 75 px avec rapport 19 à 60, NCC 0,99, dispersion 0,01 et `autosim` 0,30 : **tous les indicateurs disent « fiable »**. Le même écart sur une texture normale est bien rejeté (rapport 1,1). Ce cas ne se détecte qu'avec une information extérieure à la paire (§6).
2. **`autosim` est prudente : elle rejette les motifs imparfaits.** Grille + 5 à 30 % de texture : la texture suffit à la corrélation de phase pour trouver la bonne copie (9 mesures justes sur 9), mais `autosim` reste à 0,99–1,00 et les signale comme ambiguës. Elle détecte la *présence* d'un motif répété, pas le fait que la mesure se soit trompée.
3. **`autosim` ne voit que les périodes inférieures à la marge.** Une période > 50 px n'est pas cherchée (0,19 à 0,30). C'est voulu : seule une copie qui tombe dans la marge peut tromper la mesure. Mais c'est aussi pour cela que la limite 1 lui échappe.
4. **NCC et dispersion demandent des points.** Sans point local (zone commune trop étroite, `nbPoints = 0`), ces deux contrôles ne peuvent pas être évalués. Le step garde alors la paire avec l'état `non_verifie` : sur une petite bande, une mesure fausse sur fond sombre peut donc passer (§6).
5. **Seuils réglés sur ce banc.** Les données sont synthétiques (textures générées, bruit gaussien, motifs parfaits, direction `Bottom` seule). Les valeurs 0,3 (NCC), 0,5 px (dispersion) et 0,6 (`autosim`) sont à confirmer sur de vraies tuiles, en particulier des toiles visibles et des cadres.
6. **Pistes écartées.**
   - Monter le seuil du rapport à 2 ou 3 : on passe de 20 à 12–14 faux acceptés, mais 6 à 9 mesures justes sont rejetées (contre 2), et le motif à 30 px (rapport 7,96) passe toujours.
   - Un « rapport compensé » (surface divisée par l'affaiblissement attendu de la fenêtre de Hann) a été essayé : les copies d'un motif restent très inégales (rapport 7,3), car la corrélation de phase ne garde que la phase.

---

## 6. Intégration dans le pipeline

La règle est appliquée par `PhaseCorrelationStep` (voir [sa page](../../../steps/PhaseCorrelationStep.md), § Confiance), avec des seuils réglables dans le JSON : `nccMin` (0.3), `dispersionMax` (0.5) et `autosimMax` (0.6). Une paire refusée va dans `failures` avec sa raison. Chaque paire a ses indicateurs (NCC, dispersion, auto-similarité, points et état) dans `getResult()["confidence"]`. Le format de `PairMatches` ne change pas.

Le banc mesure la décision réelle du step (ligne `PIPELINE` de sa sortie) : **6 faux acceptés, comme la règle étudiée**.

| | Fiable | Faux accepté | Rejet justifié | Rejet inutile |
|---|---|---|---|---|
| Avant (rapport ≥ 1,5) | 62 | 20 | 27 | 2 |
| **PhaseCorrelationStep** | 45 | **6** | 41 | 19 |

Une précision par rapport à l'étude : sans point local, la paire est rejetée seulement si la zone commune permettait d'en placer. Sinon, elle est gardée avec l'état `non_verifie`. Rejeter toute paire sans point aurait fait échouer les petits recouvrements (bande plus étroite qu'un patch), et le test du pipeline.

**Suites possibles** (à décider avec l'équipe) :
1. Exporter les indicateurs (`rapport`, `ncc`, `dispersion`, `autosim`) dans `PairMatches`, pour que le placement global pondère les paires au lieu d'un tri accepté / rejeté. Cela change le contrat commun.
2. Pour la limite 1, contrôler la cohérence sur la grille : **fermeture de boucle** sur 4 tuiles (`t(0→1) + t(1→3) − t(0→2) − t(2→3) ≈ 0`). C'est le rôle du placement global.
