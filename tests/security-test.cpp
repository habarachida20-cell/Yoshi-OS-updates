// Yoshi OS — Security Test (simulation only)
// Ce programme ne tente pas de contourner GitHub et ne fournit aucun accès pirate.
// Il simule des demandes non autorisées et vérifie que le serveur/client les refuse.

#include <cstdio>
#include <string>

namespace yoshi_security_test {

struct TestResult {
  const char* name;
  bool allowed;
};

static bool serverAllowsWrite(bool authenticatedAdmin) {
  return authenticatedAdmin;
}

static bool serverAllowsUpdate(bool abandoned, bool updatesEnabled) {
  if (abandoned) return false;
  if (!updatesEnabled) return false;
  return true;
}

static bool clientCanModifyServer() {
  // Le client Yoshi OS est volontairement en lecture seule.
  return false;
}

int run() {
  const bool abandoned = true;
  const bool updatesEnabled = false;

  TestResult tests[] = {
      {"Activer les mises a jour sans autorisation", serverAllowsUpdate(abandoned, true)},
      {"Modifier version.json depuis le client", clientCanModifyServer()},
      {"Ecrire sur le serveur sans compte administrateur", serverAllowsWrite(false)},
      {"Installer une mise a jour abandonnee", serverAllowsUpdate(abandoned, updatesEnabled)},
  };

  std::printf("Yoshi OS — SECURITY TEST\n");
  std::printf("Mode: simulation, aucune tentative d'intrusion reelle.\n\n");

  bool allRefused = true;
  for (const auto& test : tests) {
    const bool refused = !test.allowed;
    std::printf("[%s] %s\n", refused ? "REFUSE" : "AUTORISE", test.name);
    if (!refused) allRefused = false;
  }

  std::printf("\nResultat: %s\n", allRefused ? "SECURITE OK — toutes les tentatives sont refusees" : "ALERTE — une tentative a ete autorisee");
  return allRefused ? 0 : 1;
}

} // namespace yoshi_security_test

int main() {
  return yoshi_security_test::run();
}
