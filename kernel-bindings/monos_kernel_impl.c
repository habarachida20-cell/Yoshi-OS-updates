// ===========================================================================
// MonOS Update — implementation kernel de l'API de mise a jour.
//
// A compiler DANS le noyau MonOS (base xv6-public-xv6-rev11). Primitives
// existantes utilisees (verifiees dans le source du noyau) :
//   - ide.c / bio.c : iderw(struct buf *), bread(uint, uint), bwrite,
//                      brelse          -> acces disque brut par blocs 512 o
//   - x86.h :         outb(ushort, uchar) -> reboot 8042 (port 0x64, 0xFE)
//   - uart.c :        uartgetc(void) -> un caractere (ou -1) console serie
//
// L'API n'ecrit JAMAIS le slot actif. L'ecriture passe par le buffer cache
// du kernel (bread -> memmove -> bwrite -> brelse). Attention : les blocs
// manipules ici sont les SLOTS RESERVES de l'image (hors systeme de fichiers) ;
// ne jamais ecrire a travers cette API des blocs du systeme de fichiers.
// ===========================================================================

#include "monos_kernel_api.h"

#include "types.h"
#include "defs.h"      /* bread, bwrite, brelse, uartgetc, ... */
#include "buf.h"       /* struct buf */
#include "x86.h"       /* outb */
#include "fs.h"        /* BSIZE */

/* Disque de boot IDE (le systeme demarre de hda). */
#define MONOS_KERNEL_BOOT_DEV 1u

/*
 * Geometrie de l'image MonOS (alignee sur la spec docs/update-system.md).
 * A VALIDER lors de la prochaine fabrication d'image "mono-disque" (voit
 * docs/kernel-binding.md). Valeurs par defaut prudentes :
 *   bloc 0      : bootblock
 *   bloc 1..15  : bloc de metadonnees + journal + reserve
 *   bloc 16..   : SLOT A (4096 blocs = 2 Mo)
 *   slot B       : 4096 blocs suivant
 *   fin de disque: Recovery
 */
#define MONOS_META_BLOCK      1u
#define MONOS_JOURNAL_BLOCK   2u
#define MONOS_SLOT_NBLOCKS    4096u
#define MONOS_SLOT_A_BLOCK    16u
#define MONOS_SLOT_B_BLOCK    (MONOS_SLOT_A_BLOCK + MONOS_SLOT_NBLOCKS)

/* ---------------------------------------------------------------------------
 * Acces disque brut (par le buffer cache kernel).
 * ------------------------------------------------------------------------- */
static int block_raw_write(unsigned int block, const void* data,
                           unsigned int nbytes) {
  if(nbytes == 0u || (nbytes % BSIZE) != 0u) return MONOS_ERR_INVALID;
  const uchar* p = (const uchar*)data;
  for(unsigned int b = 0; b < nbytes / BSIZE; ++b) {
    struct buf* bp = bread(MONOS_KERNEL_BOOT_DEV, block + b);
    if(bp == 0) return MONOS_ERR_IO;
    memmove(bp->data, p + b * BSIZE, BSIZE);
    bwrite(bp);
    brelse(bp);
  }
  return MONOS_OK;
}

static int block_raw_read(unsigned int block, void* data,
                          unsigned int nbytes) {
  if(nbytes == 0u || (nbytes % BSIZE) != 0u) return MONOS_ERR_INVALID;
  uchar* p = (uchar*)data;
  for(unsigned int b = 0; b < nbytes / BSIZE; ++b) {
    struct buf* bp = bread(MONOS_KERNEL_BOOT_DEV, block + b);
    if(bp == 0) return MONOS_ERR_IO;
    memmove(p + b * BSIZE, bp->data, BSIZE);
    brelse(bp);
  }
  return MONOS_OK;
}

static int slot_base(int slot, unsigned int* base) {
  switch(slot) {
    case MONOS_SLOT_A: *base = MONOS_SLOT_A_BLOCK; return MONOS_OK;
    case MONOS_SLOT_B: *base = MONOS_SLOT_B_BLOCK; return MONOS_OK;
    default: return MONOS_ERR_NOSLOT;
  }
}

