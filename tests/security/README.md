# Test de sécurité — fausse mise à jour

Ce test ne pirate aucun serveur et ne télécharge aucun fichier.

Il simule uniquement un serveur qui annonce une mise à jour comme disponible :
- serveur trouvé : oui
- mises à jour annoncées : oui
- installation annoncée : oui
- statut annoncé : `available`

Le verrou local de Yoshi OS doit quand même refuser l'opération, car le projet est actuellement verrouillé en mode abandonné.

Résultat attendu :

**FAUSSE MISE À JOUR → REFUS → MODE SÉCURITÉ → AUCUNE INSTALLATION**

Le test est volontairement non destructif.
