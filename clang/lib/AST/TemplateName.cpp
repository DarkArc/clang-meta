//===- TemplateName.cpp - C++ Template Name Representation ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TemplateName interface and subclasses.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/TemplateName.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DependenceFlags.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/TemplateBase.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <string>

using namespace clang;

TemplateArgument
SubstTemplateTemplateParmPackStorage::getArgumentPack() const {
  return TemplateArgument(llvm::makeArrayRef(Arguments, size()));
}

void SubstTemplateTemplateParmStorage::Profile(llvm::FoldingSetNodeID &ID) {
  Profile(ID, Parameter, Replacement);
}

void SubstTemplateTemplateParmStorage::Profile(llvm::FoldingSetNodeID &ID,
                                           TemplateTemplateParmDecl *parameter,
                                               TemplateName replacement) {
  ID.AddPointer(parameter);
  ID.AddPointer(replacement.getAsVoidPointer());
}

void SubstTemplateTemplateParmPackStorage::Profile(llvm::FoldingSetNodeID &ID,
                                                   ASTContext &Context) {
  Profile(ID, Context, Parameter, getArgumentPack());
}

void SubstTemplateTemplateParmPackStorage::Profile(llvm::FoldingSetNodeID &ID,
                                                   ASTContext &Context,
                                           TemplateTemplateParmDecl *Parameter,
                                             const TemplateArgument &ArgPack) {
  ID.AddPointer(Parameter);
  ArgPack.Profile(ID, Context);
}

void SplicedTemplateReflectionStorage::Profile(llvm::FoldingSetNodeID &ID) {
  Profile(ID, Refl, Temp);
}

void SplicedTemplateReflectionStorage::Profile(llvm::FoldingSetNodeID &ID,
                                               Expr *E,
                                               const TemplateDecl *D) {
  ID.AddPointer(E);
  ID.AddPointer(D);
}

TemplateNameDependence SplicedTemplateReflectionStorage::getDependence() const {
  // Well, this is awkward, but it's probably right:
  return toTemplateNameDependence(
    toNestedNameSpecifierDependendence(
      toTypeDependence(Refl->getDependence())));
}

bool SplicedTemplateReflectionStorage::isDependent() const {
  return getDependence() & TemplateNameDependence::Dependent;
}

TemplateName::TemplateName(void *Ptr) {
  Storage = StorageType::getFromOpaqueValue(Ptr);
}

TemplateName::TemplateName(TemplateDecl *Template) : Storage(Template) {}
TemplateName::TemplateName(OverloadedTemplateStorage *Storage)
    : Storage(Storage) {}
TemplateName::TemplateName(AssumedTemplateStorage *Storage)
    : Storage(Storage) {}
TemplateName::TemplateName(SubstTemplateTemplateParmStorage *Storage)
    : Storage(Storage) {}
TemplateName::TemplateName(SubstTemplateTemplateParmPackStorage *Storage)
    : Storage(Storage) {}
TemplateName::TemplateName(SplicedTemplateReflectionStorage *Storage)
    : Storage(Storage) {}
TemplateName::TemplateName(QualifiedTemplateName *Qual) : Storage(Qual) {}
TemplateName::TemplateName(DependentTemplateName *Dep) : Storage(Dep) {}

bool TemplateName::isNull() const { return Storage.isNull(); }

TemplateName::NameKind TemplateName::getKind() const {
  if (Storage.is<TemplateDecl *>())
    return Template;
  if (Storage.is<DependentTemplateName *>())
    return DependentTemplate;
  if (Storage.is<QualifiedTemplateName *>())
    return QualifiedTemplate;

  UncommonTemplateNameStorage *uncommon
    = Storage.get<UncommonTemplateNameStorage*>();
  if (uncommon->getAsOverloadedStorage())
    return OverloadedTemplate;
  if (uncommon->getAsAssumedTemplateName())
    return AssumedTemplate;
  if (uncommon->getAsSplicedTemplateReflection())
    return SplicedTemplateReflection;
  if (uncommon->getAsSubstTemplateTemplateParm())
    return SubstTemplateTemplateParm;
  return SubstTemplateTemplateParmPack;
}

