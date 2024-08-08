// Copyright 2019-2023 hdoc
// SPDX-License-Identifier: AGPL-3.0-only

#include <string>
#include <regex>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

#include "Matchers.hpp"
#include "MatcherUtils.hpp"
#include "types/Symbols.hpp"
#include "clang/AST/Comment.h"
#include "clang/Lex/Lexer.h"

bool hdoc::indexer::matchers::utils::isEnclosingNamespaceInList(const clang::Decl* decl, const std::vector<std::string>& list) {
  auto* parent = decl->getDeclContext();
  // The namespace itself counts as its own parent
  // This makes e.g. detail namespaces themselves also be considered detail
  if (auto* namespaceDecl = llvm::dyn_cast<clang::NamespaceDecl>(decl)) {
    parent = namespaceDecl;
  }
  while (parent) {
    if (auto* namespaceDecl = llvm::dyn_cast<clang::NamespaceDecl>(parent)) {
      if (namespaceDecl->isAnonymousNamespace()) {
        parent = parent->getParent();
        continue;
      }
      auto name = namespaceDecl->getNameAsString();
      for (const auto& substr : list) {
        if (name.find(substr) != std::string::npos) {
          return true;
        }
      }
    }
    parent = parent->getParent();
  }
  return false;
}

/// @brief Try to get a SymbolID from a QualType, and return an empty SymbolID if it's not possible
static hdoc::types::SymbolID getTypeSymbolID(const clang::QualType& typ) {
  // Get a TagDecl from the QualType, stripping pointers and references if needed.
  // Pointers and references look like different types to clang. If we want to
  // have working links in our documentation, types need to have consistent IDs
  // regardless if they are pointers, references, or templates (shown below).
  const clang::TagDecl* ret = NULL;
  if (const clang::TagDecl* decl = typ->getAsTagDecl()) {
    ret = decl;
  } else if (typ->isPointerType() && typ->getPointeeType()->getAsTagDecl()) {
    ret = typ->getPointeeType()->getAsTagDecl();
  } else if (typ->isReferenceType() && typ.getNonReferenceType()->getAsTagDecl()) {
    ret = typ.getNonReferenceType()->getAsTagDecl();
  } else {
    ret = NULL;
  }

  // Remove the template, if applicble.
  // If the type is a specialized template, convert it to the original non-specialized
  // templated type and use that for the SymbolID.
  // Otherwise clang will consider the specialized type distinct from the non-specialized
  // type and unnecessarily give it a different ID.
  if (const auto* nonspec = getNonSpecializedVersionOfDecl((clang::TagDecl*)ret)) {
    return buildID(nonspec);
  } else if (ret == NULL) {
    return hdoc::types::SymbolID();
  } else {
    return buildID(ret);
  }
}

static std::vector<hdoc::types::TemplateParam> collectTemplateParams(const clang::Decl*           res,
                                                                     const clang::PrintingPolicy& pp) {
  std::vector<hdoc::types::TemplateParam> templateParams;
  // Rather than dealing independently with ClassTemplateDecl and ClassTemplatePartialSpecializationDecl,
  // we can ask the generic Decl for the "described Template Params", which is exactly what we want
  const auto* describedTemplateParms = res->getDescribedTemplateParams();
  if (describedTemplateParms) {
    for (const auto* paramDecl : describedTemplateParms->asArray()) {
      hdoc::types::TemplateParam tparam;
      if (const auto& templateType = llvm::dyn_cast<clang::TemplateTypeParmDecl>(paramDecl)) {
        tparam.templateType    = hdoc::types::TemplateParam::TemplateType::TemplateTypeParameter;
        tparam.isTypename      = templateType->wasDeclaredWithTypename();
        tparam.isParameterPack = templateType->isParameterPack();
        tparam.name            = templateType->getNameAsString();
        // Get default argument if it exists
        tparam.defaultValue =
            templateType->hasDefaultArgument() ? templateType->getDefaultArgument().getAsString(pp) : "";
      } else if (const auto* nonTypeTemplate = llvm::dyn_cast<clang::NonTypeTemplateParmDecl>(paramDecl)) {
        tparam.templateType    = hdoc::types::TemplateParam::TemplateType::NonTypeTemplate;
        tparam.type            = nonTypeTemplate->getType().getAsString(pp);
        tparam.isParameterPack = nonTypeTemplate->isParameterPack();
        tparam.name            = nonTypeTemplate->getNameAsString();
        // Get default argument if it exists
        tparam.defaultValue =
            nonTypeTemplate->hasDefaultArgument() ? exprToString(nonTypeTemplate->getDefaultArgument(), pp) : "";
      } else if (const auto* templateTemplateType = llvm::dyn_cast<clang::TemplateTemplateParmDecl>(paramDecl)) {
        tparam.templateType = hdoc::types::TemplateParam::TemplateType::TemplateTemplateType;
        tparam.type =
            clang::Lexer::getSourceText(clang::CharSourceRange::getCharRange(templateTemplateType->getSourceRange()),
                                        res->getASTContext().getSourceManager(),
                                        res->getASTContext().getLangOpts());
        tparam.name            = templateTemplateType->getNameAsString();
        tparam.isParameterPack = templateTemplateType->isParameterPack() ? "..." : ""; // What? TODO: investigate
      }
      templateParams.emplace_back(tparam);
    }
  }
  return templateParams;
}

