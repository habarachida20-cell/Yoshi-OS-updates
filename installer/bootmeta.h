#ifndef MONOS_UPD_BOOTMETA_H
#define MONOS_UPD_BOOTMETA_H

// Metadonnees de demarrage couvrant le systeme A/B de MonOS.
//
// Architecture des slots (montee dans "docs/update-system.md") :
//   SLOT_A  -> systeme stable actuellement utilise (ou secondaire)
//   SLOT_B  -> systeme de remplacement (slot inactif)
//   RESCUE  -> micro-image de secours integree au bootloader
//
// Le blob ci-dessous est stocke dans un secteur logique dedie de l'image
// (appele "bloc de metadonnees"). Il est ecrit UNIQUEMENT par l'installeur,
// jamais par le systeme en cours de fonctionnement sur son propre slot.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace monupd {

constexpr uint32_t kBootMetaMagic      = 0x4D4F4E45u; // "MONE"
constexpr uint32_t kBootMetaStructVer  = 1u;
constexpr int      kMaxBootAttempts    = 3;           // echecs avant abandon
constexpr int      kSlotNone           = -1;
constexpr int      kSlotA              = 0;
constexpr int      kSlotB              = 1;

struct BootMeta {
  uint32_t magic;              // kBootMetaMagic si le blob est valide
  uint32_t structVersion;
  int      activeSlot;         // slot demarre actuellement
  int      nextSlot;           // slot a demarrer au prochain boot (-1 = aucun)
  uint8_t  recoveryRequired;   // 1 => Recovery doit se presenter
  uint8_t  reserved[3];
  uint32_t bootAttempts;       // nombre de boot du slot pending restants
  uint32_t bootSerial;         // incremente a chaque commit (anti double-comptage)
  uint32_t updateId;           // identifiant de la derniere mise a jour
  char     currentVersion[24]; // version stable actuelle
  char     pendingVersion[24]; // nouvelle version en cours de validation
  char     previousVersion[24];// derniere version stable avant mise a jour
};

inline void bootmeta_init(BootMeta& m) {
  std::memset(&m, 0, sizeof(m));
  m.magic         = kBootMetaMagic;
  m.structVersion = kBootMetaStructVer;
  m.activeSlot    = kSlotA;
  m.nextSlot      = kSlotNone;
  m.bootAttempts  = 0;
  m.bootSerial    = 0;
  m.updateId      = 0;
  std::snprintf(m.currentVersion,  sizeof(m.currentVersion),  "%s", "1.0.0");
  std::snprintf(m.pendingVersion,  sizeof(m.pendingVersion),  "%s", "");
  std::snprintf(m.previousVersion, sizeof(m.previousVersion), "%s", "");
}

inline bool bootmeta_valid(const BootMeta& m) {
  return m.magic == kBootMetaMagic && m.structVersion == kBootMetaStructVer;
}

// ---------------------------------------------------------------------------
// Hote (simulation/test sur PC, hors noyau) : le blob est un fichier.
// Noyau MonOS : remplacer par l'acces au bloc de metadonnees du disque.
// ---------------------------------------------------------------------------
#if !defined(MONOS_UPD_KERNEL)
inline const char* bootmeta_sim_path() {
  const char* p = std::getenv("MONOS_BOOTMETA");
  return (p && *p) ? p : "monos_bootmeta.cfg";
}
inline int bootmeta_save(const BootMeta& m) {
  if(!bootmeta_valid(m)) { return -1; }
  FILE* f = std::fopen(bootmeta_sim_path(), "wb");
  if(!f) return -2;
  size_t w = std::fwrite(&m, 1, sizeof(m), f);
  std::fclose(f);
  return w == sizeof(m) ? 0 : -3;
}
inline int bootmeta_load(BootMeta& m) {
  FILE* f = std::fopen(bootmeta_sim_path(), "rb");
  if(!f) { bootmeta_init(m); bootmeta_save(m); return 0; }
  size_t r = std::fread(&m, 1, sizeof(m), f);
  std::fclose(f);
  if(r != sizeof(m) || !bootmeta_valid(m)) { bootmeta_init(m); return -1; }
  return 0;
}
#else
// TODO(noyau MonOS) : fournir l'acces au "bloc de metadonnees" de l'image
// (couche disque du bootloader/kernel). Les deux fonctions doivent lire/ecrire
// un secteur logique reserve (128 Ko de premier/last secteur du disque).
int bootmeta_save(const BootMeta& m);
int bootmeta_load(BootMeta& m);
#endif

// Journal des mises a jour. Format texte append-only (levee du spec).
inline int journal_append(const char* line) {
#if !defined(MONOS_UPD_KERNEL)
  const char* p = std::getenv("MONOS_UPDATE_LOG");
  const char* path = (p && *p) ? p : "monos_updates.log";
  FILE* f = std::fopen(path, "a");
  if(!f) return -2;
  std::fprintf(f, "%s\n", line ? line : "");
  std::fclose(f);
  return 0;
#else
  // TODO(noyau MonOS) : append-line dans le journal du systeme de fichiers.
  (void)line;
  return 0;
#endif
}

} // namespace monupd
#endif