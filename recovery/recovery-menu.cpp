// ===========================================================================
// MonOS Recovery Menu — menu de secours simple, utilisable meme si MonOS
// ne demarre plus normalement.
//
//   1. Restaurer la derniere version stable
//   2. Reinstaller la mise a jour stable
//   3. Rechercher les mises a jour stables
//   4. Reparer le demarrage
//   5. Annuler la derniere mise a jour
//   6. Redemarrer
//   7. Contacter le support
//   0. Continuer (demarrer MonOS)
//
// Hote (tests PC) : entrees via scanf. Noyau MonOS : fonction monos_getch()
// fournie par la console kernel (TODO ci-dessous).
// ===========================================================================

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "../installer/bootmeta.h"

namespace monupd {

// Prototypes (recovery.cpp) — pas de .h dans l'arborescence spec.
int recovery_repair_boot();
int recovery_cancel_last_update();
int recovery_restore_last_stable();
int recovery_reinstall_update();
int recovery_check_updates();
int recovery_reboot();
int recovery_contact_support();

// Lecture clavier : kernel => monos_getch() (console serie non bloquante).
#if defined(MONOS_UPD_HOST)
static int readKey() { return std::getchar(); }
#else
#include "monos_kernel_api.h"
static int readKey() {
  return monos_getch();
}
#endif

static void printHeader() {
  std::printf("\n=============================================\n");
  std::printf("  MonOS Recovery v1.0\n");
  std::printf("=============================================\n");
}

static void printMenu() {
  std::printf("  1. Restaurer la derniere version stable\n");
  std::printf("  2. Reinstaller la mise a jour stable\n");
  std::printf("  3. Rechercher les mises a jour stables\n");
  std::printf("  4. Reparer le demarrage\n");
  std::printf("  5. Annuler la derniere mise a jour\n");
  std::printf("  6. Redemarrer\n");
  std::printf("  7. Contacter le support\n");
  std::printf("  0. Continuer (demarrer MonOS)\n");
}

int recoveryMenu() {
  printHeader();
  int choice = -1;
  while(choice != 0) {
    printMenu();
    std::printf("  Votre choix : ");
    choice = readKey();
    int c;
    while((c = readKey()) != '\n' && c != -1);  // vide la ligne (hote)
    switch(choice) {
      case '1': recovery_restore_last_stable(); break;
      case '2': recovery_reinstall_update();    break;
      case '3': recovery_check_updates();       break;
      case '4': recovery_repair_boot();         break;
      case '5': recovery_cancel_last_update();  break;
      case '6': recovery_reboot();              break;
      case '7': recovery_contact_support();     break;
      case '0': std::printf("Demarrage de MonOS...\n"); break;
      default : std::printf("Choix invalide.\n"); break;
    }
  }
  return 0;
}

} // namespace monupd

#ifndef MONOS_UPD_RECOVERY_MENU_LINK
int main() { return monupd::recoveryMenu(); }
#endif