static bool isHiddenFriendFunction(const clang::Decl* res) {
  // Note that unlike other friend declarations, hidden friends have a body attached (see last condition) 
  return res != nullptr && (llvm::isa<clang::FunctionDecl>(res) || llvm::isa<clang::FunctionTemplateDecl>(res)) &&
         res->isCanonicalDecl() && res->getLexicalDeclContext()->getDeclKind() == clang::Decl::Kind::CXXRecord &&
         res->getDeclContext()->getDeclKind() == clang::Decl::Kind::Namespace && res->getBody() != nullptr;
}

void hdoc::indexer::matchers::FunctionMatcher::run(const clang::ast_matchers::MatchFinder::MatchResult& Result) {
  const auto res = Result.Nodes.getNodeAs<clang::FunctionDecl>("function");

  // We must either deliberately ignore deleted functions or document them as deleted. Doing neither will mean
  // they show up as _defined_, which is the opposite of the truth. Not listing them is the easier way out; if
  // we ever do want document them we should also document implicitly deleted constructors and functions.
  if (res->isDeletedAsWritten()) return;

  // Ignore deduction guides, at least for now
  // (generally, these usually are designed to make things work as one would expect, so
  //  having documentation for them is not as important as for other things)
  if(llvm::isa<clang::CXXDeductionGuideDecl>(res)) {
    return;
  }

  // Count the number of functions matched
  this->index->functions.numMatches++;

  // Ignore invalid matches, matches in ignored files, and static functions
  if (res == nullptr ||
      isInIgnoreList(res, this->cfg) || !res->getSourceRange().isValid() ||
      (res->isStatic() && !res->isCXXClassMember()) || isInAnonymousNamespace(res) ||
      (res->getAccess() == clang::AS_private && cfg->ignorePrivateMembers == true)) {
    return;
  }

  const hdoc::types::SymbolID ID = buildID(res);
  if (this->index->functions.contains(ID)) {
    return;
  }
  this->index->functions.reserve(ID);
  hdoc::types::FunctionSymbol f;
  f.ID = ID;
  fillOutSymbol(f, res, this->cfg->rootDir);

  // Determine if the function is a conversion operator early, since it influences proto generation
  if (const auto* conversion = llvm::dyn_cast<clang::CXXConversionDecl>(res)) {
    f.isConversionOp = true;
    f.isExplicit     = conversion->isExplicit();
  }

  // Get a bunch of qualifiers
  f.isVariadic   = res->isVariadic();
  f.isVirtual    = res->isVirtualAsWritten();
  f.isConstexpr  = res->isConstexprSpecified() && !res->isExplicitlyDefaulted();
  f.isConsteval  = res->isConsteval();
  f.isInline     = res->isInlineSpecified();
  f.isNoDiscard  = res->hasAttr<clang::WarnUnusedResultAttr>();
  f.isNoExcept   = clang::isNoexceptExceptionSpec(res->getExceptionSpecType()); // TODO incorrect - may be parameterized!
  f.isNoReturn   = res->isNoReturn();
  f.storageClass = res->getStorageClass();
  f.access       = res->getAccess();

  if (const auto ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(res)) {
    f.isExplicit = ctor->isExplicit();
  }

  // Get ref qualifiers
  if (const auto* fp = res->getType()->getAs<clang::FunctionProtoType>()) {
    f.refQualifier      = fp->getRefQualifier();
    f.hasTrailingReturn = fp->hasTrailingReturn();
  }

  // Add CVR qualifiers
  if (const auto* functionT = res->getType()->getAs<clang::FunctionType>()) {
    f.isConst    = functionT->isConst();
    f.isVolatile = functionT->isVolatile();
    f.isRestrict = functionT->isRestrict();
  }

  // Get arguments and their default values if they exist
  clang::PrintingPolicy pp(res->getASTContext().getLangOpts());
  f.params.reserve(res->param_size());
  for (const auto* i : res->parameters()) {
    hdoc::types::FunctionParam a;
    a.name      = i->getNameAsString();
    auto effectiveType = i->getType();
    effectiveType.removeLocalFastQualifiers(); // drop top-level cvr-qualifiers since they are not part of the signature
    a.type.name = effectiveType.getAsString(pp);
    a.type.id   = getTypeSymbolID(effectiveType);
    if (i->hasDefaultArg()) {
      a.defaultValue = i->hasUninstantiatedDefaultArg() ? exprToString(i->getUninstantiatedDefaultArg(), pp)
                                                        : exprToString(i->getDefaultArg(), pp);
    }
    f.params.emplace_back(a);
  }

  if (clang::FunctionTemplateDecl* templateDecl = res->getDescribedFunctionTemplate()) {
    for (const auto* parameterDecl : *templateDecl->getTemplateParameters()) {
      hdoc::types::TemplateParam tparam;
      if (const auto* templateType = llvm::dyn_cast<clang::TemplateTypeParmDecl>(parameterDecl)) {
        tparam.templateType    = hdoc::types::TemplateParam::TemplateType::TemplateTypeParameter;
        tparam.isParameterPack = templateType->isParameterPack();
        tparam.isTypename      = templateType->wasDeclaredWithTypename();
        tparam.name            = templateType->getNameAsString();
        tparam.defaultValue =
            templateType->hasDefaultArgument() ? templateType->getDefaultArgument().getAsString(pp) : "";
      } else if (const auto* nonTypeTemplate = llvm::dyn_cast<clang::NonTypeTemplateParmDecl>(parameterDecl)) {
        tparam.templateType    = hdoc::types::TemplateParam::TemplateType::NonTypeTemplate;
        tparam.isParameterPack = nonTypeTemplate->isParameterPack();
        tparam.name            = nonTypeTemplate->getNameAsString();
        tparam.defaultValue =
            nonTypeTemplate->hasDefaultArgument() ? exprToString(nonTypeTemplate->getDefaultArgument(), pp) : "";
        tparam.type = nonTypeTemplate->getType().getAsString(pp);
      }
      f.templateParams.emplace_back(tparam);
    }
  }

  const clang::comments::Comment* comment = res->getASTContext().getCommentForDecl(res, nullptr);
  if (comment != nullptr) {
    processSymbolComment(f, comment, res->getASTContext());
  }

  // Don't print "void" return type for constructors and destructors.
  f.isCtorOrDtor = clang::isa<clang::CXXConstructorDecl>(res) || clang::isa<clang::CXXDestructorDecl>(res);
  if (f.isCtorOrDtor == false) {
    f.returnType.name = res->getReturnType().getAsString(pp);
    f.returnType.id   = getTypeSymbolID(res->getReturnType());
  } else {
    // simplify name of the constructors to remove template arguments in case it is a specialization
    f.name = std::regex_replace(f.name, std::regex("<.*>"), "");
  }
  f.isRecordMember = res->isCXXClassMember();
  f.isHiddenFriend = isHiddenFriendFunction(res);

  f.proto          = getFunctionSignature(f);

  fillNamespace(f, res, this->cfg);
  this->index->functions.update(f.ID, f);
}

