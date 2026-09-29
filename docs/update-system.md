Yoshi OS — Système de mise à jour

## Statut final

Le système de mise à jour est désormais en mode archive.

- status = abandoned
- project_state = unfinished
- updates_enabled = false
- install_enabled = false

Lorsqu'un PC demande une vérification, le robot doit arrêter le processus avant tout téléchargement ou toute écriture.

## Comportement sur PC

1. Lire le manifeste.
2. Vérifier le statut du projet.
3. Si le statut est abandoned, ne télécharger aucune image.
4. Ne modifier aucun système installé.
5. Afficher le message final.
6. Conserver la version déjà installée.

Message final : Yoshi OS — Projet définitivement abandonné et non terminé. Aucune nouvelle mise à jour n'est disponible.

Le dépôt reste une archive du projet.