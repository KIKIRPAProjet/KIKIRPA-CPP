# To Do

Reprise en cours de route : voir readme.md section 10. Générateur de pipeline.json : section 9.

- [x] Décider si on garde uniquement la liste des noms des steps ou si on garde l'index du step courant -> liste de noms (`completedSteps`) ;
- [x] Ajout du champ run dans le JSON (`status`, `completedSteps`, `skip`, `from`, `checkpointDir`) ;
- [x] Reprise après crash / pause, et reprise depuis un step intermédiaire (`from`) ;
- [ ] Ajouter un sérialiseur (`Checkpoint.cpp`) pour chaque nouveau type de donnée : Alignments, Positions, Seams, Mosaic ;
- [ ] Frontend : générer le script (pipeline + run) et le relire pour afficher l'avancement ;
- [ ] Optimisation : checkpoint d'import par référence aux fichiers sources ;
- [ ] Valider `Option.type` au chargement.
- [x] Programme backend `generate_pipeline` qui génère pipeline.json depuis les arguments de la console ;
- [ ] Déclarer les valeurs autorisées par option (ex. seamFinder : graphcut/voronoi) pour les valider et les afficher dans `--list` ;
