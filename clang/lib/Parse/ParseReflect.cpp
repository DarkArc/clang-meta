//===--- Parser.cpp - C Language Family Parser ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements parsing for reflection facilities.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/Parser.h"
#include "clang/AST/ASTContext.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/ParsedReflection.h"
using namespace clang;

// Assuming the current token is '::' returns true if the next tokens would
// start a nested-name-specifier (i.e., the next token is an identifier or
// (invalidly) the 'template' keyword).
static bool startsNestedNameSpecifier(Parser& P) {
  return P.NextToken().is(tok::identifier) ||
         P.NextToken().is(tok::kw_template);
}

ParsedReflectionOperand Parser::ParseCXXReflectionOperand() {
  // Prevent diagnostics in this context.
  Sema::CXXReflectionScopeRAII ReflectionScope(Actions);

  // The operand is unevaluated.
  EnterExpressionEvaluationContext Unevaluated(
      Actions, Sema::ExpressionEvaluationContext::Unevaluated);

  // Perform the tentative parses first since isCXXTypeId tends to rewrite
  // tokens, which can make subsequent parses a bit wonky.

  // Match '^::'. Note that '::' could start a nested-name-specifier, so if the
  // next token is an identifier or 'template', then this is not a reflection of
  // the global scope.
  if (Tok.is(tok::coloncolon) && !startsNestedNameSpecifier(*this)) {
    SourceLocation ColonColonLoc = ConsumeToken();
    return Actions.ActOnReflectedNamespace(ColonColonLoc);
  }

  // Tentatively parse a template-name.
  {
    TentativeParsingAction TPA(*this);
    ParsedTemplateArgument T = ParseTemplateTemplateArgument();
    if (!T.isInvalid()) {
      TPA.Commit();
      return Actions.ActOnReflectedTemplate(T);
    }
    TPA.Revert();
  }

  // Otherwise, tentatively parse a namespace-name.
  {
    TentativeParsingAction TPA(*this);
    CXXScopeSpec SS;
    SourceLocation IdLoc;
    Decl *D = ParseNamespaceName(SS, IdLoc);
    if (D) {
      TPA.Commit();
      return Actions.ActOnReflectedNamespace(SS, IdLoc, D);
    }
    TPA.Revert();
  }

  // Otherwise, try parsing this as type-id.
  if (isCXXTypeId(TypeIdAsTemplateArgument)) {
    // FIXME: Create a new DeclaratorContext?
    TypeResult T =
      ParseTypeName(nullptr, DeclaratorContext::TemplateArg);
    if (T.isInvalid()) {
      return ParsedReflectionOperand();
    }
    return Actions.ActOnReflectedType(T.get());
  }

  // Parse an expression. template argument.
  ExprResult E = ParseCastExpression(AnyCastExpr);
  if (E.isInvalid() || !E.get())
    return ParsedReflectionOperand();

  return Actions.ActOnReflectedExpression(E.get());
}

ExprResult Parser::ParseCXXReflectionExpression() {
  assert(Tok.is(tok::caret) && "expected '^'");
  SourceLocation Loc = ConsumeToken();

  ParsedReflectionOperand PR = ParseCXXReflectionOperand();
  if (PR.isInvalid())
    return ExprError();

  // FIXME: The source locations are wrong.
  return Actions.ActOnCXXReflectExpr(Loc, PR, Loc, Loc);
}

/// Parse an invalid reflection.
///
/// \verbatim
///  primary-expression:
///    __valid_reflection '(' error-message ')'
/// \endverbatim
ExprResult Parser::ParseCXXInvalidReflectionExpression() {
  assert(Tok.is(tok::kw___invalid_reflection) && "Not '__invalid_reflection'");

  SourceLocation BuiltinLoc = ConsumeToken();
  BalancedDelimiterTracker T(*this, tok::l_paren);

  if (T.expectAndConsume(diag::err_expected_lparen_after, "__invalid_reflection"))
    return ExprError();

  ExprResult MessageExpr = ParseConstantExpression();

  if (MessageExpr.isInvalid()) {
    SkipUntil(tok::r_paren, StopAtSemi);
    return ExprError();
  }

  if (T.consumeClose())
    return ExprError();

  return Actions.ActOnCXXInvalidReflectionExpr(MessageExpr.get(), BuiltinLoc,
                                               T.getCloseLocation());
}

