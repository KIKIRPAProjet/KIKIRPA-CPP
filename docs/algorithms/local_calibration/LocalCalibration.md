# Le calage local : raisonnement

> **Brouillon.** Cette page pose le raisonnement du calage local : ce que c'est, pourquoi ce sont des distorsions, comment les classer et les détecter, et où le placer dans le pipeline. La correction, les contraintes détaillées et le plan de mise en œuvre restent à écrire (voir [§5](#5-ce-qui-reste-à-écrire)). Aucun step ni algorithme de calage local n'existe encore sur `main`.
>
> Les mesures citées viennent de tuiles **IRR** (réflectographie infrarouge). Elles ne disent rien des tuiles Hasselblad en lumière visible.

| Pour… | Lire |
|---|---|
| situer le calage local dans le pipeline | [COMPRENDRE_LE_PROJET.md](../../COMPRENDRE_LE_PROJET.md) §1 et §10 |
| ce que fait déjà la mise en position | [steps/AbsolutePosStep.md](../../steps/AbsolutePosStep.md) |
| ce que contient `matches` (les points que le calage local lit) | [steps/PhaseCorrelationStep.md](../../steps/PhaseCorrelationStep.md), [steps/SiftStep.md](../../steps/SiftStep.md) |
| le rejet des aberrants, supposé branché avant le calage local | [rejection/vote_2d/Vote2D.md](../rejection/vote_2d/Vote2D.md) |
| ajouter un step | [steps/AJOUTER_UN_STEP.md](../../steps/AJOUTER_UN_STEP.md) |
| les règles de code et de documentation | [CONVENTIONS.md](../../CONVENTIONS.md) |

## Sommaire

