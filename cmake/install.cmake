# Installation de l'exécutable

# Définit les règles d'installation de l'exécutable principal.
#
# RUNTIME DESTINATION bin indique que le binaire sera installé
# dans le répertoire bin du préfixe d'installation.
#
# Exemple : <prefix>/bin/stitching
# Le nom `stitching` est provisoire : il changera avec le nom définitif du programme.
install(TARGETS stitching
    RUNTIME DESTINATION bin
)
