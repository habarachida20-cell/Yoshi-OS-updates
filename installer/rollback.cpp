// ===========================================================================
// MonOS Rollback — protection contre les mises a jour qui ne demarrent pas.
//
// Le bootloader appelle rollback_on_boot() tres tot, avant de lancer le
// systeme du slot "pending".
//
//   - si aucun pendage : rien a faire ;
//   - si le systeme tourne mal (pas de confirmation, echecs repetes),
//     bootAttempts descend jusqu'a 0 puis on roule en arriere vers la
//     derniere version stable et on exige Recovery ;
//   - le systeme confirme sa stabilite via rollback_confirm_slot() une fois
//     demarre correctement (appele par le robot/OS apres son init).
//
// Toutes les decisions s'appuient sur le blob de metadonnees (bootmeta.h),
// jamais sur des heuristiques de fichiers.
// ===========================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>

#include "bootmeta.h"
#include "../updater/version.h"

namespace monupd {

enum class BootOutcome {
  kNoPending,        // rien en attente : demarrer le slot actif
  kBootPending,      // slot pending + compteur restant : demarrerne le slot
  kRollbackNeeded,   // echecs epuises : revenir a la derniere stable
  kRecoveryRequired  // echec catastrophique : Recovery obligatoire
};

static const char* outcomeName(BootOutcome o) {
  switch(o){
    case BootOutcome::kNoPending:       return "NO_PENDING";
    case BootOutcome::kBootPending:     return "BOOT_PENDING";
    case BootOutcome::kRollbackNeeded:  return "ROLLBACK_NEEDED";
    case BootOutcome::kRecoveryRequired:return "RECOVERY_REQUIRED";
  }
  return "?";
}

static int save(BootMeta& m) {
  journal_append((std::string("ROLLBACK: attempts=") +
                  std::to_string((int)m.bootAttempts) +
                  " serial=" + std::to_string(m.bootSerial)).c_str());
  return bootmeta_save(m);
}

// ---------------------------------------------------------------------------
// Appele au tout debut du boot (par le bootloader ou main) : decide quel
// slot demarrer et s'il faut rollbacker.
// ---------------------------------------------------------------------------
BootOutcome rollback_on_boot(BootMeta& meta) {
  if(bootmeta_load(meta) != 0) {
    bootmeta_init(meta);
    bootmeta_save(meta);
    return BootOutcome::kNoPending;
  }

  // Il n'y a pas d'echec de mise a jour si on a deja valide le slot.
  if(meta.nextSlot == kSlotNone || meta.nextSlot == meta.activeSlot) {
    meta.pendingVersion[0] = 0;
    if(meta.recoveryRequired) return BootOutcome::kRecoveryRequired;
    return BootOutcome::kNoPending;
  }

  // Une mise a jour est en attente de validation sur meta.nextSlot.
  if(meta.bootAttempts > 0) {
    // Ce boot tente le slot pending. On n'attribue pas l'echec tout de suite :
    // c'est le robot, une fois le systeme OPERE, qui appellera
    // rollback_confirm_slot(). S'il ne le fait pas avant les prochains boots,
    // le compteur tombe a 0 et on rollbacke.
    --meta.bootAttempts;
    save(meta);
    return BootOutcome::kBootPending;
  }

  // attempts epuise : on abandonne le nouveau slot.
  journal_append((std::string("ROLLBACK: abandon de ")
                  + meta.pendingVersion).c_str());

  // Basculer le slot actif sur l'ancien systeme stable.
  int previous = meta.activeSlot;
  if(meta.nextSlot == previous) { // cas incoherent : on force Recovery
    meta.recoveryRequired = 1;
    save(meta);
    return BootOutcome::kRecoveryRequired;
  }
  meta.activeSlot    = previous;          // l'ancien systeme RESTE en place
  meta.nextSlot      = kSlotNone;
  meta.pendingVersion[0] = 0;
  meta.bootAttempts  = 0;
  meta.recoveryRequired = 1;              // Recovery informe de l'echec
  save(meta);
  journal_append("ROLLBACK: dernier systeme stable restaure");
  return BootOutcome::kRollbackNeeded;
}

// ---------------------------------------------------------------------------
// Confirme que le slot courant est fonctionnel : valide la mise a jour.
// Appele par l'OS apres un demarrage complet et par le robot apres un "check".
// ---------------------------------------------------------------------------
int rollback_confirm_slot() {
  BootMeta meta;
  if(bootmeta_load(meta) != 0 || !bootmeta_valid(meta)) return -1;
  if(meta.nextSlot != kSlotNone && meta.nextSlot != meta.activeSlot) {
    // La mise a jour demarre : on la valide, l'ancien systeme reste une
    // sauvegarde temporaire (previousVersion).
    meta.activeSlot = meta.nextSlot;
    meta.nextSlot   = kSlotNone;
    meta.bootAttempts = 0;
    meta.recoveryRequired = 0;
    std::snprintf(meta.previousVersion, sizeof(meta.previousVersion), "%s",
                  meta.currentVersion);
    std::snprintf(meta.currentVersion, sizeof(meta.currentVersion), "%s",
                  meta.pendingVersion);
    meta.pendingVersion[0] = 0;
    bootmeta_save(meta);
    journal_append("Rollback: nouvelle version confirmee comme stable");
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Fin de mise a jour : remet tout l'etat de demarrage a jour.
// ---------------------------------------------------------------------------
static int commit_pending(const BootMeta& pending) {
  BootMeta meta;
  if(bootmeta_load(meta) != 0 || !bootmeta_valid(meta)) return -1;
  meta.nextSlot         = pending.nextSlot;
  meta.bootAttempts     = pending.bootAttempts;
  meta.bootSerial       = pending.bootSerial;
  meta.updateId         = pending.updateId;
  std::snprintf(meta.pendingVersion, sizeof(meta.pendingVersion), "%s",
                pending.pendingVersion);
  return bootmeta_save(meta);
}

// ---------------------------------------------------------------------------
// Restaure la derniere version stable (menue Recovery #1).
// ---------------------------------------------------------------------------
int rollback_restore_last_stable() {
  BootMeta meta;
  if(bootmeta_load(meta) != 0 || !bootmeta_valid(meta)) return -1;
  if(meta.nextSlot == kSlotNone) {
    std::printf("Aucune mise a jour en attente : rien a restaurer.\n");
    return 0;
  }
  meta.nextSlot = kSlotNone;
  meta.bootAttempts = 0;
  meta.recoveryRequired = 0;
  meta.pendingVersion[0] = 0;
  int rc = bootmeta_save(meta);   // demarre l'ancien slot au prochain boot
  journal_append("Recovery: restauration de la derniere version stable");
  std::printf("Recovery: prochain demarrage sur la derniere version stable.\n");
  return rc;
}

// ---------------------------------------------------------------------------
// Annule la derniere mise a jour (menue Recovery #5).
// ---------------------------------------------------------------------------
int rollback_cancel_last_update() {
  BootMeta meta;
  if(bootmeta_load(meta) != 0 || !bootmeta_valid(meta)) return -1;
  meta.nextSlot = kSlotNone;
  meta.bootAttempts = 0;
  meta.recoveryRequired = 0;
  meta.pendingVersion[0] = 0;
  int rc = bootmeta_save(meta);
  journal_append("Recovery: derniere mise a jour annulee");
  std::printf("Recovery: derniere mise a jour annulee.\n");
  return rc;
}

// ---------------------------------------------------------------------------
// Repare le demarrage (menue Recovery #4) : purge le slot pending incomplet
// et force le slot stable.
// ---------------------------------------------------------------------------
int rollback_repair_boot() {
  BootMeta meta;
  if(bootmeta_load(meta) != 0 || !bootmeta_valid(meta)) return -1;
  int stable = meta.activeSlot;
  meta.nextSlot = kSlotNone;
  meta.bootAttempts = 0;
  meta.recoveryRequired = 0;
  if(meta.nextSlot == kSlotNone && stable >= 0) meta.activeSlot = stable;
  meta.pendingVersion[0] = 0;
  int rc = bootmeta_save(meta);
  journal_append("Recovery: demarrage repare (slot stable impose)");
  std::printf("Recovery: demarrage repare.\n");
  return rc;
}

// ---------------------------------------------------------------------------
// Utilitaires exposes au bootloader pour tester la cohérence du slot courant.
// ---------------------------------------------------------------------------
int rollback_slot_has_valid_image(int slot) {
  // TODO(noyau MonOS) : verifier l'en-tete magique + SHA-256 stocke dans le
  // slot. Retourner 1 si valide, 0 si vide/incomplet, -1 si erreur d'acces.
  (void)slot;
  return 1;
}

bool rollback_info_pending() {
  BootMeta meta;
  if(bootmeta_load(meta) != 0 || !bootmeta_valid(meta)) return false;
  return meta.nextSlot != kSlotNone && meta.nextSlot != meta.activeSlot;
}

} // namespace monupd

#ifndef MONOS_UPD_ROLLBACK_LINK
int main(int argc, char** argv) {
  using namespace monupd;
  BootMeta meta;
  if(argc > 1 && std::strcmp(argv[1], "confirm") == 0)
    return rollback_confirm_slot();
  if(argc > 1 && std::strcmp(argv[1], "status") == 0) {
    bootmeta_load(meta);
    std::printf("active=%d next=%d attempts=%u recovery=%u\n",
                meta.activeSlot, meta.nextSlot, meta.bootAttempts,
                meta.recoveryRequired);
    return 0;
  }
  BootOutcome o = rollback_on_boot(meta);
  std::printf("rollback: %s\n", outcomeName(o));
  return 0;
}
#endif