void hdoc::indexer::matchers::UsingMatcher::run(const clang::ast_matchers::MatchFinder::MatchResult& Result) {
  const auto res = Result.Nodes.getNodeAs<clang::NamedDecl>("using");

  // Only interested in aliases
  if(!llvm::isa_and_present<clang::UsingDecl>(res) && !llvm::isa_and_present<clang::UsingShadowDecl>(res)
     && !llvm::isa_and_present<clang::TypedefNameDecl>(res)) {
    return;
  }

  // We don't care about aliases defined locally in a function scope
  if (res->getDeclContext()->isFunctionOrMethod()) {
    return;
  }

  // Count the number of aliases matched
  this->index->aliases.numMatches++;

  if(isInIgnoreList(res, this->cfg)) spdlog::warn("Ignoring Using [ignore list] : {}", res->getQualifiedNameAsString());
  if(!res->getSourceRange().isValid()) spdlog::warn("Ignoring Using [invalid source range] : {}", res->getQualifiedNameAsString());

  // Ignore invalid matches and matches in ignored files
  if (res == nullptr ||
      isInIgnoreList(res, this->cfg) || !res->getSourceRange().isValid() ||
      (res->getAccess() == clang::AS_private && cfg->ignorePrivateMembers == true)) {
    return;
  }

  const hdoc::types::SymbolID ID = buildID(res);
  if (this->index->aliases.contains(ID)) {
    return;
  }
  this->index->aliases.reserve(ID);

  clang::PrintingPolicy pp(res->getASTContext().getLangOpts());

  hdoc::types::AliasSymbol a;
  a.ID = ID;
  fillOutSymbol(a, res, this->cfg->rootDir);

  a.isRecordMember = res->isCXXClassMember();
  if(a.isRecordMember) a.access = res->getAccess();
  a.templateParams = collectTemplateParams(res, pp);

  a.proto = getTypeAliasProto(a);

  spdlog::debug(" ------------- ");
  spdlog::debug("Using: {}", res->getQualifiedNameAsString());
  if(auto usingDecl = llvm::dyn_cast<clang::UsingDecl>(res)) {
    for(auto shadow : usingDecl->shadows()) {
      spdlog::debug(" + Shadow: {}", shadow->getUnderlyingDecl()->getQualifiedNameAsString());
      // currently, we just store the *last* shadow, which is the most derived one
      a.target.id = buildID(shadow->getUnderlyingDecl());
      a.target.name = shadow->getUnderlyingDecl()->getQualifiedNameAsString();
    }
  } else if(auto usingShadowDecl = llvm::dyn_cast<clang::UsingShadowDecl>(res)) {
    spdlog::debug(" + Target: {}", usingShadowDecl->getTargetDecl()->getQualifiedNameAsString());
    a.target.id = buildID(usingShadowDecl->getTargetDecl());
    a.target.name = usingShadowDecl->getTargetDecl()->getQualifiedNameAsString();
  } else if(auto typeAliasDecl = llvm::dyn_cast<clang::TypedefNameDecl>(res)) {
    std::string result;
    llvm::raw_string_ostream stream(result);
    typeAliasDecl->getUnderlyingType().print(stream, pp);
    stream.flush();
    spdlog::debug(" + Underlying: {}", result);
    a.target.id = getTypeSymbolID(typeAliasDecl->getUnderlyingType());
    a.target.name = result;
  }

  const clang::comments::Comment* comment = res->getASTContext().getCommentForDecl(res, nullptr);
  if (comment != nullptr) {
    processSymbolComment(a, comment, res->getASTContext());
  }

  fillNamespace(a, res, this->cfg);
  this->index->aliases.update(a.ID, a);
}