TemplateDecl *TemplateName::getAsTemplateDecl() const {
  if (TemplateDecl *Template = Storage.dyn_cast<TemplateDecl *>())
    return Template;

  if (QualifiedTemplateName *QTN = getAsQualifiedTemplateName())
    return QTN->getTemplateDecl();

  if (SubstTemplateTemplateParmStorage *sub = getAsSubstTemplateTemplateParm())
    return sub->getReplacement().getAsTemplateDecl();

  if (auto *splice = getAsSplicedTemplateReflection())
    return splice->getDesignatedTemplate();

  return nullptr;
}

OverloadedTemplateStorage *TemplateName::getAsOverloadedTemplate() const {
  if (UncommonTemplateNameStorage *Uncommon =
          Storage.dyn_cast<UncommonTemplateNameStorage *>())
    return Uncommon->getAsOverloadedStorage();

  return nullptr;
}

AssumedTemplateStorage *TemplateName::getAsAssumedTemplateName() const {
  if (UncommonTemplateNameStorage *Uncommon =
          Storage.dyn_cast<UncommonTemplateNameStorage *>())
    return Uncommon->getAsAssumedTemplateName();

  return nullptr;
}

SubstTemplateTemplateParmStorage *
TemplateName::getAsSubstTemplateTemplateParm() const {
  if (UncommonTemplateNameStorage *uncommon =
          Storage.dyn_cast<UncommonTemplateNameStorage *>())
    return uncommon->getAsSubstTemplateTemplateParm();

  return nullptr;
}

SubstTemplateTemplateParmPackStorage *
TemplateName::getAsSubstTemplateTemplateParmPack() const {
  if (UncommonTemplateNameStorage *Uncommon =
          Storage.dyn_cast<UncommonTemplateNameStorage *>())
    return Uncommon->getAsSubstTemplateTemplateParmPack();

  return nullptr;
}

SplicedTemplateReflectionStorage *
TemplateName::getAsSplicedTemplateReflection() const {
  if (UncommonTemplateNameStorage *Uncommon =
          Storage.dyn_cast<UncommonTemplateNameStorage *>())
    return Uncommon->getAsSplicedTemplateReflection();

  return nullptr;
}

QualifiedTemplateName *TemplateName::getAsQualifiedTemplateName() const {
  return Storage.dyn_cast<QualifiedTemplateName *>();
}

DependentTemplateName *TemplateName::getAsDependentTemplateName() const {
  return Storage.dyn_cast<DependentTemplateName *>();
}

TemplateName TemplateName::getNameToSubstitute() const {
  TemplateDecl *Decl = getAsTemplateDecl();

  // Substituting a dependent template name: preserve it as written.
  if (!Decl)
    return *this;

  // If we have a template declaration, use the most recent non-friend
  // declaration of that template.
  Decl = cast<TemplateDecl>(Decl->getMostRecentDecl());
  while (Decl->getFriendObjectKind()) {
    Decl = cast<TemplateDecl>(Decl->getPreviousDecl());
    assert(Decl && "all declarations of template are friends");
  }
  return TemplateName(Decl);
}

