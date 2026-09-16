// ===========================================================================
// MonOS Rollback — protection contre les mises a jour qui ne demarrent pas.
// Yoshi OS abandonne : aucune mise a jour pending ne peut etre validee.
// ===========================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>

#include "bootmeta.h"
#include "../updater/version.h"
#include "../updater/emergency-lock.h"

namespace monupd {

enum class BootOutcome {
  kNoPending,
  kBootPending,
  kRollbackNeeded,
  kRecoveryRequired
};

static const char* outcomeName(BootOutcome o) {
  switch(o){
    case BootOutcome::kNoPending: return "NO_PENDING";
    case BootOutcome::kBootPending: return "BOOT_PENDING";
    case BootOutcome::kRollbackNeeded: return "ROLLBACK_NEEDED";
    case BootOutcome::kRecoveryRequired: return "RECOVERY_REQUIRED";
  }
  return "?";
}

static int save(BootMeta& m) {
  journal_append((std::string("ROLLBACK: attempts=") +
                  std::to_string((int)m.bootAttempts) +
                  " serial=" + std::to_string(m.bootSerial)).c_str());
  return bootmeta_save(m);
}

BootOutcome rollback_on_boot(BootMeta& meta) {
  if(bootmeta_load(meta) != 0) {
    bootmeta_init(meta);
    bootmeta_save(meta);
    return BootOutcome::kNoPending;
  }

  // Verrou d'urgence : aucune version pending ne peut demarrer.
  // Le slot actif (derniere version stable) reste la cible du boot.
  if(yoshiEmergencyLockActive()) {
    meta.nextSlot = kSlotNone;
    meta.bootAttempts = 0;
    meta.pendingVersion[0] = 0;
    meta.recoveryRequired = 0;
    save(meta);
    journal_append("EMERGENCY LOCK: pending update cancelled; stable slot retained");
    return BootOutcome::kNoPending;
  }

  if(meta.nextSlot == kSlotNone || meta.nextSlot == meta.activeSlot) {
    meta.pendingVersion[0] = 0;
    if(meta.recoveryRequired) return BootOutcome::kRecoveryRequired;
    return BootOutcome::kNoPending;
  }

  if(meta.bootAttempts > 0) {
    --meta.bootAttempts;
    save(meta);
    return BootOutcome::kBootPending;
  }

  journal_append((std::string("ROLLBACK: abandon de ") + meta.pendingVersion).c_str());
  int previous = meta.activeSlot;
  if(meta.nextSlot == previous) {
    meta.recoveryRequired = 1;
    save(meta);
    return BootOutcome::kRecoveryRequired;
  }
  meta.activeSlot = previous;
  meta.nextSlot = kSlotNone;
  meta.pendingVersion[0] = 0;
  meta.bootAttempts = 0;
  meta.recoveryRequired = 1;
  save(meta);
  journal_append("ROLLBACK: dernier systeme stable restaure");
  return BootOutcome::kRollbackNeeded;
}

int rollback_confirm_slot() {
  // Une version ne peut plus etre confirmee comme stable apres abandon.
  if(yoshiEmergencyLockActive()) {
    journal_append("EMERGENCY LOCK: confirmation of update refused");
    return 125;
  }
  BootMeta meta;
  if(bootmeta_load(meta) != 0 || !bootmeta_valid(meta)) return -1;
  if(meta.nextSlot != kSlotNone && meta.nextSlot != meta.activeSlot) {
    meta.activeSlot = meta.nextSlot;
    meta.nextSlot = kSlotNone;
    meta.bootAttempts = 0;
    meta.recoveryRequired = 0;
    std::snprintf(meta.previousVersion, sizeof(meta.previousVersion), "%s", meta.currentVersion);
    std::snprintf(meta.currentVersion, sizeof(meta.currentVersion), "%s", meta.pendingVersion);
    meta.pendingVersion[0] = 0;
    bootmeta_save(meta);
    journal_append("Rollback: nouvelle version confirmee comme stable");
  }
  return 0;
}

int rollback_restore_last_stable() {
  BootMeta meta;
  if(bootmeta_load(meta) != 0 || !bootmeta_valid(meta)) return -1;
  meta.nextSlot = kSlotNone;
  meta.bootAttempts = 0;
  meta.recoveryRequired = 0;
  meta.pendingVersion[0] = 0;
  int rc = bootmeta_save(meta);
  journal_append("Recovery: restauration de la derniere version stable");
  std::printf("Recovery: prochain demarrage sur la derniere version stable.\n");
  return rc;
}

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

int rollback_slot_has_valid_image(int slot) {
#if defined(MONOS_UPD_KERNEL)
  if(slot != kSlotA && slot != kSlotB) return -1;
  BootMeta head;
  int rc = monos_disk_read(slot, 0u, &head, sizeof(head));
  if(rc != MONOS_OK) return -1;
  return bootmeta_valid(head) ? 1 : 0;
#else
  (void)slot;
  return 1;
#endif
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
