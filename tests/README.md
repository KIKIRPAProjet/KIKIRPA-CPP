# Scénarios JSON au format name/options

Les 24 scénarios utilisent `pipeline.steps`, avec `name` et une liste `options` contenant `type`, `key` et `value`. Le manifeste décrit les résultats attendus et n’est pas un pipeline.

**Dernière exécution : 24 scénarios conformes, 7 acceptés et 17 refusés. Les 28 tests CTest passent, dont les 32 scénarios unitaires du moteur générique.**

L’ordre de la liste est conservé. Un prérequis doit apparaître avant son utilisateur, sans devoir être immédiatement adjacent. Sans identifiants distincts, les noms répétés sont refusés. Les cycles et références explicites restent testés dans la suite C++ de l’ancien format, puisqu’ils ne sont pas exprimables dans ce format séquentiel.

| Fichier | Résultat observé | Codes observés | Rapport |
| --- | --- | --- | --- |
| [01_import_seul.json](json/01_import_seul.json) | Accepté | Aucun | [JSON](reports/01_import_seul.json) |
| [02_pipeline_complet.json](json/02_pipeline_complet.json) | Accepté | Aucun | [JSON](reports/02_pipeline_complet.json) |
| [03_pipeline_minimal.json](json/03_pipeline_minimal.json) | Accepté | Aucun | [JSON](reports/03_pipeline_minimal.json) |
| [04_optionnelle_absente.json](json/04_optionnelle_absente.json) | Accepté | Aucun | [JSON](reports/04_optionnelle_absente.json) |
| [05_alternative_correction.json](json/05_alternative_correction.json) | Accepté | Aucun | [JSON](reports/05_alternative_correction.json) |
| [06_export_apres_fusion.json](json/06_export_apres_fusion.json) | Accepté | Aucun | [JSON](reports/06_export_apres_fusion.json) |
| [07_seamline_sans_correction.json](json/07_seamline_sans_correction.json) | Accepté | Aucun | [JSON](reports/07_seamline_sans_correction.json) |
| [08_import_absent.json](json/08_import_absent.json) | Refusé | MISSING_DEPENDENCY | [JSON](reports/08_import_absent.json) |
| [09_import_apres_stitching.json](json/09_import_apres_stitching.json) | Refusé | MISSING_DEPENDENCY | [JSON](reports/09_import_apres_stitching.json) |
| [10_stitching_absent.json](json/10_stitching_absent.json) | Refusé | MISSING_DEPENDENCY | [JSON](reports/10_stitching_absent.json) |
| [11_positioning_absent.json](json/11_positioning_absent.json) | Refusé | MISSING_DEPENDENCY | [JSON](reports/11_positioning_absent.json) |
| [12_type_inconnu.json](json/12_type_inconnu.json) | Refusé | UNKNOWN_STEP_TYPE | [JSON](reports/12_type_inconnu.json) |
| [13_nom_duplique.json](json/13_nom_duplique.json) | Refusé | DUPLICATE_STEP_ID | [JSON](reports/13_nom_duplique.json) |
| [14_configuration_invalide.json](json/14_configuration_invalide.json) | Refusé | INVALID_CONFIGURATION | [JSON](reports/14_configuration_invalide.json) |
| [15_type_option_incorrect.json](json/15_type_option_incorrect.json) | Refusé | INVALID_CONFIGURATION | [JSON](reports/15_type_option_incorrect.json) |
| [16_type_option_inconnu.json](json/16_type_option_inconnu.json) | Refusé | INVALID_CONFIGURATION | [JSON](reports/16_type_option_inconnu.json) |
| [17_cle_option_dupliquee.json](json/17_cle_option_dupliquee.json) | Refusé | INVALID_CONFIGURATION | [JSON](reports/17_cle_option_dupliquee.json) |
| [18_options_non_tableau.json](json/18_options_non_tableau.json) | Refusé | INVALID_CONFIGURATION, INVALID_SCHEMA | [JSON](reports/18_options_non_tableau.json) |
| [19_option_incomplete.json](json/19_option_incomplete.json) | Refusé | INVALID_CONFIGURATION, INVALID_SCHEMA | [JSON](reports/19_option_incomplete.json) |
| [20_vide.json](json/20_vide.json) | Refusé | INVALID_SCHEMA | [JSON](reports/20_vide.json) |
| [21_regex_invalide.json](json/21_regex_invalide.json) | Refusé | INVALID_CONFIGURATION | [JSON](reports/21_regex_invalide.json) |
| [22_plusieurs_erreurs.json](json/22_plusieurs_erreurs.json) | Refusé | INVALID_CONFIGURATION, MISSING_DEPENDENCY, UNKNOWN_STEP_TYPE | [JSON](reports/22_plusieurs_erreurs.json) |
| [23_nom_absent.json](json/23_nom_absent.json) | Refusé | INVALID_SCHEMA | [JSON](reports/23_nom_absent.json) |
| [24_json_malforme.json](json/24_json_malforme.json) | Refusé | INVALID_JSON | [JSON](reports/24_json_malforme.json) |

## Relancer

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "^json-" --output-on-failure
./build/pipeline-validator validate tests/json/02_pipeline_complet.json
./build/pipeline-validator validate tests/json/09_import_apres_stitching.json
```

Les rapports frais sont générés dans `build/reports/`. Les copies conservées dans `tests/reports/` correspondent à cette exécution. Aucun traitement ni dataset réel n’est nécessaire.
