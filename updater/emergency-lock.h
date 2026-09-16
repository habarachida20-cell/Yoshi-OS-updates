#ifndef YOSHI_OS_EMERGENCY_LOCK_H
#define YOSHI_OS_EMERGENCY_LOCK_H

namespace monupd {

// Défense locale : même si un appelant contourne le robot, l'installeur
// refuse toute écriture tant que le projet est abandonné.
static constexpr bool kYoshiOsAbandoned = true;
static constexpr bool kYoshiOsUpdatesEnabled = false;
static constexpr bool kYoshiOsInstallEnabled = false;

inline bool yoshiEmergencyLockActive() {
  return kYoshiOsAbandoned || !kYoshiOsUpdatesEnabled || !kYoshiOsInstallEnabled;
}

inline const char* yoshiEmergencyMessage() {
  return "Yoshi OS — Projet abandonné. Version non terminée. Aucune mise à jour n'est disponible.";
}

} // namespace monupd

#endif
