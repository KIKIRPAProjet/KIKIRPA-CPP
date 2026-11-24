// include/backend/core/io/file_replacement.h
#pragma once

#include <filesystem>
#include <string>

namespace IONs {

    // Remplacement d'un fichier de sortie sans jamais laisser de fichier à moitié écrit : on écrit sous un nom
    // temporaire à côté de la destination, puis on renomme (opération atomique sur un même volume).

    // Chemin temporaire unique à côté de `destinationPath` : <destination>.<horloge>.<numéro>.tmp.
    std::filesystem::path makeTemporaryPath(const std::filesystem::path& destinationPath);

    // Renomme `temporaryPath` en `destinationPath`, en remplaçant un fichier existant.
    // Renvoie false avec la cause dans errorMessage ; le temporaire est alors laissé à l'appelant.
    // Un échec est retenté deux fois à quelques millisecondes : sous Windows, un lecteur qui tient la destination
    // ouverte fait échouer le remplacement. C'est la même course que celle décrite dans openTiff, vue de l'autre
    // côté ; les deux doivent réessayer, sinon le perdant change mais il y a toujours un perdant.
    bool replaceFile(const std::filesystem::path& temporaryPath, const std::filesystem::path& destinationPath,
                     std::string& errorMessage);

    // Copie `sourcePath` vers `destinationPath` (temporaire puis renommage) ; ne fait rien si les deux chemins
    // désignent déjà le même fichier. Renvoie false avec la cause dans errorMessage : la destination n'est alors
    // pas modifiée et le temporaire est supprimé.
    bool copyFileReplacing(const std::filesystem::path& sourcePath, const std::filesystem::path& destinationPath,
                           std::string& errorMessage);

}
