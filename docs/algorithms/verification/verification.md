# Vérification finale d'une image reconstruite

## Objectif

La vérification finale produit un rapport descriptif comparant une image de référence fournie par le client avec
le TIFF reconstruit. Elle sert à constater les dimensions, leurs écarts, les proportions et les métadonnées
disponibles. Elle ne décide pas si le résultat est « bon » ou « conforme ».

L'algorithme est `VerificationNs::compareImages`, déclaré dans
`include/backend/algorithms/verification/final_verification.h` et implémenté dans
`src/backend/algorithms/verification/final_verification.cpp`. Pour l'ajouter à un pipeline, utiliser le step
facultatif `final-verification`, décrit dans [FinalVerificationStep.md](../../steps/FinalVerificationStep.md).

## Installation et dépendances

Aucune installation supplémentaire n'est nécessaire pour cette fonctionnalité : elle réutilise les dépendances
déjà intégrées au backend, notamment libtiff. Aucun moteur de gestion des couleurs n'est installé ou requis.
Pour préparer et compiler le projet, suivre les instructions générales de [BUILD.md](../../BUILD.md) pour le système
concerné. Ne pas installer une bibliothèque ou un paquet propre à la vérification.

## Entrées acceptées

| Entrée | Formats acceptés | Détails |
|---|---|---|
| Image de référence | TIFF (`.tif`, `.tiff`) ou JPEG (`.jpg`, `.jpeg`) | Image fournie par le client. |
| Image reconstruite | TIFF (`.tif`, `.tiff`) uniquement | TIFF final après reconstruction/export. |

Les extensions ne sont pas sensibles à la casse. Les deux fichiers doivent exister et être lisibles au moment de
l'exécution. Il faut donner les chemins des fichiers, pas ceux des dossiers.

L'algorithme lit les en-têtes et métadonnées; il ne charge ni ne compare les pixels. Les dimensions rapportées sont
celles enregistrées dans le fichier, sans appliquer de rotation selon l'orientation EXIF/TIFF.

## Utilisation dans un pipeline

Le step est facultatif. Ajouter `final-verification` au script après que le TIFF reconstruit a été créé et que les
deux chemins sont connus. Les chemins sont des paramètres `string`. Comme tout paramètre absent prend sa valeur
par défaut vide, il faut fournir les deux chemins.

```json
{
  "pipeline": {
    "steps": [
      {
        "name": "final-verification",
        "parameters": [
          {
            "valueType": "string",
            "name": "inputImagePath",
            "value": "C:/oeuvre/reference.tif"
          },
          {
            "valueType": "string",
            "name": "outputImagePath",
            "value": "C:/oeuvre/export/reconstruction.tif"
          }
        ]
      }
    ]
  }
}
```

Les `/` dans les chemins de cet exemple sont acceptés par Windows et évitent les séquences d'échappement JSON.
Remplacer les deux chemins par les chemins réels. Le fichier `outputImagePath` doit déjà être le TIFF final au
moment où le step s'exécute. Le pipeline du backend n'effectue pas lui-même la reconstruction/export de l'image
dans cet exemple : l'application appelante doit produire le fichier avant la vérification.

Le step est enregistré dans `PipelineNs::buildPipeline` sous le nom exact `final-verification`. Il n'a pas d'entrée
de DataStore; il lit ses deux fichiers à partir des paramètres, puis publie son rapport sous la clé
`verificationReport`.

### Lire le résultat

Après une exécution réussie, le rapport complet est disponible de deux façons :

```cpp
const auto& report =
    pipeline.getDataStore().get<DataNs::VerificationReport>("verificationReport");
const nlohmann::json& details = report.details;
```

Le step expose également le rapport avec `getResult()`. En cas de succès, celui-ci contient le rapport JSON. En cas
d'erreur, le step renvoie `false` et place le message explicatif dans `getResult()["error"]`; le pipeline s'arrête
selon son comportement normal face à un step en échec.

## Utilisation directe de l'algorithme

Pour un appel C++ sans step ni DataStore, inclure l'en-tête de l'algorithme :

```cpp
#include "algorithms/verification/final_verification.h"

#include <filesystem>
#include <nlohmann/json.hpp>

const nlohmann::json report = VerificationNs::compareImages(
    std::filesystem::path("C:/oeuvre/reference.jpg"),
    std::filesystem::path("C:/oeuvre/export/reconstruction.tif"));
```

La fonction retourne un objet JSON. Elle lève `std::runtime_error` si un format n'est pas pris en charge, si un
fichier est illisible ou si ses métadonnées requises sont invalides/incomplètes. Le premier chemin doit désigner
un TIFF ou un JPEG; le second doit désigner un TIFF.