1. [Le calage local](#1-le-calage-local)
2. [Ces déformations sont des distorsions](#2-ces-déformations-sont-des-distorsions)
3. [Méthodologie : quelles distorsions, comment les détecter](#3-méthodologie--quelles-distorsions-comment-les-détecter)
4. [Où le placer dans le pipeline](#4-où-le-placer-dans-le-pipeline)
5. [Ce qui reste à écrire](#5-ce-qui-reste-à-écrire)
6. [Sources](#6-sources)

---

## 1. Le calage local

### 1.1 La consigne de départ

Les notes de départ du sujet disent :

> Le calage local : corriger des déformations à l'intérieur des tuiles, au-delà d'une translation globale.
>
> De manière globale, sur PTGUI, on observe parfois après assemblage des bords arrondis.
>
> Il faut considérer les potentielles erreurs d'alignement entre caméra et objet, le grain, les déformations de la toile,...

Trois idées à retenir :

- **« à l'intérieur des tuiles »** : on ne parle pas d'un décalage entre tuiles, mais de la **forme** de chaque tuile ;
- **« au-delà d'une translation globale »** : la mise en position (`absolute-pos`) place chaque tuile par une translation. Le calage local intervient quand cela ne suffit pas ;
- **« bords arrondis »** : c'est le symptôme visible. Les bords droits d'un tableau ou d'un cadre apparaissent courbes dans la mosaïque.


### 1.2 Ce que dit le cahier des charges

Le cahier des charges KIK-IRPA définit le calage local comme la « correction des petits décalages dus aux déformations du support » (étape 5 de la chaîne, après l'optimisation globale). Son glossaire parle d'une « légère déformation d'une tuile dans les recouvrements, pour compenser les déformations du support ».

Pourquoi il est nécessaire : les œuvres présentent de légères déformations physiques (panneaux de bois gauchis, toiles détendues, bords non peints, empâtements). **Même bien positionnées, deux tuiles voisines ne se superposent pas exactement partout**, ce qui produit des **dédoublements**. Le logiciel doit appliquer un calage élastique local dans les zones hors plan, limité aux zones concernées, sans déformer l'ensemble de la mosaïque.

À la demande du KIK-IRPA, cette fonctionnalité est prévue **dans un second temps**, après les fonctionnalités principales.

### 1.3 Un nouveau step

Le calage local est **un nouveau step** du pipeline, `local-calibration`. Il se place **après le rejet des aberrants et avant `absolute-pos`** ; le raisonnement est au [§4](#4-où-le-placer-dans-le-pipeline).

```
import → appariement → rejet → local-calibration → absolute-pos → intensity-correction → graphcut → assemblage
       (matches)       (à brancher)  (à écrire)      (translations)
```

`absolute-pos` ne calcule que des translations : `rotationRadians` vaut toujours 0 et l'échelle n'est pas modélisée. Le calage local est le maillon qui manque entre des décalages mesurés et une image qui se superpose réellement.

### 1.4 Deux exigences qui orientent tout le raisonnement

Ces deux exigences viennent du cahier des charges et de la consigne du projet. Leur détail est à écrire ([§5](#5-ce-qui-reste-à-écrire)) :

- **Aucun pixel créé.** Pas d'interpolation : chaque pixel de la mosaïque reprend la valeur d'un seul pixel source. Cela limite les corrections possibles (déplacements entiers) et fixe une précision plancher de 0,5 px par axe.
- **Les bords de la mosaïque ne sont pas déformés.** La mosaïque ne présente ni courbure, ni inclinaison, ni étirement d'ensemble. La déformation récurrente des bords est le principal défaut reproché à PTGui.

---

## 2. Définir les distortions

### 2.1 Définition

Une **distorsion** est un écart entre l'image réelle d'une tuile et l'image qu'aurait donnée une prise de vue parfaite, **au sens de la géométrie** : un point de l'œuvre n'apparaît pas à l'endroit que le modèle de translation prévoit.

Dans le pipeline actuel, le modèle est : la tuile B est la tuile A décalée d'une translation. Une tuile est donc distordue quand **le décalage entre deux points correspondants n'est pas le même partout dans le recouvrement**.

| Cas | Décalage dans le recouvrement | Nom |
|---|---|---|
| le même partout | constant | translation : `absolute-pos` s'en charge |
| varie régulièrement avec la position | linéaire | rotation, échelle, cisaillement : distorsion **affine** |
| varie de façon plus complexe | non linéaire | distorsion **non affine** |

Dans la suite, « distorsion » désigne tout écart au modèle de translation, quelle qu'en soit la cause. 
### 2.2 Ce qui ressemble à une distorsion sans en être une

- **L'erreur de mesure.** Une zone sans détail, un reflet de vernis ou un faux appariement donnent un décalage qui paraît réel. Un faux décalage est **incohérent** d'une fenêtre à sa voisine, alors qu'une vraie distorsion varie de façon continue ([§3.2](#32-comment-les-détecter)).
- **Un changement d'apparence.** En lumière rasante, les ombres dépendent de la direction de l'éclairage. Si elle change d'une tuile à l'autre, on peut mesurer des ombres plutôt que la surface. Ce point n'est pas vérifié sur nos données.

### 2.3 Pourquoi la forme du décalage renseigne sur la distorsion

Notons Δ la translation entre deux tuiles voisines, `u` la position d'un point dans la tuile A, et `D` une distorsion **commune** à toutes les tuiles (cas d'un objectif). Le même point de l'œuvre apparaît en `u` dans A et en `u − Δ` dans B. Après distorsion, on observe `D(u)` dans A et `D(u − Δ)` dans B. Le décalage que doit absorber la translation est :

```
écart(u) = D(u) − D(u − Δ)
```

- Si `D` est **affine** (`D(u) = A·u + b`), alors `écart(u) = A·Δ` : une **constante**. Il suffit de bouger la tuile B de cette valeur pour qu'elle se superpose avec A.
- Si `D` n'est **pas** affine (par exemple radiale, `D(u) = k·|u|²·u`), `écart(u)` **varie avec `u`**. Dans ce cas, effectuer une simple translation n'alignera pas totalement les images. On aura des détails en double.

Deux conséquences :

1. **Seule la partie non affine d'une distorsion commune est mesurable dans les recouvrements, et c'est elle qui crée les artefacts.** C'est le résultat de Rouwane et al. (2024).
2. Une distorsion d’objectif déplace les points par rapport au centre de la tuile : elle les écarte du centre (ou les en rapproche), et d’autant plus qu’on est loin. Au bord droit de A un pixel sera poussé vers la droite (ou ramené vers la gauche) et au bord gauche de B un pixel sera poussé vers la gauche (ou ramené vers la droite). Ainsi, la même distorsion pousse dans des **sens opposés**. L'erreur liée à la distorsion dans un recouvrement vaut donc environ **le double** de celle d'une tuile.

Ce raisonnement vaut pour une distorsion **commune**. Une distorsion **propre à chaque tuile** (une rotation différente d'une prise à l'autre) n'est pas couverte : elle est mesurée séparément ([§3.2](#32-comment-les-détecter)).

### 2.4 Le lien avec les bords arrondis

Le contour de la mosaïque est formé des bords **extérieurs** des tuiles de la périphérie. À ces bords, aucune tuile voisine ne contraint l'erreur. Une distorsion qui grandit vers les bords de la tuile (comme la distorsion radiale) courbe donc ce contour, alors que dans les recouvrements elle se voit comme un dédoublement.
Dans ce cas, on aurait donc des bords en forme de vagues.

Nous suppectons que les bords arrondis rencontrés lors de l'assemblage avec PTGui soient une tendance de l'application qui est pensée pour les panoramas à 360°.

---

## 3. Méthodologie : quelles distorsions, comment les détecter

La méthode sépare trois questions, pour qu'on puisse répondre à chacune par une mesure :

1. **Y a-t-il une distorsion ?** Le décalage résiduel après translation varie-t-il avec la position ?
2. **De quel type ?** Quelle forme mathématique, quelle cause, commune à toutes les tuiles ou propre à chacune ?
3. **Par rapport à quoi ?** Une mesure entre tuiles est **relative** : elle ne dit pas laquelle est juste. Une référence absolue est nécessaire pour fixer la forme de l'œuvre (voir [§3.3](#33-la-question-de-la-référence)).

### 3.1 Les types de distorsion

Voici une liste de distortions possibles.
On les classe selon deux critères : la **forme** du décalage, et son **origine** (commun à toutes les tuiles, propre à chacune, ou local).

| Type | Forme | Origine possible | Commune ? | Signature dans les mesures | Sources consultées |
|---|---|---|---|---|---|
| Rotation, échelle | affine | erreurs mécaniques du rail, remise au point (le grandissement change avec la mise au point) | **propre à chaque tuile** | rotation ou échelle **différentes d'une paire à l'autre** | Brown & Lowe, *Automatic Panoramic Image Stitching using Invariant Features*, IJCV 2007 ([PDF](https://www.cs.ubc.ca/~lowe/papers/07brown.pdf)) : chaque image a sa propre rotation et sa focale, ajustées ensemble (§2, §4). Remise au point : [PhotographyLife](https://photographylife.com/focus-breathing), l'angle de champ change avec la distance de mise au point (environ 5 à 6 % sur un zoom à 200 mm, jusqu'à environ 23 % annoncé sur un autre objectif à courte distance). Ce sont des objectifs photo courants, pas notre matériel. **Aucune source trouvée pour les erreurs de rail.** |
| Perspective | projective (homographie) | capteur non parallèle à l'œuvre | commune ou par tuile | contour en trapèze, échelle qui varie d'un côté à l'autre | Collins, [*Planar Homographies*](https://www.cse.psu.edu/~rtc12/CSE486/lecture21.pdf) (cours CMPEN454, Penn State) : pour une surface plane, la projection 3D vers 2D se réduit à une homographie, le plan frontal étant un cas particulier. Brown & Lowe, éq. (1) : les images se relient par des homographies. |
| Distorsion radiale | non affine, centrée sur la tuile | objectif (barillet, coussinet) | **commune** | écart qui croît vers les bords de la tuile, en sens opposés dans le recouvrement | Rouwane et al. 2024 ([doi](https://doi.org/10.1016/j.optlastec.2024.111165)) : seule la partie non affine d'une distorsion commune est mesurable dans les recouvrements. Brown & Lowe, §9 et fig. 9 : la distorsion radiale n'est pas modélisée et laisse des artefacts visibles dans le rendu. Byröd, Brown, Åström, [*Minimal Solutions for Panoramic Stitching with Radial Distortion*](https://bmva-archive.org.uk/bmvc/2009/Papers/Paper057/Abstract057.pdf), BMVC 2009 : au-delà d'une distorsion faible, il faut la modéliser dès l'estimation entre deux images. Rosten & Loveland, [arXiv:0810.4426](https://arxiv.org/abs/0810.4426) : détection sur une seule image par la méthode du fil à plomb, les droites de la scène doivent redevenir droites. |
| Relief, parallaxe | non affine, irrégulier | relief de l'œuvre vue de deux points différents | **non commune**, dépend de l'endroit | champ des déplacements  cohérent mais non affine, propre à chaque zone | Lee & Sim, [*Warping Residual Based Image Stitching for Large Parallax*](https://mlanthology.org/cvpr/2020/lee2020cvpr-warping/), CVPR 2020 : la parallaxe vient d'une scène non plane vue de deux points, et une seule homographie ne suffit pas. Tchana et al., [SENA, arXiv:2601.01257](https://arxiv.org/abs/2601.01257), 2026 (préprint non relu) : l'hypothèse de scène plane échoue quand la profondeur varie. Lus seulement via les résumés. |
| Déformation du support | non affine, lisse | panneau gauchi, toile détendue | **non commune** | idem, étendu sur une grande zone | Gauvin et al., *Image correlation to evaluate influence of hygrothermal loading on wood*, Strain 50(5), 2014 : la corrélation d'images mesure le champ de déformation du bois soumis à l'humidité (motivation : le gauchissement des panneaux peints). Elle porte sur une coupe de bois, pas sur des tuiles. Sindel, Maier, Christlein, [arXiv:2601.16348](https://arxiv.org/abs/2601.16348), 2026 (préprint) : l'alignement de photographies de panneaux peints demande un modèle non rigide (splines à plaque mince). Lus seulement via les résumés. **Aucune source trouvée sur la détection à partir de recouvrements de tuiles.** |
| Assemblage | globale à la mosaïque | modèle d'objectif ajusté par un logiciel (PTGui) | sans objet | contour courbe dans la mosaïque, **sans** écarts correspondants entre tuiles | Brown & Lowe, §5 : sans redressement, un panorama assemblé par un modèle global sort ondulé, car l'orientation globale choisie est arbitraire. C'est un effet du modèle global, pas des tuiles. **Aucune source trouvée sur les bords arrondis de PTGui.** |
| Scan IRR | à établir | capteur mobile, scan composite | à établir | à établir | **Aucune source trouvée.** La page [Conservation Wiki](https://www.conservation-wiki.com/wiki/Infrared_Reflectography_(IRR)) consultée ne parle ni de distorsion ni de géométrie. |

Remarques :

- **L'objectif.** Pour les tuiles Hasselblad en lumière visible et rasante, les déformations optiques sont corrigées en amont par Phocus (cahier des charges). Il s'agit d'une correction **nominale** : un reliquat reste possible. Elle n'est donc pas supposée mais **vérifiée** ([§3.2](#32-comment-les-détecter)). Elle ne concerne pas les tuiles IRR.
- **La parallaxe n'existe que s'il y a du relief.** Pour un plan parfait, un rail qui translate un capteur parallèle à l'œuvre ne la produit pas. Pour fixer un ordre de grandeur, un relief de 1 mm vu à un angle de 10° au bord de chaque tuile (hypothèses illustratives) donne un décalage d'environ 40 px à 8,62 µm par pixel, très au-dessus de l'objectif de 2 px. Le calcul exact demande la distance et l'optique réelles.
- **La texture** (grain, craquelures) ne déforme rien. Elle intervient dans la **mesure** : détails absents, faux appariements ([§2.2](#22-ce-qui-ressemble-à-une-distorsion-sans-en-être-une)). À ne pas confondre avec le relief d'ensemble.
- **Le type « assemblage »** n'est pas une distorsion des tuiles. Il est listé parce qu'il produit le même symptôme visible (bords arrondis). Le pipeline ne l'a pas par construction, puisqu'il n'utilise aucun modèle global.

### 3.2 Comment les détecter

On introduit le concept de fenêtre : un carré de 256*256 pixels dans une zone de recouvrement. On fait se chevaucher les fenêtres avec un pas de 128 pixels pour pouvoir observer les différences d'alignement.


Le principe est de **mesurer le décalage résiduel dans le recouvrement après la meilleure translation, fenêtre par fenêtre**, puis de le caractériser. Le script de diagnostic (`diagnostic_calage_local.py`) applique ce principe. Il vit pour l'instant dans le dossier de travail du projet, pas dans le dépôt.

Le script part des **images** : il fait lui-même l'alignement et les mesures par fenêtres. Dans le pipeline, ces mesures existent déjà : ce sont les points de `matches` ([§4.1](#41-ce-dont-le-calage-local-a-besoin)). Le script reste utile comme outil de validation, indépendant du pipeline.

**Étape 1 : aligner.** On place B par rapport à A par une translation **entière**, donc sans créer de pixel. Une recherche de gabarit sur des copies réduites donne une position grossière, puis on la recentre sur la médiane des écarts mesurés. Cette médiane **définit** la « translation globale ».

**Étape 2 : mesurer.** Le recouvrement est découpé en fenêtres de 256 px (pas de 128 px). Dans chacune, une corrélation de phase donne un décalage `(dx, dy)` au sous-pixel et un pic entre 0 et 1 au niveau de ce décalage. Si les deux fenêtres indiquent la même chose mais décalée, le pic est proche de 1, s'il n'y a presque rien en commun, il n'y a pas de pic. On base l'indice de confiance sur ces pics. Les décalages sont assemblés sous la forme d'un champ.

**Étape 3 : résumer.** On calcule les métriques du document « Pipeline Algorithme » : moyenne, médiane, 95e percentile (p95), maximum, et part des fenêtres sous 1, 2 et 5 px. Le p95 est la valeur sous laquelle se trouvent 95 % des fenêtres. Il dit « presque partout, l'écart est au plus de tant », alors que la moyenne cache une mauvaise zone et que le maximum dépend d'une seule fenêtre aberrante.

**Étape 4 : caractériser.** C'est ici que l'on répond à la question « de quel type ? ».

| Question | Test | Ce que cela révèle |
|---|---|---|
| Reste-t-il un écart qui n'est pas une translation ? | écart après translation seule | distorsion ou non |
| La part non affine est-elle importante ? | retirer la tendance **affine** (rotation, échelle, cisaillement relatifs), regarder le reste | si le reste est négligeable, la translation et un réglage rigide suffisent |
| L'écart grandit-il vers les bords des tuiles ? | écart moyen par tranche de **distance au centre de A** et de B, corrélation | distorsion centrée sur la tuile (radiale) |
| L'écart est-il réel ou du bruit ? | **cohérence spatiale** : différence entre fenêtres voisines comparée à l'écart lui-même | champ réel (continu) ou erreur de mesure (incohérent) |
| La distorsion est-elle commune ? | comparer plusieurs paires : même champ partout, ou rotation différente d'une paire à l'autre | commune (objectif) ou propre à chaque tuile (rail) |
| L'optique est-elle bien corrigée en amont ? | ajuster un champ commun sur les écarts, voir s'il est proche de zéro | confirme (ou non) la correction Phocus |
| Le contour courbe vient-il de l'assemblage ? | comparer le contour de la mosaïque avec les écarts entre tuiles et avec les paramètres du projet PTGui | assemblage ou tuiles |

Les trois dernières lignes ne sont pas encore programmées.

**Limites connues du diagnostic actuel :**

- une **seule paire** ne sépare pas la déformation de A de celle de B ;
- les fenêtres n'existent que **dans le recouvrement** (8 % à l'horizontale, 17 % à la verticale sur Blès) ; un recouvrement trop étroit en donne trop peu pour conclure ;
- le retrait de la tendance affine est mal conditionné dans la direction étroite d'une bande ;
- le seuil de confiance (0,3) est réglé à l'œil sur Blès ;
- le script n'a pas de test unitaire.

### 3.3 La question de la référence

Une distorsion se mesure **entre** tuiles. Rien ne dit laquelle est juste, et aucune tuile n'est une référence fiable a priori : si toutes partagent la même optique, choisir l'une d'elles reporte son erreur sur les autres.

Deux besoins à ne pas confondre :

- **Rendre les tuiles cohérentes entre elles** : une référence n'est pas nécessaire. Un champ commun à toutes les tuiles peut être estimé à partir des seuls recouvrements (méthode auto-calibrée de Rouwane et al.).
- **Fixer la forme absolue de l'œuvre** : une référence externe est nécessaire. Le cahier des charges prévoit, en option, une vue d'ensemble à plus faible résolution comme ancrage, et le KIK-IRPA reproche à PTGui de ne pas permettre de désigner une image de référence.

Choisir la référence « à l'œil nu » est à discuter. Un choix visuel n'est ni mesuré ni reproductible, alors que le cahier des charges demande des mesures enregistrées. L'œil convient comme **validation par l'opérateur**, pas comme méthode de choix.

### 3.4 Premières mesures (tuiles IRR)

Écarts en pixels pleine résolution, fenêtres de 256 px, avant et après retrait de la tendance affine.

| Paire | Fenêtres | Translation seule : moyenne / p95 | Après retrait affine : moyenne / p95 | Grandit vers les bords |
|---|---|---|---|---|
| Blès b→a | 23 | 6,0 / 9,2 | 2,0 / 5,9 | oui (1,4 → 2,7 px) |
| Blès c→d | 17 | 6,0 / 15,0 | 3,6 / 7,8 | oui (1,9 → 5,9 px) |
| Dürer 03→02 | 110 | 12,8 / 36,5 | 12,5 / 31,6 | oui (9 → 17 px) |
| Dürer 04→07 | 39 | 14,6 / 33,3 | 15,4 / 32,2 | peu fiable (bruit) |
| Blès b→c, a→d | 4 et 6 | non concluant | non concluant | recouvrement horizontal trop étroit |

Ce qu'on en tire, avec prudence :

- Une translation seule laisse **6 à 15 px** d'écart moyen, très au-dessus de l'objectif de 2 px (p95).
- Sur Blès, la rotation relative entre tuiles (0,04 à 0,6°) domine l'écart, et elle **diffère d'une paire à l'autre** : c'est un défaut propre à chaque tuile, pas une distorsion commune.
- Sur Dürer 03→02, 12 px subsistent après retrait affine, avec un champ cohérent d'une fenêtre à l'autre : une déformation **non rigide**, probablement le relief du support.
- Sur les paires exploitables, l'écart résiduel **grandit avec la distance au centre de la tuile**. L'hypothèse d'une déformation qui s'accentue aux bords est **soutenue, sans être prouvée** (peu de fenêtres, deux tuiles par paire).
- **Aucune paire n'atteint p95 ≤ 2 px** après retrait affine.

Ces mesures portent sur des tuiles IRR, scan à capteur mobile, non corrigées par Phocus. Elles ne permettent pas de conclure pour les tuiles Hasselblad ni pour le cas de référence (La chute d'Icare).

---

## 4. Où le placer dans le pipeline

### 4.1 Ce dont le calage local a besoin

- **Des décalages mesurés à plusieurs endroits du recouvrement.** `matches` les contient déjà : `pointsInA[i] ↔ pointsInB[i]`, avec `pointsInA[i] − pointsInB[i] ≈ (translationX, translationY)`. L'écart résiduel d'un point est donc `pointsInA[i] − pointsInB[i] − (translationX, translationY)`. Le step n'a pas à relire les images ni à refaire les fenêtres du script.
- **Des correspondances propres.** Un faux appariement ressemble à une distorsion ([§2.2](#22-ce-qui-ressemble-à-une-distorsion-sans-en-être-une)) et fausserait le champ estimé. Le rejet des aberrants (vote 2D) passe donc avant. Il n'est pas branché sur `main` aujourd'hui ; ce document suppose qu'il le sera.
- **Assez de points, bien répartis.** `phase-correlation` publie au plus `nbPoints` points locaux par paire (10 par défaut, un par petit patch) ; `sift` et `lightglue` en publient beaucoup plus. Le nombre de paramètres du modèle de distorsion doit rester petit devant le nombre de points, sinon le modèle ajuste le bruit.

### 4.2 Les positions possibles

| Position | Avantages | Inconvénients | Verdict |
|---|---|---|---|
| **1. Juste après l'appariement, avant le rejet** | aucun | les faux appariements contaminent le champ ; il faudrait un ajustement robuste, qui refait le travail du rejet | à écarter |
| **2. Après le rejet, avant `absolute-pos`** | entrées propres ; aucune position nécessaire ; `absolute-pos` reste inchangé s'il reçoit des translations corrigées | chaque paire garde sa propre translation, la cohérence globale (les boucles de la grille) n'est pas exploitée | **recommandée pour une première version** |
| **3. Après `absolute-pos`** | les positions sont connues, les boucles de la grille aussi | les positions ont été calculées sur des translations **biaisées** par la distorsion (voir [§4.4](#44-faut-il-estimer-la-translation-globale-)) ; il faut relancer `absolute-pos`, donc itérer | possible plus tard, comme raffinement |
| **4. Dans `absolute-pos`** (ajustement conjoint des positions et du champ) | c'est la formulation la plus complète (Rouwane et al.) | un step qui fait deux choses ; couplage fort ; `absolute-pos` est déjà testé et stable | hors de portée d'une première version |

Les positions 3 et 4 correspondent à ce que font les deux articles étudiés : alterner l'assemblage et la correction jusqu'à convergence (DistortCorrect, 15 à 22 itérations), ou les résoudre ensemble (Rouwane et al.). On y revient seulement si la position 2 laisse un biais mesurable.

### 4.3 Le step `local-calibration` entre le rejet et `absolute-pos`

| Sens | Clé | Type | Remarque |
|---|---|---|---|
| Entrée | `tiles` | `Tiles` | les dimensions et le centre de chaque tuile |
| Entrée | `matches` | `PairMatches` | après rejet des aberrants |
| Sortie | `matches` | `PairMatches` | **réécrite** : points corrigés, translations recalculées. Même précédent que `match-filter` dans [AJOUTER_UN_STEP.md](../../steps/AJOUTER_UN_STEP.md#5-enregistrer-le-step) : les steps suivants lisent des clés fixées dans leur code, donc `absolute-pos` continue de lire `matches` |
| Sortie | `distortion` | nouveau type, à définir | le modèle, ses coefficients, son centre, l'amplitude au bord, l'erreur résiduelle par paire |

- Les données brutes ne sont pas perdues : elles restent dans le checkpoint du step d'appariement.
- Un nouveau type de donnée demande sa sérialisation de checkpoint, sinon le step est refait à chaque reprise ([AJOUTER_UN_STEP.md §6](../../steps/AJOUTER_UN_STEP.md#6-si-la-sortie-est-dun-nouveau-type)).
- `absolute-pos` ne change pas : il reçoit des translations qui ne sont plus faussées par la distorsion.


### 4.4 Conséquences sur le reste du pipeline

- **L'assemblage** doit appliquer le champ à chaque tuile (par pixel le plus proche, sans interpolation) avant de la placer par `positions`. Le contrat avec l'assemblage (PR #69) est à décider.
- **`graphcut` et `intensity-correction`** lisent les pixels des tuiles source, sans le champ. Une déformation de 12 px en pleine résolution vaut 3 px à l'échelle 0,25 de `graphcut` et 1,5 px à la réduction 1/8 d'`intensity-correction`. L'effet est probablement faible pour l'intensité ; à vérifier pour les coutures.
- **Les rotations propres à chaque tuile** (0,04 à 0,6° sur Blès) ne sont pas une distorsion commune : ce sont des **poses**, qui relèvent de la mise en position globale. `PairMatches` ne transporte pas d'angle relatif, donc `absolute-pos` ne peut pas les calculer. À décider séparément : étendre `PairMatches`, ou les traiter dans un autre step.
- **La déformation du support** (Dürer 03→02) n'est pas commune aux tuiles : un champ commun ne la corrige pas. Elle demande un modèle local, par zone, dans une version ultérieure.
- **Le script de diagnostic** reste un outil de validation indépendant : il mesure depuis les images, sans dépendre de `matches`.

---

## 5. Ce qui reste à écrire

- **Les contraintes**, avec leur origine : fidélité à l'œuvre (aucun pixel créé, 16 bits, fichiers source intacts), géométrie (bords non déformés, échelle conservée, amplitude mesurée et limitée, zones douteuses signalées), logiciel (conventions, mémoire).
- **La correction** : quel modèle retenir selon le type détecté (quels modes `φₖ`), et comment l'appliquer sans interpolation (déplacement par pixel le plus proche).
- **Le critère de réussite** : moyenne et p95 en pixels, contour de la mosaïque droit. Une proposition existe (moyenne ≤ 1 px, p95 ≤ 2 px) ; elle est à valider avec le KIK-IRPA.
- **Le plan de mise en œuvre** : un algorithme isolé, le step `local-calibration` ([§4.3](#43-le-step-local-calibration-entre-le-rejet-et-absolute-pos)), le type `Distortion` et son checkpoint, le contrat avec l'assemblage, la validation.
- **Le biais des translations** : le mesurer sur une grille réelle pour décider s'il faut itérer avec `absolute-pos` (positions 3 et 4 du [§4.2](#42-les-positions-possibles)).
- **L'échelle globale** : choisir la référence (pas du rail ou vue d'ensemble).
- **Les rotations par tuile** : décider où les calculer.
- **Les données manquantes** : une série Hasselblad avec son assemblage PTGui aux bords arrondis, idéalement le fichier de projet PTGui (il enregistre les paramètres d'objectif ajustés et la projection).

## 6. Sources

| Source | Apport |
|---|---|
| *Projet KIK-IRPA : cahier des charges* (§2.3, §3.3.5, glossaire) | définition du calage local, exigences de fidélité et de géométrie |
| *Questions-réponses sur le cahier des charges* | défauts de PTGui (bords déformés, pas d'image de référence), correction Phocus |
| Rouwane et al., *High resolution and large field of view imaging using a stitching procedure coupled with distortion corrections*, Optics & Laser Technology 177 (2024) 111165, [doi:10.1016/j.optlastec.2024.111165](https://doi.org/10.1016/j.optlastec.2024.111165) | seule la partie non affine d'une distorsion commune est mesurable dans les recouvrements ; modèle polynomial commun ; code et données ouverts |
| Cang et al., *DistortCorrect Stitcher*, bioRxiv 2024, [doi:10.1101/2024.10.28.620720](https://doi.org/10.1101/2024.10.28.620720) (preprint non relu, code non publié) | distorsion radiale commune, sens opposés dans le recouvrement ; d'usage limité ici |
| *Pipeline Algorithme* (document d'équipe) | métriques de validation d'un décalage |
