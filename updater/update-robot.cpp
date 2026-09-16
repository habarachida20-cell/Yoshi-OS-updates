// Yoshi OS Update Robot — projet abandonne / version non terminee.
// Le manifeste GitHub est la verite distante ; le verrou local est la derniere
// barriere avant tout telechargement ou toute installation.

#include <cstdio>
#include <string>
#include "emergency-lock.h"

namespace monupd {

static const char* kAbandonedMessage = yoshiEmergencyMessage();

static bool updatesEnabledFromServer(bool serverFound, bool updatesEnabled,
                                     const char* status, bool installEnabled) {
  if(!serverFound) return false;                 // fail-closed
  if(!updatesEnabled || !installEnabled) return false;
  if(status && status[0] != '\0' && std::string(status) == "abandoned") return false;
  return true;
}

int checkAndRunUpdater(bool serverFound, bool updatesEnabled,
                       const char* status, bool installEnabled) {
  // Le projet est abandonne : aucun chemin de telechargement/installation.
  if(yoshiEmergencyLockActive() ||
     !updatesEnabledFromServer(serverFound, updatesEnabled, status, installEnabled)) {
    std::printf("%s\n", kAbandonedMessage);
    return 0;
  }

  // Aucun chemin d'installation n'est disponible dans cette version.
  std::printf("%s\n", kAbandonedMessage);
  return 0;
}

int updateRobotMain(int argc, char** argv) {
  (void)argc;
  (void)argv;
  // Etat connu du manifeste actuel : abandonne, mises a jour et installation desactivees.
  return checkAndRunUpdater(true, false, "abandoned", false);
}

} // namespace monupd

#ifndef MONOS_UPD_ROBOT_LINK
int main(int argc, char** argv) {
  return monupd::updateRobotMain(argc, argv);
}
#endif
