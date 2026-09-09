// ===========================================================================
// MonOS Installer — installation securisee d'une image dans le slot INACTIF.
//
// Principes :
//   - le slot actif n'est JAMAIS touche ;
//   - l'image est ecrite dans le slot inactif, puis RECLUE et verifiee par
//     SHA-256 avant de marquer le slot de demarrage suivant ;
//   - si l'installation est interrompue, le slot inactif est "incomplet" :
//     aucune demarrage sur lui (le bootloader/rollback choisit l'autre) ;
//   - un compteur de tentatives (bootAttempts) est pose au commit.
//
// Compilation hote (tests PC, pour la couche simu) :
//   g++ installer.cpp -DMONOS_UPD_HOST -o installer.exe
// ===========================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>

#include "bootmeta.h"
#include "../updater/version.h"
#include "../updater/sha256.h"

namespace monupd {

// ---------------------------------------------------------------------------
// Hote / simulation : le slot est un fichier ("" = retour simples). 
// Noyau : TODO(noyau MonOS) — les deux fonctions ci-dessous sont remplacees
// par la couche disque du kernel (lecture/ecriture de blocs logiques).
// ---------------------------------------------------------------------------
#if !defined(MONOS_UPD_KERNEL)
inline std::string slotPath(int slot) {
  return std::string("monos_slot_") + char(slot == kSlotA ? 'A' : 'B') + ".img";
}
static int slot_write_block(const std::string& path, long long offset, const void* buf, size_t len) {
  FILE* f = std::fopen(path.c_str(), "r+b");
  if(!f) f = std::fopen(path.c_str(), "w+b");
  if(!f) return -1;
  std::fseek(f, (long)offset, SEEK_SET);
  size_t w = std::fwrite(buf, 1, len, f);
  std::fclose(f);
  return (w == len) ? 0 : -2;
}
static int slot_read_block(const std::string& path, long long offset, void* buf, size_t len) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if(!f) return -1;
  std::fseek(f, (long)offset, SEEK_SET);
  size_t r = std::fread(buf, 1, len, f);
  std::fclose(f);
  return (r == len) ? 0 : -2;
}
#else
// TODO(noyau MonOS) : API disque du kernel :
//   int monos_disk_write(int slot, uint32_t block, const void* data, size_t nblocks);
//   int monos_disk_read (int slot, uint32_t block, void* data, size_t nblocks);
#endif

// ---------------------------------------------------------------------------
// Ecrit une image dans un slot par blocs 128 Ko puis relit pour verification.
// ---------------------------------------------------------------------------
static int install_image_to_slot(const std::string& srcImage, const std::string& sha256Expected,
                                 int targetSlot, const char* imageName) {
  std::string slot = slotPath(targetSlot);
  FILE* in = std::fopen(srcImage.c_str(), "rb");
  if(!in) { std::printf("Install: source introuvable (%s)\n", srcImage.c_str()); return -1; }

  static const size_t kBlock = 128u * 1024u;
  unsigned char* buf = new unsigned char[kBlock];
  long long offset = 0;
  size_t readable = 0;

  std::printf("Install: écriture dans le slot %c (%s)...\n",
              targetSlot == kSlotA ? 'A' : 'B', slot.c_str());
  while(!std::feof(in)) {
    readable = std::fread(buf, 1, kBlock, in);
    if(readable == 0) break;
    if(slot_write_block(slot, offset, buf, readable) != 0) {
      std::printf("Install: échec d'écriture (slot incomplet, jamais démarré)\n");
      delete[] buf; std::fclose(in); return -2;
    }
    offset += (long long)readable;
  }
  std::fclose(in);

  // Relire l'intégralité et recalculer le SHA-256.
  std::printf("Install: relecture + SHA-256...\n");
  unsigned char fileDigest[32];
  if(sha256_file(slot.c_str(), fileDigest) != 0) {
    delete[] buf; return -3;
  }
  char hex[65]; sha256_hex(fileDigest, hex);
  if(std::strcmp(hex, sha256Expected.c_str()) != 0) {
    std::printf("Install: la relecture du slot ne correspond pas au manifeste\n");
    std::printf("  attendu : %s\n  relu    : %s\n", sha256Expected.c_str(), hex);
    delete[] buf; return -4;
  }

  std::printf("Install: OK, %lld octets écrits et vérifiés\n", offset);
  delete[] buf;
  return 0;
}

// ---------------------------------------------------------------------------
// Point d'entrée de l'installeur (appelé par le robot et par Recovery).
// ---------------------------------------------------------------------------
int installer_run(const char* srcImage, const char* sha256Expected,
                  const char* imageName) {
  BootMeta meta;
  bootmeta_load(meta);
  if(!bootmeta_valid(meta)) {
    std::printf("Install: métadonnées de démarrage invalides\n");
    return -1;
  }

  int target = (meta.activeSlot == kSlotA) ? kSlotB : kSlotA;

  // 1) SHA-256 déjà vérifié en amont par le robot ; on le revérifie ici aussi.
  unsigned char d[32]; char hex[65];
  if(sha256_file(srcImage, d) != 0 || (sha256_hex(d, hex),
     std::strcmp(hex, sha256Expected) != 0)) {
    std::printf("Install: SHA-256 de la source incorrect\n");
    return -2;
  }

  // 2) Écriture slot inactif + relecture.
  int rc = install_image_to_slot(srcImage, sha256Expected, target, imageName);
  if(rc != 0) return rc;

  // 3) Commit : slot de démarrage suivant + compteur de tentatives.
  meta.nextSlot       = target;
  meta.bootAttempts   = kMaxBootAttempts;
  meta.bootSerial    += 1;
  meta.updateId       = meta.bootSerial;
  std::snprintf(meta.previousVersion, sizeof(meta.previousVersion), "%s", meta.pendingVersion);
  std::snprintf(meta.pendingVersion,  sizeof(meta.pendingVersion),  "%s", imageName);
  if(bootmeta_save(meta) != 0) {
    std::printf("Install: impossible d'enregistrer les métadonnées\n");
    return -3;
  }

  journal_append("Install: OK");
  std::printf("Install: commit OK — prochain boot sur le slot %c (attempts=%d)\n",
              target == kSlotA ? 'A' : 'B', kMaxBootAttempts);
  // TODO(noyau MonOS) : monos_reboot() pour lancer le nouveau slot.
  return 0;
}

} // namespace monupd

#ifndef MONOS_UPD_INSTALLER_LINK
int main(int argc, char** argv) {
  if(argc < 3) {
    std::printf("Usage : installer <image.img> <sha256hex> [imageName]\n");
    return 64;
  }
  return monupd::installer_run(argv[1], argv[2], argc > 3 ? argv[3] : argv[1]);
}
#endif