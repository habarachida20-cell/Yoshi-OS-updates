# 🚨 URGENCE — ALERTE SERVEUR YOSHI OS

## État
Le système de sécurité a détecté un problème avec le mécanisme de déclenchement des vérifications.

Le workflow GitHub Actions actuellement observé s'arrête en **startup_failure** avant l'exécution du robot. Les vérifications de sécurité ne doivent donc pas être considérées comme opérationnelles tant qu'un lancement réussi n'a pas été confirmé.

## Mode urgence

En cas de problème :

- aucune mise à jour réelle ne doit être téléchargée ;
- aucune mise à jour réelle ne doit être installée ;
- aucune fausse mise à jour ne doit modifier les vrais fichiers ;
- les fichiers suspects doivent rester isolés ;
- le système doit conserver les fichiers existants ;
- aucune suppression automatique ne doit être effectuée pour tenter une récupération.

## Version stable de récupération

Une version stable doit privilégier la sécurité et la conservation des données :

1. passer le serveur en **MODE URGENCE** ;
2. bloquer les opérations de mise à jour ;
3. conserver les fichiers et configurations existants ;
4. effectuer les tests uniquement dans un dossier temporaire isolé ;
5. vérifier que le workflow de sécurité démarre réellement ;
6. seulement après un test réussi, réactiver progressivement les fonctions nécessaires.

## Message à afficher

> 🚨 URGENT : problème détecté sur le serveur de mise à jour.  
> MODE URGENCE ACTIVÉ.  
> Les mises à jour et installations sont bloquées.  
> Les fichiers existants sont conservés.  
> Une version stable de récupération doit être utilisée.

## Important

Ce fichier est une alerte et une spécification de comportement. Il ne lance pas de boucle infinie et ne modifie pas le serveur à lui seul. Le blocage effectif doit être appliqué par le code de sécurité et le workflow validé.
