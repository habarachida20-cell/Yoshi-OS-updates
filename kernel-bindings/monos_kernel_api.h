#ifndef MONOS_UPD_KERNEL_API_H
#define MONOS_UPD_KERNEL_API_H

// ===========================================================================
// MonOS Update — API kernel (contrat unique entre le domaine utilisateur
// update-robot/installer/recovery et le noyau MonOS).
//
// Fichier de branchement des "hooks noyau". Le noyau doit fournir ces 9
// symboles (implementation dans kernel-bindings/monos_kernel_impl.c).
// Tant qu'un sous-systeme manque (ex. pile HTTPS, cle Ed25519), la fonction
// retourne un code d'erreur explicite : la mise a jour est REFUSEE proprement,
// jamais de boot casse. Le slot actif n'est jamais ecrit par ces fonctions.
// ===========================================================================

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Identifiants de slot (identiques a monupd::kSlotA/kSlotB). */
#define MONOS_SLOT_NONE (-1)
#define MONOS_SLOT_A    0
#define MONOS_SLOT_B    1

/* Codes de retour communs. */
enum {
  MONOS_OK          =  0,
  MONOS_ERR_IO      = -1,
  MONOS_ERR_INVALID = -2,
  MONOS_ERR_NOSLOT  = -3,
  MONOS_ERR_NONET   = -4,
  MONOS_ERR_NOKEY   = -5
};

/*
 * Disque : lecture/ecriture d'un temporaire contigu dans le slot (blocs
 * logiques de 512 o, hors systeme de fichiers). block = numero de bloc
 * absolu de l'image ; nbytes multiple de 512. L'ecriture ne touche JAMAIS
 * le slot actif (le verrou est pose ici, cote kernel).
 */
int monos_disk_write(int slot, unsigned int block, const void* data,
                     unsigned int nbytes);
int monos_disk_read(int slot, unsigned int block, void* data,
                    unsigned int nbytes);

/*
 * "Bloc de metadonnees" (BootMeta A/B). Lecture/ecriture du secteur logique
 * dedie, jamais via le systeme de fichiers.
 */
int monos_bootmeta_save(const void* blob, size_t len);
int monos_bootmeta_load(void* blob, size_t len);

/*
 * Reseau : GET HTTPS vers GitHub (manifeste + assets). out et outLen sont
 * alloues par le kernel (la couche kernel doit allouer/free). En l'absence
 * de pile TLS reseau dans MonOS : MONOS_ERR_NONET.
 */
int monos_net_https_get(const char* url, long* status, unsigned char** out,
                        size_t* outLen);

/* Clavier : un caractere, ou -1 si aucun (non bloquant, console serie). */
int monos_getch(void);

/* Redemarrage (controleur 8042). Ne retourne en principe jamais. */
int monos_reboot(void);

/*
 * Journal des mises a jour (append-line). Dans le noyau : secteur journal
 * dedie (pas le systeme de fichiers), tronque si la ligne depasse un bloc.
 */
int monos_journal_append(const char* line);

/*
 * Verification de signature Ed25519 des images. Key hex compilee cote image.
 * Pas de backend dans le kernel actuel : MONOS_ERR_NOKEY (SHA-256 reste
 * TOUJOURS obligatoire, lui, meme sans signature).
 */
int monos_ed25519_verify(const char* keyHex, const char* imgPath,
                         const char* sigPath);

#ifdef __cplusplus
}
#endif

#endif /* MONOS_UPD_KERNEL_API_H */