/// Parse a reflection trait.
///
/// \verbatim
///   primary-expression:
///     __reflect '(' expression-list ')'
/// \endverbatim
ExprResult Parser::ParseCXXReflectionReadQuery() {
  assert(Tok.is(tok::kw___reflect) && "Not __reflect");
  SourceLocation Loc = ConsumeToken();

  // Parse any number of arguments in parens.
  BalancedDelimiterTracker Parens(*this, tok::l_paren);
  if (Parens.expectAndConsume())
    return ExprError();

  SmallVector<Expr *, 2> Args;
  do {
    ExprResult Expr = ParseConstantExpression();
    if (Expr.isInvalid()) {
      Parens.skipToEnd();
      return ExprError();
    }
    Args.push_back(Expr.get());
  } while (TryConsumeToken(tok::comma));

  if (Parens.consumeClose())
    return ExprError();

  SourceLocation LPLoc = Parens.getOpenLocation();
  SourceLocation RPLoc = Parens.getCloseLocation();
  return Actions.ActOnCXXReflectionReadQuery(Loc, Args, LPLoc, RPLoc);
}

/// Parse a reflective pretty print of integer and string values.
///
/// \verbatim
///   primary-expression:
///     __reflect_print '(' expression-list ')'
/// \endverbatim
ExprResult Parser::ParseCXXReflectPrintLiteralExpression() {
  assert(Tok.is(tok::kw___reflect_print) && "Not __reflect_print");
  SourceLocation Loc = ConsumeToken();

  // Parse any number of arguments in parens.
  BalancedDelimiterTracker Parens(*this, tok::l_paren);
  if (Parens.expectAndConsume())
    return ExprError();

  SmallVector<Expr *, 2> Args;
  do {
    ExprResult Expr = ParseConstantExpression();
    if (Expr.isInvalid()) {
      Parens.skipToEnd();
      return ExprError();
    }
    Args.push_back(Expr.get());
  } while (TryConsumeToken(tok::comma));

  if (Parens.consumeClose())
    return ExprError();

  SourceLocation LPLoc = Parens.getOpenLocation();
  SourceLocation RPLoc = Parens.getCloseLocation();
  return Actions.ActOnCXXReflectPrintLiteral(Loc, Args, LPLoc, RPLoc);
}

/// Parse a reflective pretty print of a reflection.
///
/// \verbatim
///   primary-expression:
///     __reflect_pretty_print '(' reflection ')'
/// \endverbatim
ExprResult Parser::ParseCXXReflectPrintReflectionExpression() {
  assert(Tok.is(tok::kw___reflect_pretty_print) && "Not __reflect_pretty_print");
  SourceLocation Loc = ConsumeToken();

  // Parse any number of arguments in parens.
  BalancedDelimiterTracker Parens(*this, tok::l_paren);
  if (Parens.expectAndConsume())
    return ExprError();

  ExprResult Reflection = ParseConstantExpression();
  if (Reflection.isInvalid()) {
    Parens.skipToEnd();
    return ExprError();
  }

  if (Parens.consumeClose())
    return ExprError();

  SourceLocation LPLoc = Parens.getOpenLocation();
  SourceLocation RPLoc = Parens.getCloseLocation();
  return Actions.ActOnCXXReflectPrintReflection(Loc, Reflection.get(),
                                                LPLoc, RPLoc);
}

/// Parse a reflective dump of a reflection.
///
/// \verbatim
///   primary-expression:
///     __reflect_dump '(' reflection ')'
/// \endverbatim
ExprResult Parser::ParseCXXReflectDumpReflectionExpression() {
  assert(Tok.is(tok::kw___reflect_dump) && "Not __reflect_dump");
  SourceLocation Loc = ConsumeToken();

  // Parse any number of arguments in parens.
  BalancedDelimiterTracker Parens(*this, tok::l_paren);
  if (Parens.expectAndConsume())
    return ExprError();

  ExprResult Reflection = ParseConstantExpression();
  if (Reflection.isInvalid()) {
    Parens.skipToEnd();
    return ExprError();
  }

  if (Parens.consumeClose())
    return ExprError();

  SourceLocation LPLoc = Parens.getOpenLocation();
  SourceLocation RPLoc = Parens.getCloseLocation();
  return Actions.ActOnCXXReflectDumpReflection(Loc, Reflection.get(),
                                               LPLoc, RPLoc);
}