std::vector<std::string> templateArgsToStrings(const clang::TemplateArgumentList& args, const clang::ASTContext& ctx, const hdoc::types::RecordSymbol& record) {
  std::vector<std::string> ret;
  char fallbackName = 'T';
  for (const auto& arg : args.asArray()) {
    std::string result;
    llvm::raw_string_ostream stream(result);
    clang::PrintingPolicy pp(ctx.getLangOpts());
    arg.print(pp, stream, true);
    stream.flush();

    // Special case handling for template type parameter types, if we don't have a good name we go on a journey to find one
    if(result.starts_with("type-parameter-")) {
      std::string replacement = "";
      if (arg.getKind() == clang::TemplateArgument::ArgKind::Type) {
        auto qt = arg.getAsType();
        if (const auto* templateType = llvm::dyn_cast<clang::TemplateTypeParmType>(qt->getUnqualifiedDesugaredType())) {
          if(auto id = templateType->getIdentifier()) {
            replacement = id->getName().str();
          } else if(auto decl = templateType->getDecl()) {
            replacement = decl->getNameAsString();
          }
          // If we still don't have anything here, we are dealing with a param which no longer has a name at this point
          // we try to look up its name in the template params of the record we are currently processing
          if(replacement == "") {
            auto idx = templateType->getIndex();
            if(idx < record.templateParams.size()) {
              replacement = record.templateParams[idx].name;
            }
          }
        }
      }
      // Final all-else-failed fallback ("T", "U", "V", etc.)
      if(replacement == "") {
        replacement = fallbackName++;
        if (fallbackName > 'Z') {
          fallbackName = 'A';
        }
      }
      ret.emplace_back(replacement);
    } else {
      // We also potentially have template arguments for a template template type, which we just remove for readability
      std::regex re("<.*>");
      result = std::regex_replace(result, re, "<...>");
      ret.emplace_back(result);
    }
  }
  return ret;
}

