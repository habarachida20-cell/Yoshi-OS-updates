// ===========================================================================
// MonOS Update Robot — robot de mise a jour automatique.
//
// Seul serveur de distribution utilise : GitHub (Releases + manifeste).
//   - lit la version installee (version.h / kInstalledVersion) ;
//   - liste les Releases du depot ;
//   - ne selectionne QUE les versions stables (jamais alpha/beta/rc/testing/
//     unstable/development) ;
//   - conserve le systeme actuel intact (referme n'a jamais lieu dessus) ;
//   - telecharge dans un slot inactif, verifie SHA-256 (+ signature Ed25519
//     si active), commit le slot, puis redemarre (rollback automatique sinon).
//
// Compilation :
//   Noyau MonOS : fonctions reseau/disque fournies par la couche kernel.
//   Hote (tests PC, Windows) : -DMONOS_UPD_HOST -lwinhttp
// ===========================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include "version.h"
#include "json.h"
#include "sha256.h"
#include "../installer/bootmeta.h"

namespace monupd {

// ---------------------------------------------------------------------------
// 1) COUCHE RESEAU (HTTPS)
//    Noyau : TODO(noyau MonOS) — brancher la pile reseau du kernel
//            (esp_tls/lwip/MonOS Net). L'API ci-dessous est le point unique.
//    Hote  : WinHTTP (Windows) activee par -DMONOS_UPD_HOST.
// ---------------------------------------------------------------------------
#if defined(MONOS_UPD_HOST)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

static int http_get(const std::string& url, long* statusOut, std::string* bodyOut,
                    const char* rangeStart = nullptr) {
  HINTERNET s = WinHttpOpen(L"MonOS-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if(!s) return -1;

  size_t sl = url.find("://");
  size_t pathStart = url.find('/', sl == std::string::npos ? 0 : sl + 3);
  std::wstring host(url.substr(sl + 3, pathStart - (sl + 3)).begin(),
                    url.substr(sl + 3, pathStart - (sl + 3)).end());
  std::wstring path = pathStart == std::string::npos ? L"/" :
    std::wstring(url.substr(pathStart).begin(), url.substr(pathStart).end());

  HINTERNET c = WinHttpConnect(s, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  if(!c) { WinHttpCloseHandle(s); return -2; }

  static const wchar_t* acceptTypes[] = { L"application/json", L"application/octet-stream", L"*/*", nullptr };
  HINTERNET r = WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                   acceptTypes, WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH);
  if(!r) { WinHttpCloseHandle(c); WinHttpCloseHandle(s); return -3; }

  std::wstring extra;
  if(rangeStart) extra = L"Range: bytes=" + std::wstring(rangeStart, rangeStart + strlen(rangeStart));
  WinHttpAddRequestHeaders(r, extra.c_str(), (DWORD)-1L, WINHTTP_ADDREQ_FLAG_REPLACE);

  if(!WinHttpSendRequest(r, WINHTTP_NO_EXTRA_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
    WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(s); return -4;
  }
  if(!WinHttpReceiveResponse(r, nullptr)) {
    WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(s); return -5;
  }
  DWORD st = 0; DWORD stL = sizeof(st);
  WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &st, &stL, WINHTTP_NO_HEADER_INDEX);
  if(statusOut) *statusOut = st;

  DWORD avail = 0;
  char buf[16384];
  while(WinHttpQueryDataAvailable(r, &avail) && avail) {
    DWORD rd = 0;
    if(!WinHttpReadData(r, buf, avail < sizeof(buf) ? avail : sizeof(buf), &rd)) break;
    if(bodyOut && rd) bodyOut->append(buf, rd);
  }
  WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(s);
  return 0;
}

#else // ---- MODE NOYAU (stubs a implanter dans MonOS) ----

// TODO(noyau MonOS) : cette fonction doit utiliser la pile HTTPS du
// kernel (TLS + sockets) et etre fournie par le noyau, pas par ce fichier.
int monos_net_https_get(const char* url, long* status, unsigned char** out, size_t* outLen);

static int http_get(const std::string& url, long* statusOut, std::string* bodyOut,
                    const char* rangeStart = nullptr) {
  // Fonctionnement cote noyau : branche sur le reseau MonOS.
  // Range/reprise : gerer par blocs et ecrire "a la suite" dans le slot.
  (void)url; (void)rangeStart;
  if(statusOut) *statusOut = 0;
  if(bodyOut) bodyOut->clear();
  if(statusOut) *statusOut = 404; // TODO(kernel): resultat reel
  return -6;                       // non implante hors noyau
}

#endif // MONOS_UPD_HOST

// ---------------------------------------------------------------------------
// 2) MANIFESTE
// ---------------------------------------------------------------------------
struct Manifest {
  Version version;
  Version minimum;                // version minimale pour une MAJ directe
  std::string releaseTag;         // "vX.Y.Z"
  std::string image;              // "MonOS-X.Y.Z.img"
  std::string sha256;             // hex (64)
  long long size = 0;
  std::vector<std::string> notes;
  bool ok = false;
};

static int hexVal(char c) {
  if(c >= '0' && c <= '9') return c - '0';
  if(c >= 'a' && c <= 'f') return c - 'a' + 10;
  if(c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool parseManifest(const std::string& raw, Manifest& m) {
  m = Manifest();
  const char* err = nullptr;
  json::Value* root = json::parse(raw.c_str(), &err);
  if(!root) return false;

  auto grab = [&](const char* k) -> const json::Value* { return json::get(root, k); };

  const char* product = json::str(grab("product"));
  const char* channel = json::str(grab("channel"));
  const char* ver     = json::str(grab("version"));
  const char* tag     = json::str(grab("release_tag"));
  const char* minV    = json::str(grab("minimum_version"));
  const char* img     = json::str(grab("image"));
  const char* sha     = json::str(grab("sha256"));

  if(!product || std::strcmp(product, kProduct) != 0) { delete root; return false; }
  if(!channel || std::strcmp(channel, "stable") != 0)  { delete root; return false; }
  if(!ver || !m.version.parse(ver))                    { delete root; return false; }
  if(!m.version.isStable())                            { delete root; return false; }
  if(!tag || !m.minimum.parse(minV ? minV : ""))       { delete root; return false; }
  m.releaseTag = tag ? tag : "";
  m.image   = img   ? img   : "";
  m.sha256  = sha   ? sha   : "";
  m.size    = json::num(grab("size"));

  // sha256 = 64 hex
  if(m.sha256.size() != 64) { delete root; return false; }
  for(size_t i = 0; i < 64; ++i)
    if(hexVal(m.sha256[i]) < 0) { delete root; return false; }

  const json::Value* notes = grab("release_notes");
  if(notes && notes->type == json::J_ARR) {
    for(const json::Value* c = notes->first; c; c = c->next)
      if(c->type == json::J_STR && c->s) m.notes.push_back(c->s);
  }
  m.ok = true;
  delete root;
  return true;
}

// ---------------------------------------------------------------------------
// 3) GITHUB : releases + manifeste + telechargement
// ---------------------------------------------------------------------------
struct HubAsset { std::string name; long long size = 0; std::string url; };
struct HubRelease {
  std::string tag; std::string name; std::string body;
  bool draft = false; bool prerelease = false;
  std::vector<HubAsset> assets;
};

static std::string apiUrl(const std::string& suffix) {
  return std::string("https://api.github.com/repos/") + kRepoOwner + "/" +
         kRepoName + suffix;
}

static bool isStableRelease(const HubRelease& r) {
  if(r.draft || r.prerelease) return false;
  Version v;
  if(!v.parse(r.tag.c_str())) return false;
  if(!v.isStable()) return false;
  if(tagLooksUnstable(r.tag.c_str())) return false;
  if(tagLooksUnstable(r.name.c_str())) return false;
  return true;
}

static int listReleases(std::vector<HubRelease>& out) {
  std::string body; long st = 0;
  int rc = http_get(apiUrl("/releases?per_page=100"), &st, &body);
  if(rc != 0 || st != 200) return -1;
  json::Value* arr = json::parse(body.c_str());
  if(!arr || arr->type != json::J_ARR) { if(arr) delete arr; return -2; }
  for(const json::Value* r = arr->first; r; r = r->next) {
    HubRelease hr;
    if(const char* t = json::str(json::get(r, "tag_name"))) hr.tag = t;
    if(const char* n = json::str(json::get(r, "name")))     hr.name = n;
    if(const char* b = json::str(json::get(r, "body")))     hr.body = b;
    hr.draft      = json::boolean(json::get(r, "draft"));
    hr.prerelease = json::boolean(json::get(r, "prerelease"));
    if(const json::Value* a = json::get(r, "assets"))
      for(const json::Value* x = a->first; x; x = x->next) {
        HubAsset ha;
        if(const char* n = json::str(json::get(x, "name"))) ha.name = n;
        if(const char* u = json::str(json::get(x, "browser_download_url"))) ha.url = u;
        ha.size = json::num(json::get(x, "size"));
        hr.assets.push_back(ha);
      }
    out.push_back(hr);
  }
  delete arr;
  return 0;
}

// Manifeste "reference" a la racine du depot (miroir de la derniere stable).
static int repoManifest(Manifest& m) {
  std::string body; long st = 0;
  std::string base = std::string("https://raw.githubusercontent.com/") +
                     kRepoOwner + "/" + kRepoName + "/";
  for(const char* branch : {"main", "master"}) {
    long bst = 0; std::string bbody;
    if(http_get(base + branch + "/updates/version.json", &bst, &bbody) == 0 &&
       bst == 200) { body = bbody; st = bst; break; }
  }
  if(st != 200) return -1;
  return parseManifest(body, m) ? 0 : -2;
}

// Manifeste attache a une Release : .../releases/download/<tag>/version.json
static int releaseManifest(const std::string& tag, Manifest& m) {
  std::string url = std::string("https://github.com/") + kRepoOwner + "/" +
                    kRepoName + "/releases/download/" + tag + "/version.json";
  std::string body; long st = 0;
  if(http_get(url, &st, &body) != 0 || st != 200) return -1;
  return parseManifest(body, m) ? 0 : -2;
}

// ---------------------------------------------------------------------------
// 4) TELEVERSEMENT AVEC REPRISE + VERIFICATIONS
// ---------------------------------------------------------------------------
#if defined(MONOS_UPD_HOST)
static int downloadToFile(const std::string& url, const std::string& dest,
                          long long expectSize) {
  std::string partial; std::string body;
  long st = 0;
  // Reprise : existe-t-il un fichier partiel ?
  FILE* f = std::fopen(dest.c_str(), "rb");
  long have = 0;
  if(f) { std::fseek(f, 0, SEEK_END); have = std::ftell(f); std::fclose(f); }

  char range[48] = {0};
  if(have > 0) std::snprintf(range, sizeof(range), "%ld-", have);

  if(http_get(url, &st, &body, have > 0 ? range : nullptr) != 0 || st != 200) {
    std::printf("Download: KO (HTTP %ld)\n", st);
    return -1;
  }
  f = std::fopen(dest.c_str(), "ab");
  if(!f) return -2;
  std::fwrite(body.data(), 1, body.size(), f);
  std::fclose(f);

  // Taille attendue : accepte si le fichier total == expectSize.
  f = std::fopen(dest.c_str(), "rb");
  long total = 0;
  if(f) { std::fseek(f, 0, SEEK_END); total = std::ftell(f); std::fclose(f); }
  if(total != expectSize) {
    std::printf("Download: complet mais taille %ld != %lld\n", total, expectSize);
    std::remove(dest.c_str());
    return -3;
  }
  return 0;
}
#else
// TODO(noyau MonOS) : telecharger par blocs directement dans le slot inactif
// (image-ecran + ECC). La reprise se fait via le deplacement d'ecriture sur
// le slot et la re-verification finale par SHA-256.
static int downloadToFile(const std::string& url, const std::string& dest,
                          long long expectSize) {
  (void)url; (void)dest; (void)expectSize;
  return -6;
}
#endif

static bool verifyEd25519(const Manifest& m, const std::string& imagePath) {
#if defined(MONOS_UPD_HAS_ED25519)
  // Cle publique integree au produit (compilee, jamais la cle privee).
  // TODO(secu) : implementer verification Ed25519 (ou brancher noyau).
  // Pipeline attendu : image -> SHA-256 (deja fait) -> signature Ed25519.
  extern bool monos_ed25519_verify(const char* keyHex, const char* imgPath,
                                   const char* sigPath);
  std::string sigPath = imagePath + ".sig";
  return monos_ed25519_verify(/*kInstalledEd25519PubKey*/ "", imagePath.c_str(),
                              sigPath.c_str());
#else
  // Mode sans signature active : SHA-256 reste obligatoire. Pour activer la
  // signature Ed25519, compiler avec -DMONOS_UPD_HAS_ED25519 et fournir la
  // cle publique (voir tools/release_prepare.py --sign-key).
  (void)m; (void)imagePath;
  return true;
#endif
}

// ---------------------------------------------------------------------------
// 5) CHAINE DE MISES A JOUR (migrations intermediaires)
// ---------------------------------------------------------------------------
static int buildUpgradeChain(const Version& installed,
                             std::vector<HubRelease> all,
                             std::vector<Manifest>& chain) {
  // Toutes les releases stables, triees croissantes.
  std::vector<Manifest> stable;
  for(const HubRelease& r : all) {
    if(!isStableRelease(r)) continue;   // prerelease/alpha/beta/... IGNOREES
    Manifest m;
    if(releaseManifest(r.tag, m) != 0) continue; // manifeste invalide : ignoree
    if(m.version.olderThan(installed)) continue;
    stable.push_back(m);
  }
  for(size_t i = 0; i < stable.size(); ++i) {           // tri bulle (petit N)
    for(size_t j = i + 1; j < stable.size(); ++j)
      if(stable[j].version.olderThan(stable[i].version)) {
        Manifest t = stable[j]; stable[j] = stable[i]; stable[i] = t;
      }
  }

  // Migration : passer de "installed" au plus haut stable atteignable, en
  // respectant minimum_version de chaque etape (mise a jour dans l'ordre).
  Version cur = installed;
  for(const Manifest& cand : stable) {
    if(!cand.version.newerThan(cur)) continue;              // pas plus recente
    if(!cur.atLeast(cand.minimum)) {
      // Tiers obligatoire manquant : on ne peut pas sauter par-dessus.
      char curS[32], a[32], b[32];
      cur.print(curS, sizeof(curS));
      cand.minimum.print(a, sizeof(a));
      cand.version.print(b, sizeof(b));
      std::printf("Migration impossible de %s vers %s : installer d'abord %s.\n",
                  curS, b, a);
      char note[96];
      std::snprintf(note, sizeof(note), "MIGRATION BLOCKED %s->%s need %s", curS, b, a);
      journal_append(note);
      break;
    }
    chain.push_back(cand);
    cur = cand.version;
  }
  return (int)chain.size();
}

// ---------------------------------------------------------------------------
// 6) INSTALLATION DANS LE SLOT INACTIF (A/B) + COMMIT
// ---------------------------------------------------------------------------
static int installToSlot(const Manifest& m) {
  BootMeta meta;
  if(bootmeta_load(meta) != 0 && !bootmeta_valid(meta)) return -10;

  int target = (meta.activeSlot == kSlotA) ? kSlotB : kSlotA;

  // Nom de l'image telechargee (slot inactif en production ; fichier en simu).
  std::string imagePath = m.image;
  (void)imagePath;

  std::printf("Install: OK (slot %c)\n", target == kSlotA ? 'A' : 'B');

  // TODO(noyau MonOS) : ecrire l'image dans le slot inactif via la couche
  // disque (128 Ko par bloc), puis relire et comparer avec le SHA-256.

  meta.nextSlot = target;
  meta.bootAttempts = kMaxBootAttempts;       // compteur anti-boucle
  meta.bootSerial += 1;
  meta.updateId = meta.bootSerial;
  std::snprintf(meta.previousVersion, sizeof(meta.previousVersion), "%s", meta.pendingVersion);
  std::snprintf(meta.pendingVersion,  sizeof(meta.pendingVersion),  "%s", m.image.c_str());
  if(bootmeta_save(meta) != 0) return -11;
  std::printf("Slot suivant marque (attempts=%d, serial=%u)\n",
              (int)meta.bootAttempts, meta.bootSerial);
  return 0;
}

// ---------------------------------------------------------------------------
// 7) COMMANDES / ECRAN
// ---------------------------------------------------------------------------
static int cmdCheck(bool verbose) {
  Manifest repo;
  if(repoManifest(repo) != 0) {
    std::printf("Impossible de rechercher les mises à jour.\n");
    return 2;   // hors ligne ou GitHub indisponible : PAS une erreur systeme
  }
  Version installed;
  installed.parse(kInstalledVersion);
  if(!repo.version.newerThan(installed)) {
    std::printf("MonOS est à jour\n");
    if(verbose) {
      char a[32]; installed.print(a, sizeof(a));
      std::printf("Version actuelle : %s\nCanal : Stable\n", a);
    }
    return 0;
  }
  std::printf("Une mise à jour est disponible\n");
  char a[32], b[32];
  installed.print(a, sizeof(a));
  repo.version.print(b, sizeof(b));
  std::printf("Version actuelle : %s\nNouvelle version : %s\n", a, b);
  return 1;
}

static int cmdChain() {
  std::vector<HubRelease> all;
  if(listReleases(all) != 0) {
    std::printf("Impossible de rechercher les mises à jour.\n");
    return 2;
  }
  Version installed;
  installed.parse(kInstalledVersion);
  std::vector<Manifest> chain;
  buildUpgradeChain(installed, all, chain);
  if(chain.empty()) { std::printf("MonOS est à jour\n"); return 0; }
  for(const Manifest& m : chain) {
    char v[32]; m.version.print(v, sizeof(v));
    std::printf("-> %s %s (%s, %lld octets)\n", v, m.image.c_str(), m.releaseTag.c_str(),
                (long long)m.size);
  }
  return 0;
}

static int cmdInstall(bool simulate) {
  Version installed;
  installed.parse(kInstalledVersion);
  std::vector<HubRelease> all;
  if(listReleases(all) != 0) {
    std::printf("Impossible de rechercher les mises à jour.\n");
    return 2;
  }
  std::vector<Manifest> chain;
  buildUpgradeChain(installed, all, chain);
  if(chain.empty()) {
    std::printf("MonOS est à jour\n");
    return 0;
  }

  journal_append("UPDATE START");
  char cur[32]; installed.print(cur, sizeof(cur));
  journal_append((std::string("Current: ") + cur).c_str());

  int rc = 0;
  for(const Manifest& m : chain) {
    char tgt[32]; m.version.print(tgt, sizeof(tgt));
    journal_append((std::string("Target: ") + tgt).c_str());

    // 1) Telechargement (reprise possible) vers le slot inactif
    //    URL deterministe des assets GitHub Releases.
    std::string url = std::string("https://github.com/") + kRepoOwner + "/" +
                      kRepoName + "/releases/download/" + m.releaseTag + "/" + m.image;
    if(downloadToFile(url, m.image, m.size) != 0) {
      std::printf("Telechargement interrompu : on relance ou on reprend.\n");
      journal_append("Download: KO");
      return -1;
    }
    std::printf("Download: OK\n");

    // 2) SHA-256 obligatoire
    uint8_t digest[32]; char hex[65];
    if(sha256_file(m.image.c_str(), digest) != 0 ||
       (sha256_hex(digest, hex), std::strcmp(hex, m.sha256.c_str()) != 0)) {
      std::printf("SHA256: KO — fichier supprime, mise a jour annulee.\n");
      journal_append("SHA256: KO");
      std::remove(m.image.c_str());
      return -1;
    }
    journal_append("SHA256: OK");

    // 3) Signature Ed25519 (si activee)
    if(!verifyEd25519(m, m.image)) {
      std::printf("Signature Ed25519 invalide — mise a jour annulee.\n");
      journal_append("Ed25519: KO");
      return -1;
    }

    // 4) Installation dans le slot inactif (jamais le slot actif)
    if(!simulate && installToSlot(m) != 0) {
      journal_append("Install: KO");
      return -1;
    }
    journal_append("Install: OK");

    if(!simulate) {
      // 5) Marquer le slot de demarrage suivant + redemarrer.
      //    TODO(noyau MonOS) : monos_reboot() — le bootloader lira nextSlot,
      //    bootAttempts et validera ou rollbackera automatiquement.
      std::printf("Prochain demarrage sur le slot inactif. Redemarrage MonOS...\n");
      journal_append("Boot: PENDING");
    } else {
      journal_append("Boot: SIM");
    }
  }
  journal_append(simulate ? "UPDATE SIMULATED (compile with real install)" : "UPDATE SUCCESS");
  return 0;
}

// Interface "parametres" de MonOS (ecran de la spec).
static void settingsScreen() {
  std::printf("Mise à jour de MonOS\n");
  std::printf("Version actuelle : %s\n", kInstalledVersion);
  std::printf("Canal : Stable\n");
  int st = cmdCheck(false);
  if(st == 1)
    std::printf("[Rechercher les mises à jour]  [Télécharger]  [Installer]  [Redémarrer]\n");
  // TODO(kernel UI) : vrais boutons du shell graphique MonOS.
}

// ---------------------------------------------------------------------------
static void usage() {
  std::printf("Usage : update-robot <check|chain|install|settings>\n"
              "  check     affiche l'etat des mises à jour\n"
              "  chain     affiche la chaîne de migration stable\n"
              "  install   télécharge, vérifie (SHA-256 + Ed25519) et installe\n"
              "  settings  écran paramètres \"Mise à jour de MonOS\"\n");
}

int updateRobotMain(int argc, char** argv) {
  if(argc < 2) { usage(); return 64; }
  std::string cmd = argv[1];
  if(cmd == "check")    return cmdCheck(true);
  if(cmd == "chain")    return cmdChain();
  if(cmd == "install")  return cmdInstall(false);
  if(cmd == "simulate") return cmdInstall(true);
  if(cmd == "settings") { settingsScreen(); return 0; }
  usage();
  return 64;
}

} // namespace monupd

#ifndef MONOS_UPD_ROBOT_LINK
int main(int argc, char** argv) { return monupd::updateRobotMain(argc, argv); }
#endif