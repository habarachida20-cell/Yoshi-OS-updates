#include <cstdio>
#define MONOS_UPD_ROBOT_LINK
#include "../updater/update-robot.cpp"

int main() {
  // Simulation d'une fausse mise à jour : le serveur prétend que tout est actif.
  // Le verrou local doit néanmoins refuser le téléchargement et l'installation.
  int rc = monupd::checkAndRunUpdater(true, true, "available", true);
  if (rc != 0) {
    std::fprintf(stderr, "FAIL: le robot a retourné un code inattendu.\n");
    return 1;
  }
  std::puts("SECURITY TEST PASSED: fausse mise à jour refusée par le verrou local.");
  return 0;
}
