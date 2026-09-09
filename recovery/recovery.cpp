// ===========================================================================
// MonOS Recovery — logique de secours.
//
// Se presente quand :
//   - rollback_on_boot() a epuise ses tentatives (recoveryRequired=1) ;
//   - un slot pending est incomplet ;
//   - l'utilisateur invoque Recovery (touche dediee au boot).
//
// Ne depend que de bootmeta.h + des fonctions de rollback ; pas du systeme
// de fichiers complet (assure le fonctionnement meme si MonOS ne boote pas).
// ===========================================================================

#include <cstdio>
#include <cstring>

#include "../installer/bootmeta.h"
#include "../updater/version.h"
#if defined(MONOS_UPD_KERNEL)
#include "monos_kernel_api.h"
#endif

// Prototypes des fonctions de rollback.cpp (l'arborescence specifique ne
// prevoit pas de .h dans installer/ ; ce point sera centralise dans le noyau).
namespace monupd {
int rollback_restore_last_stable();
int rollback_cancel_last_update();
int rollback_repair_boot();
int rollback_confirm_slot();
}

namespace monupd {

// ---------------------------------------------------------------------------
// Detecte une situation de Recovery sans interaction utilisateur.
// ---------------------------------------------------------------------------
bool recovery_needed() {
  BootMeta meta;
  if(bootmeta_load(meta) != 0) return false;
  return meta.recoveryRequired != 0;
}

// ---------------------------------------------------------------------------
// S'il y avait une mise a jour en attente et que le slot local est invalide,
// on bascule vers le slot stable avant la partie graphique.
// ---------------------------------------------------------------------------
int recovery_stage1_auto_patch() {
  BootMeta meta;
  if(bootmeta_load(meta) != 0 || !bootmeta_valid(meta)) return -1;

  if(meta.recoveryRequired) {
    std::printf("Recovery: erreur detecte a la derniere mise a jour.\n");
    // basculer sur l'ancien systeme stable : nextSlot -> none
    meta.nextSlot = kSlotNone;
    meta.bootAttempts = 0;
    meta.pendingVersion[0] = 0;
    meta.recoveryRequired = 0;
    return bootmeta_save(meta);
  }
  if(meta.nextSlot != kSlotNone && meta.nextSlot != meta.activeSlot) {
    // slot pending non valide -> on revient au dernier stable sans demander.
    std::printf("Recovery: slot en attente invalide, retour a la version stable.\n");
    meta.nextSlot = kSlotNone;
    meta.bootAttempts = 0;
    meta.pendingVersion[0] = 0;
    return bootmeta_save(meta);
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Actions du menu Recovery (appelees par recovery-menu).
// ---------------------------------------------------------------------------
int recovery_restore_last_stable()         { return rollback_restore_last_stable(); }
int recovery_reinstall_update()            { return rollback_repair_boot(); }
int recovery_check_updates()               { return 0; } // TODO : appel robot "check"
int recovery_repair_boot()                 { return rollback_repair_boot(); }
int recovery_cancel_last_update()          { return rollback_cancel_last_update(); }
int recovery_reboot() {
#if defined(MONOS_UPD_KERNEL)
  monos_reboot();
#endif
  return 0;
}
int recovery_contact_support()             { std::printf("https://github.com/%s/%s/issues\n", kRepoOwner, kRepoName); return 0; }

} // namespace monupd