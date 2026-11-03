# Infos sur cette branche (Mathias)

> [INFO]
> [Lien vers les notes sur la branche](https://s.craft.me/svSHAm7ZxXqNxe).

## To Do

- Le **TIFF pyramidal n’est pas encore généré** (voir _// TODO : écrire ici le TIFF tuilé pyramidal_) et le chemin stocké dans Tuile est vide (`fs::path{}`) ;
- Les blocs ne sont pas encore lisibles à la demande (avec une fonction `readRegion(tuile, niveau, rectangle)`) ;
- L’overlap n’est pas calculé par cette branche.

## Notes générales

> [!WARNING]
> Attention, il y a bien une différence entre Tiles et TileImages
> Actuellement, la différence est la suivante :
> | Type | Contenu | Usage |
> |---|---|---|
> | `Tiles` | `std::vector<std::shared_ptr<Tuile>>` | Collection officielle des images à assembler dans le pipeline |
> | `TileImages` | `std::vector<TileImage>` | Collection temporaire d’images réellement chargées en mémoire |
