// ===========================================================================
// Yoshi OS Update Robot — PROJET ABANDONNÉ
// ===========================================================================
// Le projet Yoshi OS est abandonné / non terminé.
// Les mises à jour sont volontairement désactivées.
// Ce programme ne contacte plus GitHub et n'installe aucune mise à jour.
// ===========================================================================

#include <cstdio>

int updateRobotMain(int /*argc*/, char** /*argv*/) {
  std::printf("Yoshi OS — Projet abandonné. Aucune mise à jour n'est disponible.\n");
  return 0;
}

#ifndef MONOS_UPD_ROBOT_LINK
int main(int argc, char** argv) {
  return updateRobotMain(argc, argv);
}
#endif