ExprResult Parser::ParseCXXCompilerErrorExpression() {
  assert(Tok.is(tok::kw___compiler_error) && "Not '__compiler_error'");

  SourceLocation BuiltinLoc = ConsumeToken();
  BalancedDelimiterTracker T(*this, tok::l_paren);

  if (T.expectAndConsume(diag::err_expected_lparen_after, "__compiler_error"))
    return ExprError();

  ExprResult MessageExpr = ParseConstantExpression();

  if (MessageExpr.isInvalid()) {
    SkipUntil(tok::r_paren, StopAtSemi);
    return ExprError();
  }

  if (T.consumeClose())
    return ExprError();

  return Actions.ActOnCXXCompilerErrorExpr(MessageExpr.get(), BuiltinLoc,
                                           T.getCloseLocation());
}

bool Parser::matchCXXSpliceBegin(tok::TokenKind T, unsigned LookAhead) {
  if (!getLangOpts().Reflection)
    return false;

  if (getRelativeToken(LookAhead).isNot(tok::l_square))
    return false;
  if (getRelativeToken(LookAhead + 1).isNot(T))
    return false;
  return true;
}

bool Parser::matchCXXSpliceEnd(tok::TokenKind T, unsigned LookAhead) {
  if (getRelativeToken(LookAhead).isNot(T))
    return false;
  if (getRelativeToken(LookAhead + 1).isNot(tok::r_square))
    return false;
  return true;
}

bool Parser::isCXXPackSpliceBegin(unsigned LookAhead) {
  if (getRelativeToken(LookAhead).isNot(tok::ellipsis))
    return false;

  return matchCXXSpliceBegin(tok::colon, LookAhead + 1);
}

bool Parser::parseCXXSpliceBegin(tok::TokenKind T, SourceLocation &SL) {
  if (!matchCXXSpliceBegin(T))
    return true;

  SL = ConsumeBracket();
  ConsumeToken();
  return false;
}

bool Parser::parseCXXSpliceEnd(tok::TokenKind T, SourceLocation &SL) {
  if (!matchCXXSpliceEnd(T)) {
    Diag(Tok, diag::err_expected_end_of_splice);
    return true;
  }

  ConsumeToken();
  SL = ConsumeBracket();
  return false;
}

/// Parse an expression splice expression.
///
/// \verbatim
///   decl-splice:
///     '[: constant-expression ':]'
/// \endverbatim
ExprResult Parser::ParseCXXExprSpliceExpr() {
  assert(matchCXXSpliceBegin(tok::colon) && "Not '[:'");

  // Try parsing the splice as part of a nested-name-specifier. Note that
  // this will annotate the splice as a single token.
  CXXScopeSpec SS;
  if (ParseOptionalCXXScopeSpecifier(SS,
                                      /*ObjectType=*/nullptr,
                                      /*ObjectHasErrors=*/false,
                                      /*EnteringContext=*/false))
    return ExprError();

  if (Tok.is(tok::annot_reflection_splice)) {
    // A splice cannot be the inner identifier of a nested-name-specifier.
    if (SS.isValid()) {
      // FIXME: This is probable the wrong diagnostic.
      Diag(Tok.getLocation(), diag::err_expected) << "identifier";
      return ExprError();
    }
    ExprResult Refl = getExprAnnotation(Tok);
    SourceLocation EndLoc = Tok.getEndLoc();
    SourceLocation StartLoc = ConsumeAnnotationToken();
    return Actions.ActOnCXXExprSpliceExpr(StartLoc, Refl.get(), EndLoc);
  }

  // FIXME: What else could this be?
  assert(false && "Unhandled parse");
}