void hdoc::indexer::matchers::RecordMatcher::run(const clang::ast_matchers::MatchFinder::MatchResult& Result) {
  const auto res = Result.Nodes.getNodeAs<clang::CXXRecordDecl>("record");

  // Count the number of records matched
  this->index->records.numMatches++;

  // Ignore invalid matches
  if (res == nullptr || !res->isCompleteDefinition() || !res->getSourceRange().isValid() ||
      isInIgnoreList(res, this->cfg) || isInAnonymousNamespace(res)) {
    return;
  }

  // Ignore records defined in function scope, those should always be implementation details
  if (res->getDeclContext()->isFunctionOrMethod()) {
    return;
  }

  // Try to deduce name of structs/unions with C-style typedef decls
  // If the name is empty we try to find the name using some AST hackery, and save the name to be populated later
  std::string cachedName;
  if (res->getNameAsString() == "") {
    if (const auto* possibleTagDecl = llvm::cast_or_null<const clang::TagDecl>(res)) {
      if (const clang::TypedefNameDecl* possibleTypedefDecl = possibleTagDecl->getTypedefNameForAnonDecl()) {
        cachedName = possibleTypedefDecl->getNameAsString();
      }
    }
    if (cachedName == "") {
      return;
    }
  }

  // Skip compiler-injected class specializations not caught by the above (pulled from adobe/hyde)
  if (const auto* s = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(res)) {
    if (!s->getTypeAsWritten()) {
      return;
    }
  }

  const hdoc::types::SymbolID ID = buildID(res);
  if (this->index->records.contains(ID)) {
    return;
  }
  this->index->records.reserve(ID);
  hdoc::types::RecordSymbol c;
  c.ID = ID;
  fillOutSymbol(c, res, this->cfg->rootDir);

  // Apply the cached name found earlier for suspected typedef'ed decls
  if (c.name == "") {
    c.name = cachedName;
  }

  if (const auto* parent = llvm::dyn_cast<clang::CXXRecordDecl>(res->getParent())) {
    c.name = parent->getNameAsString() + "::" + c.name;
  }

  // Get methods and decls (what's the difference?) for this record
  for (const auto* m : res->methods()) {
    if (m == nullptr || m->isImplicit() ||
        isInIgnoreList(m, this->cfg) || isInAnonymousNamespace(m) ||
        (m->getAccess() == clang::AS_private && cfg->ignorePrivateMembers == true)) {
      continue;
    }
    c.methodIDs.emplace_back(buildID(m->getCanonicalDecl()));
  }
  for (const auto* d : res->decls()) {
    if (const auto* ftd = llvm::dyn_cast<clang::FunctionTemplateDecl>(d)) {
      if (ftd == nullptr || ftd->isImplicit() ||
          isInIgnoreList(ftd, this->cfg) || isInAnonymousNamespace(ftd) ||
          (ftd->getAccess() == clang::AS_private && cfg->ignorePrivateMembers == true)) {
        continue;
      }
      c.methodIDs.emplace_back(buildID(ftd));
    } else if (const auto* fd = llvm::dyn_cast<clang::FriendDecl>(d)) {
      if (const auto namedFriend = fd->getFriendDecl(); isHiddenFriendFunction(namedFriend)) {
        c.hiddenFriendIDs.emplace_back(buildID(namedFriend));
        spdlog::debug(
            "Added hidden friend {} of {}", namedFriend->getQualifiedNameAsString(), res->getQualifiedNameAsString());
      }
    } else {
      // add aliases
      const clang::NamedDecl* alias = llvm::dyn_cast<clang::UsingShadowDecl>(d);
      if (!alias) alias = llvm::dyn_cast<clang::UsingDecl>(d);
      if (!alias) alias = llvm::dyn_cast<clang::TypedefNameDecl>(d);
      if (alias == nullptr || alias->isImplicit() ||
          isInIgnoreList(alias, this->cfg) || isInAnonymousNamespace(alias) ||
          (alias->getAccess() == clang::AS_private && cfg->ignorePrivateMembers == true)) {
        continue;
      }
      c.aliasIDs.emplace_back(buildID(alias));
      spdlog::debug("Added member alias: {}", alias->getQualifiedNameAsString());
    }
  }

  // Find records this record inherits from
  if (res->isThisDeclarationADefinition()) {
    for (const auto base : res->bases()) {
      if (const auto* baseRecord = base.getType()->getAsCXXRecordDecl()) {
        // add std prefix for records that are in that namespace
        if (baseRecord->isInStdNamespace()) {
          c.baseRecords.push_back(
              {buildID(baseRecord), base.getAccessSpecifier(), "std::" + baseRecord->getNameAsString()});
        }
        // Records that should be in the DB
        else {
          c.baseRecords.push_back({buildID(baseRecord), base.getAccessSpecifier(), baseRecord->getNameAsString()});
        }
      }
    }
  }

  // Determine record type
  c.type = res->getKindName();

  // Get full declaration including templates
  clang::PrintingPolicy pp(res->getASTContext().getLangOpts());
  c.templateParams = collectTemplateParams(res, pp);

  // For template specializations, include the template arguments in the name
  // We do this after the template handling above, so we can re-use the TemplateTypeParm names
  // stored for c for those template arguments which were *not* specialized and are represented
  // as canonical ("type-parameter-*") in the template argument list
  if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(res)) {
    c.name += fmt::format("<{}>", fmt::join(templateArgsToStrings(spec->getTemplateArgs(), res->getASTContext(), c), ", "));
  }

  c.proto = getRecordProto(c);

  // TODO: fix this hack
  // If there is an anonymous struct/enum/union declared as a member variable of a record, clang
  // will make its type "enum (anonymous $TYPE at path/to/file)"
  // This is not ideal, and I haven't found a way to elegantly pinpoint this case
  // Consequently, we're using a substring match to see if that string appears in the type string
  // and then we discard decls that match this condition at the call site
  auto isAnonRecordMemberVar = [&](const auto& decl) {
    return decl->getType().getAsString(pp).find("anonymous ") != std::string::npos;
  };

  // TODO: refactor the member variables and static member variable blocks to consolidate duplicated code
  for (const auto* field : res->fields()) {
    if (field->getAccess() == clang::AS_private && cfg->ignorePrivateMembers == true) {
      continue;
    }

    hdoc::types::MemberVariable mv;
    mv.isStatic     = false;
    mv.name         = field->getNameAsString();
    mv.defaultValue = field->hasInClassInitializer() ? exprToString(field->getInClassInitializer(), pp) : "";
    mv.access       = field->getAccess();

    // Ignore anonymous structs and unions that may appear as member variables
    // Anonymous records have their types recorded as "anonymous struct at $FILE:$LINE"
    // which is ugly, so we replace it with out own
    if (field->isAnonymousStructOrUnion() || isAnonRecordMemberVar(field)) {
      mv.type.name = "anonymous struct/union";
    } else {
      mv.type.name = field->getType().getAsString(pp);
      mv.type.id   = getTypeSymbolID(field->getType());
    }

    const clang::comments::Comment* comment = res->getASTContext().getCommentForDecl(field, nullptr);
    if (comment != nullptr) {
      for (auto c = comment->child_begin(); c != comment->child_end(); ++c) {
        if (const auto* paraComment = llvm::dyn_cast<clang::comments::ParagraphComment>(*c)) {
          mv.docComment = getParaCommentContents(paraComment, res->getASTContext());
        }
      }
    }

    c.vars.emplace_back(mv);
  }

  // Get static members that aren't caught by res->fields()
  for (const auto* d : res->decls()) {
    if (const auto* vd = llvm::dyn_cast<clang::VarDecl>(d)) {
      if (vd == nullptr || (vd->getAccess() == clang::AS_private && cfg->ignorePrivateMembers == true)) {
        continue;
      }

      hdoc::types::MemberVariable mv;
      mv.isStatic     = true;
      mv.name         = vd->getNameAsString();
      mv.defaultValue = vd->hasInit() ? exprToString(vd->getInit(), pp) : "";
      mv.access       = vd->getAccess();

      // See previous section for explanation
      if (isAnonRecordMemberVar(vd)) {
        mv.type.name = "anonymous struct/union";
      } else {
        mv.type.name = vd->getType().getAsString(pp);
        mv.type.id   = getTypeSymbolID(vd->getType());
      }

      const clang::comments::Comment* comment = res->getASTContext().getCommentForDecl(vd, nullptr);
      if (comment != nullptr) {
        for (auto c = comment->child_begin(); c != comment->child_end(); ++c) {
          if (const auto* paraComment = llvm::dyn_cast<clang::comments::ParagraphComment>(*c)) {
            mv.docComment = getParaCommentContents(paraComment, res->getASTContext());
          }
        }
      }

      c.vars.emplace_back(mv);
    }
  }

  const clang::comments::Comment* comment = res->getASTContext().getCommentForDecl(res, nullptr);
  if (comment != nullptr) {
    processSymbolComment(c, comment, res->getASTContext());
  }

  fillNamespace(c, res, this->cfg);
  this->index->records.update(c.ID, c);
}

