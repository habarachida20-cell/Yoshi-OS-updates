# Yoshi OS — Robot de sécurité permanent

Ce fichier définit le comportement attendu du robot de sécurité.

## Déclenchement

Le robot doit être lancé automatiquement par le workflow de sécurité du dépôt lors des modifications des fichiers de sécurité.

## Surveillance

À chaque lancement, il vérifie notamment :

- les fausses mises à jour ;
- les signatures absentes ou invalides ;
- les créateurs non reconnus ;
- les tentatives de téléchargement ;
- les tentatives d'installation ;
- les tentatives de modification des fichiers du système.

## Réaction

Une mise à jour suspecte doit être :

1. refusée ;
2. isolée dans une quarantaine temporaire ;
3. empêchée de modifier les vrais fichiers ;
4. signalée comme événement de sécurité.

## Important

Le robot est un mécanisme de test et de surveillance du dépôt. Il ne doit pas exécuter de code provenant d'une fausse mise à jour et ne doit jamais modifier les vrais fichiers du dépôt pendant un test.

Le workflow GitHub Actions reste contrôlé par GitHub : un workflow ne peut pas tourner en permanence sans fin. Le comportement permanent est donc obtenu par des déclenchements automatiques et des vérifications répétées, pas par une boucle infinie.
