set(LLVM_LINK_COMPONENTS
  FrontendOpenMP
  MC
  MCParser
  Support
  )

add_clang_library(clangParse
  ParseAST.cpp
  ParseCXXInlineMethods.cpp
  ParseDecl.cpp
  ParseDeclCXX.cpp
  ParseExpr.cpp
  ParseExprCXX.cpp
  ParseInit.cpp
  ParseObjc.cpp
  ParseOpenMP.cpp
  ParsePragma.cpp
  ParseReflect.cpp
  ParseStmt.cpp
  ParseStmtAsm.cpp
  ParseTemplate.cpp
  ParseTentative.cpp
  Parser.cpp

  LINK_LIBS
  clangAST
  clangBasic
  clangLex
  clangSema

  DEPENDS
  omp_gen
  )
