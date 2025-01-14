// SPDX-License-Identifier: Apache-2.0
#include "signature/signature.h"
#include "common/enum_errcode.h"
#include "common/errcode.h"
#include "common/errinfo.h"
#include "spdlog/spdlog.h"
#include <cstddef>
#include <filesystem>
#include <iostream>

namespace WasmEdge {
namespace Signature {

Expect<void> Signature::signWasmFile(const fs::path &Path,
                                     const fs::path &PrikeyPath,
                                     const fs::path &PubkeyPath,
                                     const fs::path &Target) {
  Span<Byte> Code;
  FileMgr FMgr;
  if (std::ifstream Is{Path, std::ios::binary | std::ios::ate}) {
    auto SizeToRead = Is.tellg();
    Is.close();
    FMgr.setPath(Path);
    std::string Str(SizeToRead, '\0');
    if (auto Res = FMgr.readBytes(SizeToRead)) {
      auto *Data = (*Res).data();
      Code = Span<Byte>(reinterpret_cast<Byte *>(*Data), SizeToRead);
    } else {
      spdlog::error(ErrInfo::InfoFile(Path));
      return Unexpect(ErrCode::IllegalPath);
    }
  } else {
    spdlog::error(ErrInfo::InfoFile(Path));
    return Unexpect(ErrCode::IllegalPath);
  }
  if (PrikeyPath.empty() && PubkeyPath.empty()) {
    if (auto Sig = keygen(Code, Path.parent_path()); !Sig) {
      spdlog::error(ErrInfo::InfoFile(Path));
      return Unexpect(Sig);
    } else {
      auto Result = sign(Path, Target, *Sig);
      return Result;
    }
    // return sign(Path, Target, *Sig);
  } else if (!PrikeyPath.empty() && !PubkeyPath.empty()) {
    /// Temporaly return unexpect
    /// In the future need to consider case of third-party key pairs
    spdlog::error(ErrInfo::InfoFile(Path));
    return Unexpect(ErrCode::IllegalPath);
  }
  /// User provides only private key or public key
  /// This is an invalid case
  else {
    spdlog::error(ErrInfo::InfoFile(Path));
    return Unexpect(ErrCode::Unreachable);
  }
}

Expect<bool> Signature::verifyWasmFile(const fs::path &Path,
                                       const fs::path &PubKeyPath) {
  LDMgr LMgr;
  FileMgr FMgr;
  Configure Conf;
  Span<Byte> Code;
  Loader::Loader TempLoader(Conf);
  FMgr.setPath(Path);
  if (auto Res = TempLoader.parseModule(Path)) {
    for (auto CustomSec : (*Res)->getCustomSections()) {
      if (CustomSec.getName() == DEFAULT_CUSOTM_SECTION_NAME) {
        return verify(Code, CustomSec.getContent(), PubKeyPath);
      }
    }
    return Unexpect(Res);
  } else
    return Unexpect(Res);
}

Expect<void> Signature::sign(fs::path Path, fs::path Target,
                             const std::vector<uint8_t> Signature) {
  fs::path Namestem = Path.filename().replace_extension();
  Namestem += "_signed";
  if (Target.empty()) {
    Target = Namestem.replace_extension(".wasm");
    Target = Path.parent_path() / Target;
  }
  if (fs::exists(Target))
    fs::remove(Target);
  fs::copy(Path, Target);
  std::ofstream File(Target.string(),
                     std::ios::binary | std::ios::ate | std::ios::in);
  FileMgr FMgr0;
  FMgr0.setPath(Target);
  try {
    File.exceptions(File.failbit);
  } catch (const std::ios_base::failure &Error) {
  }
  uint8_t SectionId = 0x00;
  uint32_t SectionLen = Signature.size();
  uint32_t NameLen = sizeof DEFAULT_CUSOTM_SECTION_NAME;
  /// FIXME: make it work correctly with parsing
  /// Currently using filestream writing raw bytes
  /// But faced with problem "loading failed: malformed section id, Code: 0x25"
  /// when used Loader to parse module
  File.write(reinterpret_cast<const char *>(&SectionId), 1);
  File.write(reinterpret_cast<const char *>(&SectionLen), 4);
  File.write(reinterpret_cast<const char *>(&NameLen), 4);
  File.write(reinterpret_cast<const char *>(&DEFAULT_CUSOTM_SECTION_NAME),
             NameLen);
  File.write(reinterpret_cast<const char *>(&Signature[0]), SectionLen);
  File.close();
  return {};
}

Expect<bool> Signature::verify(const Span<Byte> Code,
                               const Span<Byte> Signature,
                               const fs::path &PubKeyPath) {
  /// Return void for this template
  FileMgr FMgr;
  Span<Byte> PublicKeyBytes;
  if (std::ifstream Is{PubKeyPath, std::ios::binary | std::ios::ate}) {
    auto SizeToRead = Is.tellg();
    FMgr.setPath(PubKeyPath);
    if (auto Res = FMgr.readBytes(SizeToRead)) {
      auto *Data = (*Res).data();
      PublicKeyBytes = Span<Byte>(reinterpret_cast<Byte *>(*Data), SizeToRead);
    } else {
      spdlog::error(Res.error());
      spdlog::error(ErrInfo::InfoFile(PubKeyPath));
      return Unexpect(ErrCode::IllegalPath);
    }
  }
  return Alg.verify(Code, Signature, PublicKeyBytes);
}

Expect<Span<Byte>> Signature::readBytes(const fs::path &Path) {
  if (std::ifstream Is{Path, std::ios::binary | std::ios::ate}) {
    FileMgr FMgr;
    auto SizeToRead = Is.tellg();
    FMgr.setPath(Path);
    std::string Str(SizeToRead, '\0');
    if (auto Res = FMgr.readBytes(SizeToRead)) {
      auto *Data = (*Res).data();
      return Span<Byte>(reinterpret_cast<Byte *>(*Data), SizeToRead);
    }
    return Unexpect(ErrCode::IllegalPath);
  }
  spdlog::error(ErrInfo::InfoFile(Path));
  return Unexpect(ErrCode::IllegalPath);
}

// Expect<Span<Byte>> keygen(Span<const uint8_t> Code) {
Expect<std::vector<Byte>> Signature::keygen(const Span<uint8_t> Code,
                                            const fs::path &Path) {
  return Alg.keygen(Code, Path);
}

} // namespace Signature
} // namespace WasmEdge