## Contenu du rapport

Le JSON a `status: "report_ready"` quand la lecture et la comparaison sont terminées. Ses principaux champs sont :

| Champ | Contenu |
|---|---|
| `inputImage`, `outputImage` | Chemin, format détecté et métadonnées lues pour chaque image. |
| `dimensions.input`, `dimensions.output` | Largeur et hauteur en pixels. |
| `dimensions.difference` | Écart signé `sortie - référence`, séparément pour la largeur et la hauteur. |
| `dimensions.outputToInputRatio` | Rapport sortie/référence pour la largeur et la hauteur, ainsi que le rapport des proportions (ratio largeur/hauteur de sortie divisé par celui de référence). |
| `metadataComparisons` | Pour chaque propriété comparée : valeur de référence, valeur de sortie et booléen `equal`. |
| `iccProfile` | Présence, taille en octets, identité exacte des profils et état de comparaison colorimétrique. |
| `differences` | Liste des propriétés dont les valeurs diffèrent ou qui sont présentes d'un seul côté. Une différence n'est pas un verdict. |
| `warnings` | Avertissements, notamment l'absence de profil ICC ou l'impossibilité de déterminer l'équivalence colorimétrique. |

Exemple simplifié :

```json
{
  "status": "report_ready",
  "dimensions": {
    "input": { "width": 1000, "height": 800 },
    "output": { "width": 1200, "height": 800 },
    "difference": { "width": 200, "height": 0 },
    "outputToInputRatio": {
      "width": 1.2,
      "height": 1.0,
      "aspectRatio": 1.2
    }
  },
  "iccProfile": {
    "inputPresent": true,
    "outputPresent": true,
    "inputBytes": 3144,
    "outputBytes": 3144,
    "identicalBytes": false,
    "colorimetricEquivalence": "colorimetric_equivalence_undetermined"
  }
}
```

Les écarts de dimensions sont informatifs : aucun seuil, redimensionnement, correction ou échec de conformité
n'est appliqué. Un ratio supérieur à `1` indique que la mesure de sortie est plus grande que celle de référence.

## Métadonnées inspectées et limites

- **TIFF** : dimensions, bits par échantillon, nombre d'échantillons, format d'échantillon, interprétation
  photométrique, compression, orientation, configuration planaire, échantillons supplémentaires, profil ICC,
  tags texte courants (description, nom du document, marque, modèle, logiciel, date, artiste, droits d'auteur) et
  présence/état EXIF avec dates originales et numérisées quand disponibles.
- **JPEG** : dimensions, précision et nombre de composantes du frame, marqueur de compression, version et taille
  de miniature JFIF, présence et champs usuels EXIF, commentaires, segments APP et profil ICC réparti en segments.
  Les segments JPEG comparés sont signalés par marqueur, taille et égalité exacte des octets.
- Les fichiers TIFF sont inspectés dans leur premier répertoire d'image. Les tags propriétaires arbitraires ne sont
  pas décodés. Les champs non présents ou non lisibles peuvent apparaître comme `null`, `false` ou absents selon le
  champ.
- Les DPI/PPI et densités physiques JFIF ne sont pas lus ni comparés.
- Les pixels, leur apparence, leur qualité, leur netteté et leur fidélité visuelle ne sont pas évalués.
- Si les profils ICC sont identiques octet par octet, `identicalBytes` vaut `true` et
  `colorimetricEquivalence` vaut `identical_profile_data`. Si un profil manque, ou si les profils diffèrent, le
  rapport décrit la situation sans conclure que le rendu colorimétrique est différent. Pour deux profils présents
  mais différents, l'équivalence vaut `colorimetric_equivalence_undetermined`, car aucun moteur de gestion des
  couleurs n'est utilisé.

## Checkpoints et reprise

Le step sérialise son résultat dans le checkpoint sous `verificationReport.json`. Il déclare aussi les deux images
comme entrées externes : le checkpoint tient compte de leurs chemins, tailles et dates de modification. Si un
fichier change d'une manière détectée par cette empreinte, le checkpoint du step n'est plus réutilisé. Ce mécanisme
sert à la reprise du pipeline; il ne remplace pas une copie de sauvegarde des fichiers.

## Tests

Le test ciblé est `test_final_verification_step` dans `tests/steps/test_final_verification_step.cpp`. Il couvre les
dimensions/ratios, les métadonnées TIFF, les profils ICC identiques et différents, les segments ICC JPEG, la
publication du résultat, les checkpoints et les erreurs de lecture.