void hdoc::indexer::matchers::EnumMatcher::run(const clang::ast_matchers::MatchFinder::MatchResult& Result) {
  const auto res = Result.Nodes.getNodeAs<clang::EnumDecl>("enum");

  // Count the number of classes matched
  this->index->enums.numMatches++;

  // Ignore invalid matches and anonymous enums
  if (res == nullptr || res->getNameAsString() == "" ||
      isInIgnoreList(res, this->cfg) || isInAnonymousNamespace(res)) {
    return;
  }

  const hdoc::types::SymbolID ID = buildID(res);
  if (this->index->enums.contains(ID)) {
    return;
  }
  this->index->enums.reserve(ID);
  hdoc::types::EnumSymbol e;
  e.ID = ID;
  fillOutSymbol(e, res, this->cfg->rootDir);

  if (const auto* parent = llvm::dyn_cast<clang::CXXRecordDecl>(res->getParent())) {
    e.name = parent->getNameAsString() + "::" + e.name;
  }

  // Determine if this enum is scoped, i.e. an "enum class" or "enum struct"
  if (res->isScoped()) {
    if (res->isScopedUsingClassTag()) {
      e.type = "enum class";
    } else {
      e.type = "enum struct";
    }
  } else {
    e.type = "enum";
  }

  for (const auto* m : res->enumerators()) {
    hdoc::types::EnumMember em;
    em.name  = m->getNameAsString();
    em.value = m->getInitVal().getExtValue();

    // Grab comment for this enum value
    const auto memberComment = res->getASTContext().getCommentForDecl(m, nullptr);
    if (memberComment != nullptr) {
      for (auto c = memberComment->child_begin(); c != memberComment->child_end(); ++c) {
        if (const auto* paraComment = llvm::dyn_cast<clang::comments::ParagraphComment>(*c)) {
          em.docComment += getParaCommentContents(paraComment, res->getASTContext());
        }
      }
    }
    e.members.emplace_back(em);
  }

  const clang::comments::Comment* comment = res->getASTContext().getCommentForDecl(res, nullptr);
  if (comment != nullptr) {
    processSymbolComment(e, comment, res->getASTContext());
  }

  fillNamespace(e, res, this->cfg);
  this->index->enums.update(e.ID, e);
}

void hdoc::indexer::matchers::NamespaceMatcher::run(const clang::ast_matchers::MatchFinder::MatchResult& Result) {
  const auto res = Result.Nodes.getNodeAs<clang::NamespaceDecl>("namespace");

  // Count the number of namespaces matched
  this->index->namespaces.numMatches++;

  // Ignore invalid matches and anonymous enums
  if (res == nullptr || res->getNameAsString() == "" ||
      isInIgnoreList(res, this->cfg) || isInAnonymousNamespace(res)) {
    return;
  }

  const hdoc::types::SymbolID ID = buildID(res);
  if (this->index->namespaces.contains(ID)) {
    return;
  }
  this->index->namespaces.reserve(ID);
  hdoc::types::NamespaceSymbol n;
  n.ID = ID;
  fillOutSymbol(n, res, this->cfg->rootDir);

  fillNamespace(n, res, this->cfg);
  this->index->namespaces.update(n.ID, n);
}
