# FinalVerificationStep

Code : `steps/final_verification_step.h` / `.cpp` (namespace `StepsNs`). Nom dans le script : `final-verification`.
Comparaison : `algorithms/verification/` (`VerificationNs::compareImages`).

Step facultatif à placer après la production du TIFF reconstruit. Il compare un fichier de référence, TIFF ou JPEG,
à un fichier de sortie TIFF, puis publie un rapport descriptif. Il n'évalue pas la qualité, ne modifie aucun fichier
et ne compare pas les DPI/PPI ni les pixels eux-mêmes.

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ | Rôle |
|---|---|---|---|---|
| Entrée | — | — | — | `inputImagePath` et `outputImagePath` désignent des fichiers existants |
| Sortie | `verificationReport` | `VerificationReport` | `DataNs::VerificationReport` | Rapport JSON complet |

`getResult()` expose également le rapport complet; les erreurs d'ouverture ou de lecture sont retournées dans
`getResult()["error"]` et font échouer le step.

## Paramètres

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `inputImagePath` | string | vide | Image de référence du client, TIFF ou JPEG |
| `outputImagePath` | string | vide | Image reconstruite, TIFF |

## Rapport

- Dimensions de référence et de sortie, écarts signés (sortie moins référence) et rapports largeur/hauteur.
- Métadonnées techniques comparées : profondeur par échantillon, nombre d'échantillons, format d'échantillon,
  interprétation photométrique, compression, orientation, configuration planaire et échantillons supplémentaires.
- Tags TIFF usuels (description, nom du document, marque, modèle, logiciel, date, artiste, droits d'auteur,
  dates EXIF originales/numérisées).
- Pour les JPEG, informations de frame, tags EXIF usuels (orientation, marque, modèle, logiciel, date, artiste,
  copyright), version/miniature JFIF, commentaires et segments APP hors champs de densité physique.
- Présence et taille du profil ICC, égalité exacte de ses octets et état de l'équivalence colorimétrique.

Si les profils ICC sont identiques octet par octet, le rapport l'indique. S'ils diffèrent, le rapport ne conclut pas
qu'ils produisent un rendu différent : en l'absence de moteur de gestion des couleurs, leur équivalence
colorimétrique est indiquée comme indéterminée. Un profil absent est signalé comme avertissement.

Les DPI/PPI sont volontairement ignorés. Les dimensions différentes sont rapportées, mais elles ne déclenchent
aucun verdict de qualité ou de conformité.

## Exemple de rapport

```json
{
  "status": "report_ready",
  "dimensions": {
    "input": { "width": 1000, "height": 800 },
    "output": { "width": 1200, "height": 800 },
    "difference": { "width": 200, "height": 0 },
    "outputToInputRatio": { "width": 1.2, "height": 1.0, "aspectRatio": 1.2 }
  },
  "iccProfile": {
    "inputPresent": true,
    "outputPresent": true,
    "identicalBytes": false,
    "colorimetricEquivalence": "colorimetric_equivalence_undetermined"
  }
}
```

Les segments de métadonnées JPEG hors JFIF (dont les données de densité ne sont pas comparées) sont comparés
octet par octet. Les tags TIFF comparés sont ceux listés ci-dessus; le step ne tente pas de décoder des tags
propriétaires arbitraires. Le rapport est sauvegardé dans les checkpoints sous `verificationReport.json`.

## Tests

`tests/steps/test_final_verification_step.cpp` couvre les différences de dimensions et de métadonnées TIFF,
les profils ICC identiques au niveau octet, les JPEG, la publication au DataStore et les erreurs de lecture.