ExprResult Parser::ParseCXXMemberExprSpliceExpr(Expr *Base, bool IsTemplate) {

  // FIXME: This is broken for template parsing.

  assert(Tok.isOneOf(tok::arrow, tok::period));

  bool IsArrow = Tok.getKind() == tok::arrow;
  SourceLocation OperatorLoc = ConsumeToken();
  SourceLocation TemplateKWLoc;
  if (Tok.is(tok::kw_template))
    TemplateKWLoc = ConsumeToken();

  SourceLocation SBELoc;
  if (parseCXXSpliceBegin(tok::colon, SBELoc))
    return ExprError();

  ExprResult Expr = ParseConstantExpression();
  if (Expr.isInvalid())
    return ExprError();

  SourceLocation SEELoc;
  if (parseCXXSpliceEnd(tok::colon, SEELoc))
    return ExprError();

  // Check for template arguments
  if (Tok.is(tok::less) && !TemplateKWLoc.isInvalid()) {
    SourceLocation LAngleLoc;
    TemplateArgList TemplateArgs;
    SourceLocation RAngleLoc;

    if (ParseTemplateIdAfterTemplateName(/*ConsumeLastToken=*/true,
                                         LAngleLoc, TemplateArgs, RAngleLoc))
      return ExprError();

    ASTTemplateArgsPtr TemplateArgsPtr(TemplateArgs);

    return Actions.ActOnCXXMemberExprSpliceExpr(
        Base, Expr.get(), IsArrow, OperatorLoc, TemplateKWLoc,
        SBELoc, SEELoc, LAngleLoc, TemplateArgsPtr, RAngleLoc);
  }

  return Actions.ActOnCXXMemberExprSpliceExpr(
      Base, Expr.get(), IsArrow, OperatorLoc,
      TemplateKWLoc, SBELoc, SEELoc, /*TemplateArgs=*/nullptr);
}

/// Parse pack splice expression.
///
/// \verbatim
///   pack-splice:
///     '...' '[' '<' constant-expression '>' ']'
/// \endverbatim
ExprResult Parser::ParseCXXPackSpliceExpr() {
  assert(isCXXPackSpliceBegin() && "Not '[<'");
  SourceLocation EllipsisLoc = ConsumeToken();

  SourceLocation SBELoc;
  if (parseCXXSpliceBegin(tok::colon, SBELoc))
    return ExprError();

  ExprResult Expr = ParseConstantExpression();
  if (Expr.isInvalid())
    return ExprError();

  SourceLocation SEELoc;
  if (parseCXXSpliceEnd(tok::colon, SEELoc))
    return ExprError();

  return Actions.ActOnCXXPackSpliceExpr(EllipsisLoc, SBELoc,
                                        Expr.get(), SEELoc);
}

bool Parser::AnnotateIdentifierSplice() {
  assert(matchCXXSpliceBegin(tok::hash) && GetLookAheadToken(2).isNot(tok::ellipsis));

  // Attempt to reinterpret an identifier splice as a single annotated token.
  IdentifierInfo *II;
  SourceLocation IIBeginLoc;
  SourceLocation IIEndLoc;

  bool Invalid = ParseCXXIdentifierSplice(II, IIBeginLoc, IIEndLoc);
  if (Invalid) {
    // If invalid replace the identifier with a unique invalid identifier
    // for recovery purposes.
    Actions.ActOnCXXInvalidIdentifierSplice(II);
  }

  Token AnnotTok;
  AnnotTok.startToken();
  AnnotTok.setKind(Invalid ? tok::annot_invalid_identifier_splice
                           : tok::annot_identifier_splice);
  AnnotTok.setAnnotationValue(reinterpret_cast<void *>(II));
  AnnotTok.setLocation(IIBeginLoc);
  AnnotTok.setAnnotationEndLoc(IIEndLoc);
  UnconsumeToken(AnnotTok);

  return Invalid;
}

bool Parser::TryAnnotateIdentifierSplice() {
  if (!matchCXXSpliceBegin(tok::hash) || GetLookAheadToken(2).is(tok::ellipsis))
    return false;

  return AnnotateIdentifierSplice();
}

bool Parser::ParseCXXIdentifierSplice(
    IdentifierInfo *&Id, SourceLocation &IdBeginLoc) {
  SourceLocation IdEndLoc;
  return ParseCXXIdentifierSplice(Id, IdBeginLoc, IdEndLoc);
}

/// Parse an identifier splice
///
///   identifier-splice:
///     '[' '#' reflection '#' ']'
///
/// Returns true if parsing or semantic analysis fail.
bool Parser::ParseCXXIdentifierSplice(
    IdentifierInfo *&Id, SourceLocation &IdBeginLoc, SourceLocation &IdEndLoc) {
  assert(matchCXXSpliceBegin(tok::hash) && "Not '[#'");

  if (parseCXXSpliceBegin(tok::hash, IdBeginLoc))
    return true;

  SmallVector<Expr *, 4> Parts;
  while (true) {
    ExprResult Result = ParseConstantExpression();
    if (Result.isInvalid()) {
      SkipUntil(tok::r_square);
      return true;
    }

    Parts.push_back(Result.get());
    if (matchCXXSpliceEnd(tok::hash))
      break;

    if (ExpectAndConsume(tok::comma)) {
      SkipUntil(tok::r_square);
      return true;
    }
  }

  if (parseCXXSpliceEnd(tok::hash, IdEndLoc))
    return true;

  ArrayRef<Expr *> FinalParts(Parts.data(), Parts.size());
  if (Actions.ActOnCXXIdentifierSplice(FinalParts, Id))
    return true;

  return false;
}

