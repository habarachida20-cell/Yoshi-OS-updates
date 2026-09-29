#ifndef YOSHI_OS_ANTI_FAKE_UPDATE_H
#define YOSHI_OS_ANTI_FAKE_UPDATE_H

#include <string>

namespace monupd {

enum class UpdateTrustResult {
  Trusted,
  Unsigned,
  InvalidSignature,
  UnknownCreator
};

struct AntiFakeDecision {
  bool accepted;
  bool quarantine;
  bool emergencyMode;
  UpdateTrustResult trust;
  std::string message;
};

inline AntiFakeDecision inspectUpdateSignature(
    bool signaturePresent,
    bool signatureValid,
    bool creatorTrusted) {

  if (!signaturePresent) {
    return {false, true, true, UpdateTrustResult::Unsigned,
            "URGENT : mise a jour non signee. Fichier place en quarantaine."};
  }

  if (!signatureValid) {
    return {false, true, true, UpdateTrustResult::InvalidSignature,
            "URGENT : signature de mise a jour invalide. Fichier place en quarantaine."};
  }

  if (!creatorTrusted) {
    return {false, true, true, UpdateTrustResult::UnknownCreator,
            "URGENT : createur non reconnu. Mise a jour placee en quarantaine."};
  }

  return {true, false, false, UpdateTrustResult::Trusted,
          "Mise a jour signee par un createur reconnu."};
}

} // namespace monupd

#endif