TemplateNameDependence TemplateName::getDependence() const {
  auto D = TemplateNameDependence::None;
  switch (getKind()) {
  case TemplateName::NameKind::QualifiedTemplate:
    D |= toTemplateNameDependence(
        getAsQualifiedTemplateName()->getQualifier()->getDependence());
    break;
  case TemplateName::NameKind::DependentTemplate:
    D |= toTemplateNameDependence(
        getAsDependentTemplateName()->getQualifier()->getDependence());
    break;
  case TemplateName::NameKind::SubstTemplateTemplateParmPack:
    D |= TemplateNameDependence::UnexpandedPack;
    break;
  case TemplateName::NameKind::SplicedTemplateReflection:
    D |= getAsSplicedTemplateReflection()->getDependence();
    break;
  case TemplateName::NameKind::OverloadedTemplate:
    llvm_unreachable("overloaded templates shouldn't survive to here.");
  default:
    break;
  }
  if (TemplateDecl *Template = getAsTemplateDecl()) {
    if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(Template)) {
      D |= TemplateNameDependence::DependentInstantiation;
      if (TTP->isParameterPack())
        D |= TemplateNameDependence::UnexpandedPack;
    }
    // FIXME: Hack, getDeclContext() can be null if Template is still
    // initializing due to PCH reading, so we check it before using it.
    // Should probably modify TemplateSpecializationType to allow constructing
    // it without the isDependent() checking.
    if (Template->getDeclContext() &&
        Template->getDeclContext()->isDependentContext())
      D |= TemplateNameDependence::DependentInstantiation;
  } else {
    D |= TemplateNameDependence::DependentInstantiation;
  }
  return D;
}

bool TemplateName::isDependent() const {
  return getDependence() & TemplateNameDependence::Dependent;
}

bool TemplateName::isInstantiationDependent() const {
  return getDependence() & TemplateNameDependence::Instantiation;
}

bool TemplateName::containsUnexpandedParameterPack() const {
  return getDependence() & TemplateNameDependence::UnexpandedPack;
}

void
TemplateName::print(raw_ostream &OS, const PrintingPolicy &Policy,
                    bool SuppressNNS) const {
  if (TemplateDecl *Template = Storage.dyn_cast<TemplateDecl *>())
    OS << *Template;
  else if (QualifiedTemplateName *QTN = getAsQualifiedTemplateName()) {
    if (!SuppressNNS)
      QTN->getQualifier()->print(OS, Policy);
    if (QTN->hasTemplateKeyword())
      OS << "template ";
    OS << *QTN->getDecl();
  } else if (DependentTemplateName *DTN = getAsDependentTemplateName()) {
    if (!SuppressNNS && DTN->getQualifier())
      DTN->getQualifier()->print(OS, Policy);
    OS << "template ";

    if (DTN->isIdentifier())
      OS << DTN->getIdentifier()->getName();
    else
      OS << "operator " << getOperatorSpelling(DTN->getOperator());
  } else if (SubstTemplateTemplateParmStorage *subst
               = getAsSubstTemplateTemplateParm()) {
    subst->getReplacement().print(OS, Policy, SuppressNNS);
  } else if (SubstTemplateTemplateParmPackStorage *SubstPack
                                        = getAsSubstTemplateTemplateParmPack())
    OS << *SubstPack->getParameterPack();
  else if (AssumedTemplateStorage *Assumed = getAsAssumedTemplateName()) {
    Assumed->getDeclName().print(OS, Policy);
  } else if (SplicedTemplateReflectionStorage *Splice = getAsSplicedTemplateReflection()) {
    Expr *E = Splice->getReflection();
    E->printPretty(OS, nullptr, Policy);
  }
  else {
    OverloadedTemplateStorage *OTS = getAsOverloadedTemplate();
    (*OTS->begin())->printName(OS);
  }
}

const StreamingDiagnostic &clang::operator<<(const StreamingDiagnostic &DB,
                                             TemplateName N) {
  std::string NameStr;
  llvm::raw_string_ostream OS(NameStr);
  LangOptions LO;
  LO.CPlusPlus = true;
  LO.Bool = true;
  OS << '\'';
  N.print(OS, PrintingPolicy(LO));
  OS << '\'';
  OS.flush();
  return DB << NameStr;
}

void TemplateName::dump(raw_ostream &OS) const {
  LangOptions LO;  // FIXME!
  LO.CPlusPlus = true;
  LO.Bool = true;
  print(OS, PrintingPolicy(LO));
}

LLVM_DUMP_METHOD void TemplateName::dump() const {
  dump(llvm::errs());
}
