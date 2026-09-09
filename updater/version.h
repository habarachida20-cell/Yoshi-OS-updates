#ifndef MONOS_UPD_VERSION_H
#define MONOS_UPD_VERSION_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>

namespace monupd {

// ---- Identite du produit ----
static const char* kProduct          = "MonOS";
static const char* kRepoOwner        = "habarachida20-cell";
static const char* kRepoName         = "Yoshi-OS-updates";
static const char* kUpdateChannel    = "stable";
// Version installee localement dans l'image. Bumpee a chaque sortie stable.
static const char* kInstalledVersion = "1.0.0";

// Classe SemVer (MAJOR.MINOR.PATCH) avec comparaison numerique, jamais en texte.
class Version {
public:
  int  major = 0;
  int  minor = 0;
  int  patch = 0;
  char prerelease[64] = {};   // suffixe "-xxx" entre : le robot l'ignore (jamais stable)

  Version() {}
  Version(int ma, int mi, int pa, const char* pre = nullptr)
    : major(ma), minor(mi), patch(pa) {
    if(pre) std::strncpy(prerelease, pre, sizeof(prerelease)-1);
  }

  // Parse "v?"X.Y.Z["-pre"] en appliquant les regles strictes du semver.
  bool parse(const char* s) {
    major = minor = patch = 0;
    prerelease[0] = 0;
    if(!s || *s == 0) return false;
    const char* p = s;
    if(*p == 'v' || *p == 'V') ++p;
    if(!parseNum(&p, &major) || *p != '.') return false; ++p;
    if(!parseNum(&p, &minor) || *p != '.') return false; ++p;
    if(!parseNum(&p, &patch)) return false;
    if(*p == '-') {                       // prerelease : acceptee pour info, refusee en auto
      ++p;
      size_t i = 0;
      while(*p && *p != '+' && i < sizeof(prerelease)-1) prerelease[i++] = *p++;
      prerelease[i] = 0;
    }
    return true;
  }

  bool isStable() const { return prerelease[0] == 0; }

  void print(char* out, size_t n) const {
    if(prerelease[0])
      std::snprintf(out, n, "%d.%d.%d-%s", major, minor, patch, prerelease);
    else
      std::snprintf(out, n, "%d.%d.%d", major, minor, patch);
  }

  bool operator==(const Version& o) const {
    return major==o.major && minor==o.minor && patch==o.patch;
  }
  bool operator!=(const Version& o) const { return !(*this == o); }

  // Comparaison SemVer stricte (numerique). 1.9.0 < 1.10.0.
  int compare(const Version& o) const {
    if(major != o.major) return major < o.major ? -1 : 1;
    if(minor != o.minor) return minor < o.minor ? -1 : 1;
    if(patch != o.patch) return patch < o.patch ? -1 : 1;
    const bool aStable = (prerelease[0] == 0);
    const bool bStable = (o.prerelease[0] == 0);
    if(aStable && !bStable) return 1;   // release > pre-release
    if(!aStable && bStable) return -1;
    return 0;
  }
  bool olderThan(const Version& o) const { return compare(o) < 0; }
  bool newerThan(const Version& o) const { return compare(o) > 0; }
  bool atLeast(const Version& min) const { return compare(min) >= 0; }

private:
  static bool parseNum(const char** pp, int* out) {
    const char* p  = *pp;
    int         v  = 0;
    if(!std::isdigit((unsigned char)*p)) return false;
    if(*p == '0' && std::isdigit((unsigned char)p[1])) return false; // pas de zero en tete
    while(std::isdigit((unsigned char)*p)) { v = v*10 + (*p - '0'); ++p; }
    *pp = p;
    *out = v;
    return true;
  }
};

// Heuristique anti-instabilite sur tag + notes de release.
// Une version contenant un de ces mots est TOUJOURS refusee par le robot.
// IMPORTANT : le tag lui-meme est deja parse en SemVer ; un suffixe "-xxx"
// rend la version instable (cf. Version::isStable). Cette heuristique est une
// ceinture supplementaire pour les noms/types non-semver (ex: "v1.3.0-beta").
inline bool tagLooksUnstable(const char* text) {
  static const char* badWords[] = {
    "alpha", "beta", "channel-testing", "unstable",
    "development", "snapshot", "nightly", "canary",
    "preview", "pre-release", "-rc", ".rc"
  };
  if(!text) return true;
  char low[256];
  size_t n = std::strlen(text);
  if(n > sizeof(low)-1) n = sizeof(low)-1;
  for(size_t i = 0; i < n; ++i) low[i] = (char)std::tolower((unsigned char)text[i]);
  low[n] = 0;
  for(const char* w : badWords) {
    if(!*w) continue;
    if(std::strstr(low, w)) return true;
  }
  return false;
}

} // namespace monupd
#endif