int monos_disk_write(int slot, unsigned int block, const void* data,
                     unsigned int nbytes) {
  unsigned int base;
  if(block > MONOS_SLOT_NBLOCKS) return MONOS_ERR_INVALID;
  if(slot_base(slot, &base) != MONOS_OK) return MONOS_ERR_NOSLOT;
  if(block + nbytes / BSIZE > MONOS_SLOT_NBLOCKS) return MONOS_ERR_INVALID;
  return block_raw_write(base + block, data, nbytes);
}

int monos_disk_read(int slot, unsigned int block, void* data,
                    unsigned int nbytes) {
  unsigned int base;
  if(block > MONOS_SLOT_NBLOCKS) return MONOS_ERR_INVALID;
  if(slot_base(slot, &base) != MONOS_OK) return MONOS_ERR_NOSLOT;
  if(block + nbytes / BSIZE > MONOS_SLOT_NBLOCKS) return MONOS_ERR_INVALID;
  return block_raw_read(base + block, data, nbytes);
}

int monos_bootmeta_save(const void* blob, size_t len) {
  if(blob == 0 || len == 0u || len > BSIZE) return MONOS_ERR_INVALID;
  return block_raw_write(MONOS_META_BLOCK, blob, (unsigned int)len);
}

int monos_bootmeta_load(void* blob, size_t len) {
  if(blob == 0 || len == 0u || len > BSIZE) return MONOS_ERR_INVALID;
  return block_raw_read(MONOS_META_BLOCK, blob, (unsigned int)len);
}

int monos_journal_append(const char* line) {
  /* Journal logique : ligne lineaire dans le secteur journal. Une ligne plus
   * longue qu'un bloc est tronquee (suffisant pour les traces spec). */
  uchar buf[BSIZE];
  if(line == 0) line = "";

  for(unsigned int idx = 0; idx < 16u; ++idx) {
    int rc = block_raw_read(MONOS_JOURNAL_BLOCK + idx, buf, BSIZE);
    if(rc != MONOS_OK) return rc;
    unsigned int used = 0u;
    while(used < BSIZE && buf[used] != 0) ++used;
    unsigned int room = BSIZE - used;
    if(room > 1u) {
      /* Recopie max room-1 caracteres puis '\n' puis fin. */
      unsigned int i = 0u;
      while(i + 1u < room && line[i] != '\0') { buf[used + i] = (uchar)line[i]; ++i; }
      buf[used + i] = (uchar)'\n';
      if(used + i + 1u < BSIZE) buf[used + i + 1u] = 0;
      return block_raw_write(MONOS_JOURNAL_BLOCK + idx, buf, BSIZE);
    }
  }
  return MONOS_ERR_IO; /* journal plein */
}

int monos_getch(void) {
  return uartgetc();
}

int monos_reboot(void) {
  /* Reset 8042. Sous QEMU : lancer avec -no-reboot pour voir la sortie. */
  outb(0x64, 0xFE);
  for(;;) { /* attente : si le reboot ne part pas, ne jamais continuer */ }
  return MONOS_ERR_IO;
}

int monos_net_https_get(const char* url, long* status, unsigned char** out,
                        size_t* outLen) {
  /* TODO(kernel) : pile HTTPS (TLS + TCP/IP + pilote NIC) inexistante dans le
   * noyau actuel. L'ABI est fixee : quand la pile arrivera, seule cette
   * fonction change. En attendant : refus propre (update jamais cassee). */
  (void)url;
  if(status) *status = 404L;
  if(out) *out = 0;
  if(outLen) *outLen = 0u;
  return MONOS_ERR_NONET;
}

int monos_ed25519_verify(const char* keyHex, const char* imgPath,
                         const char* sigPath) {
  /* TODO(kernel) : backend Ed25519 (courbes) inexistant dans le noyau. */
  (void)keyHex; (void)imgPath; (void)sigPath;
  return MONOS_ERR_NOKEY;
}