/// Parse a type splice
///
/// \verbatim
///   type-splice:
///     [: reflection :]
///     [: reflection :] template < template-argument-list >
/// \endverbatim
SourceLocation Parser::ParseTypeSplice(DeclSpec &DS) {
  assert((Tok.is(tok::annot_type_splice) ||
          (Tok.is(tok::kw_typename) &&
           matchCXXSpliceBegin(tok::colon, /*LookAhead=*/1)))
         && "Not a type splice");

  ExprResult Result;
  SourceLocation StartLoc = Tok.getLocation();
  SourceLocation EndLoc;

  if (Tok.is(tok::annot_type_splice)) {
    Result = getExprAnnotation(Tok);
    EndLoc = Tok.getAnnotationEndLoc();
    ConsumeAnnotationToken();
    if (Result.isInvalid()) {
      DS.SetTypeSpecError();
      return SourceLocation();
    }
  } else {
    StartLoc = ConsumeToken();

    SourceLocation SBELoc;
    if (parseCXXSpliceBegin(tok::colon, SBELoc))
      return SourceLocation();

    Result = ParseConstantExpression();
    if (Result.isInvalid()) {
      DS.SetTypeSpecError();
      return SourceLocation();
    }

    if (parseCXXSpliceEnd(tok::colon, EndLoc))
      return SourceLocation();
  }

  const char *PrevSpec = nullptr;
  unsigned DiagID;
  const PrintingPolicy &Policy = Actions.getASTContext().getPrintingPolicy();

  if (DS.SetTypeSpecType(DeclSpec::TST_type_splice, StartLoc, PrevSpec,
                         DiagID, Result.get(), Policy)) {
    Diag(StartLoc, DiagID) << PrevSpec;
    DS.SetTypeSpecError();
  }
  return EndLoc;
}

void Parser::AnnotateExistingTypeSplice(const DeclSpec &DS,
                                        SourceLocation StartLoc,
                                        SourceLocation EndLoc) {
  // make sure we have a token we can turn into an annotation token
  if (PP.isBacktrackEnabled())
    PP.RevertCachedTokens(1);
  else
    PP.EnterToken(Tok, /*IsReinject*/true);

  Tok.setKind(tok::annot_type_splice);
  setExprAnnotation(Tok,
                    DS.getTypeSpecType() == TST_type_splice ?
                    DS.getRepAsExpr() : ExprError());
  Tok.setAnnotationEndLoc(EndLoc);
  Tok.setLocation(StartLoc);
  PP.AnnotateCachedTokens(Tok);
}

/// Parse a reflection splice.
///
///   splice:
///     [: constant-expression :]
///
bool Parser::ParseReflectionSplice(ParsedSplice &Splice) {
  // This is already a splice token.
  if (Tok.is(tok::annot_reflection_splice)) {
    Splice.Start = Tok.getLocation();
    Splice.End = Tok.getEndLoc();
    Splice.Refl = getExprAnnotation(Tok);
    return false;
  }

  assert(matchCXXReflectionSpliceBegin());

  // Parse the splice.
  parseCXXReflectionSpliceBegin(Splice.Start);
  Splice.Refl = ParseConstantExpression();
  if (Splice.Refl.isInvalid())
    return true;
  if (parseCXXReflectionSpliceEnd(Splice.End))
    return true;

  return false;
}

