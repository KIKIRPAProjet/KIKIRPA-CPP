# Notes pour exécuter le projet sur macOS

Consulter ce document en cas de problème de lancement lié aux bibliothèques ou plugins Qt !

Pour l'environnement de test et le développement local (l'exécutable généré étant `build/stitching` ou `build/macos/stitching`) :

```bash
export QT_PLUGIN_PATH=$(brew --prefix qtbase)/share/qt/plugins
export DYLD_FRAMEWORK_PATH=$(brew --prefix qtbase)/lib
./build/stitching
# ou avec le preset cmake :
# ./build/macos/stitching
```

On indique simplement à macOS où se trouvent les plugins et frameworks Qt installés par Homebrew pour la session de terminal active.

Si l'application est packagée sous forme de bundle macOS (`.app`) :

```bash
/opt/homebrew/opt/qtbase/bin/macdeployqt build/stitching.app -libpath=/opt/homebrew/lib
```
