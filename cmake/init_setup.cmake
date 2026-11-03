# Installation de l'exécutable

# Définit les règles d'installation de l'exécutable principal.
#
# RUNTIME DESTINATION bin indique que le binaire sera installé
# dans le répertoire bin du préfixe d'installation.
#
# Exemple : <prefix>/bin/stitching
# En gros c'est le nom du programme à chnagé mais voir ce que l'on veut.
install(TARGETS stitching
    RUNTIME DESTINATION bin
)