/// Parse a reflection splice.
///
///   splice:
///     [: constant-expression :]
///
/// \param IsTypename True if this is part of a typename-specifier. A typename
/// specifier can be followed by template arguments.
///
/// \param TemplateKeywordLoc If valid, the location of the preceding 'template'
/// keyword. This idn
///
/// \return True if the splice is obviously ill-formed.
///
/// FIXME: We need to pass the 'template' keyword as part of the template name,
/// if its present. It would also be nice to parse pass the 'typename' keyword,
/// but I don't think we have access to it in the calling contexts.
bool Parser::ParseReflectionSplice(CXXScopeSpec &SS, ParsedSplice &Splice,
                                   bool IsTypename,
                                   SourceLocation TemplateKeywordLoc) {
  if (ParseReflectionSplice(Splice))
    return true;

  // When the splice is followed by a template argument list, try parsing
  // the arguments and annotating the token as a template-id.
  auto AnnotateTemplateId = [&](){
    TemplateTy Template;
    TemplateNameKind Kind = Actions.ActOnTemplateSplice(SS, SourceLocation(),
                                                        Splice.Refl.get(),
                                                        Template);
    if (Kind == TNK_Non_template)
      return true;
    UnqualifiedId TemplateName;
    TemplateName.setSplicedTemplateName(Template, Splice.Start);

    // Annotate the current token as a typename or template-id, depending
    // on what follows.
    if (AnnotateTemplateIdToken(Template, Kind, SS, SourceLocation(),
                                TemplateName, /*NameSpliced=*/true,
                                /*AllowTypeAnnotation=*/true))
      return true;

    return false;
  };

  // For typename-specifiers and splices preceded by constructs, parse the
  // trailing template-argument list (if present) and annotate the construct
  // as a template-id.
  if (IsTypename && Tok.is(tok::less)) {
    if (AnnotateTemplateId())
      return true;
  }
  else if (TemplateKeywordLoc.isValid()) {
    if (Tok.isNot(tok::less)) {
      Diag(Splice.End, diag::err_expected_after) << ":]" << "<";
      return true;
    }
    if (AnnotateTemplateId())
      return true;
  }

  return false;
}

void Parser::AnnotateExistingReflectionSplice(ParsedSplice &Splice) {
  // The Token may have been previously annotated as a type or template-id.
  //
  // TODO: The annotation should contain the splice. It would be nice to
  // check that, but we may not keep enough information around to do so.
  if (Tok.isAnnotation())
    return;

  // Make sure we have a token we can turn into an annotation token.
  if (PP.isBacktrackEnabled())
    PP.RevertCachedTokens(1);
  else
    PP.EnterToken(Tok, /*IsReinject*/true);

  // Annotate the token with the spliced expression.
  //
  // FIXME: See TemplateIdAnnotation for an example of how to create something
  // more complex than just an expression. That would prevent multiple
  // evaluations of the same construct.
  assert(Splice.Refl.get() && "Missing expr");
  Tok.setKind(tok::annot_reflection_splice);
  setExprAnnotation(Tok, Splice.Refl);
  Tok.setAnnotationEndLoc(Splice.End);
  Tok.setLocation(Splice.Start);
  PP.AnnotateCachedTokens(Tok);
}

/// Parse a type pack splice
///
/// \verbatim
///   type-pack-splice:
///     '...' '[' '<' reflection '>' ']'
/// \endverbatim
///
/// The constant expression must be a reflection of a type.
SourceLocation Parser::ParseTypePackSplice(DeclSpec &DS) {
  assert(isCXXPackSpliceBegin() && "Not a type pack splice");

  SourceLocation StartLoc = ConsumeToken();
  SourceLocation SBELoc;
  if (parseCXXSpliceBegin(tok::colon, SBELoc))
    return SourceLocation();

  ExprResult Result = ParseConstantExpression();
  if (Result.isInvalid()) {
    DS.SetTypeSpecError();
    return SourceLocation();
  }

  SourceLocation EndLoc;
  if (parseCXXSpliceEnd(tok::colon, EndLoc))
    return SourceLocation();

  const char *PrevSpec = nullptr;
  unsigned DiagID;
  const PrintingPolicy &Policy = Actions.getASTContext().getPrintingPolicy();

  if (DS.SetTypeSpecType(DeclSpec::TST_type_pack_splice, StartLoc, PrevSpec,
                         DiagID, Result.get(), Policy)) {
    Diag(StartLoc, DiagID) << PrevSpec;
    DS.SetTypeSpecError();
  }
  return EndLoc;
}

