// ===========================================================================
// MonOS Installer — installation securisee d'une image dans le slot INACTIF.
// Projet Yoshi OS abandonne : toute installation est definitivement bloquee.
// ===========================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>

#include "bootmeta.h"
#include "../updater/version.h"
#include "../updater/sha256.h"
#include "../updater/emergency-lock.h"

namespace monupd {

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
#else
#include "monos_kernel_api.h"
static int slotFromPath(const std::string& path) {
  return (path.empty() || path[path.size() - 5] == 'A') ? kSlotA : kSlotB;
}
static int slot_write_block(const std::string& path, long long offset, const void* buf, size_t len) {
  int slot = slotFromPath(path);
  if(len == 0 || (len % 512) != 0) return -2;
  return monos_disk_write(slot, (unsigned int)(offset / 512), buf, (unsigned int)len);
}
#endif

static int install_image_to_slot(const std::string& srcImage, const std::string& sha256Expected,
                                 int targetSlot, const char* imageName) {
  std::string slot = slotPath(targetSlot);
  FILE* in = std::fopen(srcImage.c_str(), "rb");
  if(!in) { std::printf("Install: source introuvable (%s)\n", srcImage.c_str()); return -1; }

  static const size_t kBlock = 128u * 1024u;
  unsigned char* buf = new unsigned char[kBlock];
  long long offset = 0;
  while(!std::feof(in)) {
    size_t readable = std::fread(buf, 1, kBlock, in);
    if(readable == 0) break;
    if(slot_write_block(slot, offset, buf, readable) != 0) {
      std::printf("Install: echec d'ecriture\n");
      delete[] buf; std::fclose(in); return -2;
    }
    offset += (long long)readable;
  }
  std::fclose(in);

  unsigned char fileDigest[32];
  if(sha256_file(slot.c_str(), fileDigest) != 0) { delete[] buf; return -3; }
  char hex[65]; sha256_hex(fileDigest, hex);
  if(std::strcmp(hex, sha256Expected.c_str()) != 0) {
    std::printf("Install: SHA-256 du slot incorrect\n");
    delete[] buf; return -4;
  }
  delete[] buf;
  (void)imageName;
  return 0;
}

int installer_run(const char* srcImage, const char* sha256Expected,
                  const char* imageName) {
  // BLOCAGE DUR : teste avant toute lecture/ecriture d'image ou de bootmeta.
  if(yoshiEmergencyLockActive()) {
    std::printf("%s\n", yoshiEmergencyMessage());
    journal_append("EMERGENCY LOCK: installation refusee");
    return 125;
  }

  BootMeta meta;
  bootmeta_load(meta);
  if(!bootmeta_valid(meta)) {
    std::printf("Install: metadonnees de demarrage invalides\n");
    return -1;
  }

  int target = (meta.activeSlot == kSlotA) ? kSlotB : kSlotA;

  unsigned char d[32]; char hex[65];
  if(sha256_file(srcImage, d) != 0 || (sha256_hex(d, hex),
     std::strcmp(hex, sha256Expected) != 0)) {
    std::printf("Install: SHA-256 de la source incorrecte\n");
    return -2;
  }

  int rc = install_image_to_slot(srcImage, sha256Expected, target, imageName);
  if(rc != 0) return rc;

  meta.nextSlot = target;
  meta.bootAttempts = kMaxBootAttempts;
  meta.bootSerial += 1;
  meta.updateId = meta.bootSerial;
  std::snprintf(meta.previousVersion, sizeof(meta.previousVersion), "%s", meta.pendingVersion);
  std::snprintf(meta.pendingVersion, sizeof(meta.pendingVersion), "%s", imageName);
  if(bootmeta_save(meta) != 0) {
    std::printf("Install: impossible d'enregistrer les metadonnees\n");
    return -3;
  }

  journal_append("Install: OK");
  std::printf("Install: commit OK — prochain boot sur le slot %c\n",
              target == kSlotA ? 'A' : 'B');
#if defined(MONOS_UPD_KERNEL)
  monos_reboot();
#endif
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
