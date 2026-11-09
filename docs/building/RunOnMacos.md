# Notes pour compiler le projet sur MacOS

Regarder à ce document si problème à cause de Qt !

Exécuter cette commande afin de placer les frameworks et plugins Qt directement à l'intérieur de Frontend.app :

```bash
/opt/homebrew/opt/qtbase/bin/macdeployqt build/Frontend.app -libpath=/opt/homebrew/lib
```

C'est ok pour un déploiement sur Mac. Pour l'environnement de test, on peut plutôt taper ceci :

```bash
export QT_PLUGIN_PATH=$(brew --prefix qtbase)/share/qt/plugins
export DYLD_FRAMEWORK_PATH=$(brew --prefix qtbase)/lib
./build/stitching
```

On dit simplement à macOS où se trouve les plugins et frameworks Qt de Homebrew pour la session de terminal active.