// This is a custom method for storing the type pack splice tokens as
// the existing methods for doing so (e.g. ConsumeAndStoreUntil) don't
// work with our use of introductory and ending token sequences
// (i.e. '[' '<' and '>' ']').
//
// As a reminder, these cannot be combined by the lexer per cases like:
//   b[x<a>]
bool Parser::ConsumeAndStoreTypePackSplice(CachedTokens &Toks) {
  assert(isCXXPackSpliceBegin() && "Not a type pack splice");

  // Store the one off introductory '...'
  Toks.push_back(Tok);
  ConsumeToken();

  unsigned OpenTokenCount = 0;
  while (true) {
    if (matchCXXSpliceBegin(tok::colon)) {
      // Store the possibly nested introductory tokens

      // [
      Toks.push_back(Tok);
      ConsumeBracket();

      // <
      Toks.push_back(Tok);
      ConsumeToken();

      ++OpenTokenCount;

      continue;
    }

    if (matchCXXSpliceEnd(tok::colon)) {
      // Store the possibly nested ending tokens

      // >
      Toks.push_back(Tok);
      ConsumeToken();

      // ]
      Toks.push_back(Tok);
      ConsumeBracket();

      // If we've hit the matching end of this splice,
      // finish storing tokens
      if (--OpenTokenCount == 0)
        break;

      continue;
    }

    switch (Tok.getKind()) {
    case tok::eof:
    case tok::annot_module_begin:
    case tok::annot_module_end:
    case tok::annot_module_include:
      // Ran out of tokens.
      return false;
    default:
      Toks.push_back(Tok);
      ConsumeAnyToken();
      break;
    }
  }

  return true;
}

/// Parse a mystery splice.
///
/// \verbatim
///   template-argument:
///     '[' ':' reflection ':' ']'
/// \endverbatim
bool Parser::ParseCXXTemplateArgumentMysterySplice(
                                            ParsedTemplateArgument &ParsedArg) {
  ParsedSplice PS;
  if (ParseReflectionSplice(PS))
    return true;

  if (!Tok.isOneOf(tok::ellipsis, tok::comma, tok::greater, tok::greatergreater,
                   tok::greatergreatergreater)) {
    // The next token does not end this splice, this is something else
    AnnotateExistingReflectionSplice(PS);
    return false;
  }

  ParsedArg = ParsedTemplateArgument(PS.Refl.get(), PS.Start,
                                     /*EllipsisLoc=*/SourceLocation());

  return false;
}

/// Parse a template argument pack splice.
///
/// \verbatim
///   type-pack-splice:
///     '...' '[' '<' reflection '>' ']'
/// \endverbatim
ParsedTemplateArgument Parser::ParseCXXTemplateArgumentPackSplice() {
  assert(isCXXPackSpliceBegin() && "Not a pack splice");

  SourceLocation StartLoc = ConsumeToken();
  SourceLocation SBELoc;
  if (parseCXXSpliceBegin(tok::colon, SBELoc))
    return ParsedTemplateArgument();

  ExprResult Result = ParseConstantExpression();
  if (Result.isInvalid())
    return ParsedTemplateArgument();

  SourceLocation EndLoc;
  if (parseCXXSpliceEnd(tok::colon, EndLoc))
    return ParsedTemplateArgument();

  // We could just handle the ellipsis here, but to integrate better
  // with existing control flow, allow ActOnPackExpansion to rebuild
  // the ParsedTemplateArgument with the ellipsis loc.
  if (!Tok.isOneOf(tok::ellipsis, tok::comma, tok::greater, tok::greatergreater,
                   tok::greatergreatergreater)) {
    // The next token does not end this pack splice
    return ParsedTemplateArgument();
  }

  return ParsedTemplateArgument(Result.get(), StartLoc,
                                /*EllipsisLoc=*/SourceLocation());
}

/// Parse a concatenation expression.
///
///   primary-expression:
///      '__concatenate' '(' constant-argument-list ')'
///
/// Each argument in the constant-argument-list must be a constant expression.
///
/// Returns true if parsing or semantic analysis fail.
ExprResult Parser::ParseCXXConcatenateExpression() {
  assert(Tok.is(tok::kw___concatenate));
  SourceLocation KeyLoc = ConsumeToken();

  BalancedDelimiterTracker Parens(*this, tok::l_paren);
  if (Parens.expectAndConsume())
    return ExprError();

  SmallVector<Expr *, 4> Parts;
  do {
    ExprResult Expr = ParseConditionalExpression();
    if (Expr.isInvalid()) {
      Parens.skipToEnd();
      return ExprError();
    }
    Parts.push_back(Expr.get());
  } while (TryConsumeToken(tok::comma));

  if (Parens.consumeClose())
    return ExprError();

  return Actions.ActOnCXXConcatenateExpr(Parts, KeyLoc,
                                         Parens.getOpenLocation(),
                                         Parens.getCloseLocation());
}
