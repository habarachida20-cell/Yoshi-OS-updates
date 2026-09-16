// Yoshi OS Update Robot — projet abandonné / version non terminée.
// Le serveur est consulté uniquement pour vérifier l'état du projet.
// Si le serveur confirme l'état "abandoned" ou "updates_enabled=false",
// aucune connexion de téléchargement, aucune recherche de release et aucune
// installation ne doit être effectuée.

#include <cstdio>

namespace monupd {

static const char* kAbandonedMessage =
  "Yoshi OS — Projet abandonné. Version non terminée. Aucune mise à jour n'est disponible.";

// État définitif : les mises à jour sont désactivées.
static bool updatesEnabledFromServer(bool serverFound, bool updatesEnabled,
                                     const char* status) {
  if(!serverFound) return false;
  if(!updatesEnabled) return false;
  if(status && status[0] != 0 && std::string(status) == "abandoned") return false;
  return true;
}

// Point d'entrée de contrôle : le client ne doit jamais installer une release
// lorsque le serveur indique que Yoshi OS est abandonné/non terminé.
int checkAndRunUpdater(bool serverFound, bool updatesEnabled,
                       const char* status) {
  if(!updatesEnabledFromServer(serverFound, updatesEnabled, status)) {
    std::printf("%s\n", kAbandonedMessage);
    return 0;
  }

  // Aucun chemin d'installation n'est disponible dans la version abandonnée.
  std::printf("%s\n", kAbandonedMessage);
  return 0;
}

} // namespace monupd
