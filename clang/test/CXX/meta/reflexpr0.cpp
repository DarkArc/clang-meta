// RUN: %clang_cc1 -std=c++1z -freflection %s

#include "reflection_query.h"

struct assertion { };

#define assert(E) if (!(E)) __builtin_abort();

struct S {
  enum E { X, Y };
  enum class EC { X, Y };

  void function() { }
  constexpr void constexpr_function() { }

  int variable;
  static const int static_variable = 0;
  static constexpr int constexpr_variable = 0;
};

enum E { A, B, C };
enum class EC { A, B, C };

void f() { }

int global;

void ovl();
void ovl(int);

template<typename T>
void fn_tmpl() { }

template<typename T>
struct ClassTemplate {
  T function() { return T(); }
  constexpr T constexpr_function() { return T(); }

  T variable;
  static const T static_variable = T();
  static constexpr T constexpr_variable = T();
};

namespace N {
  namespace M {
    enum E { A, B, C };
    enum class EC { A, B, C };
  }
}

constexpr int test() {
  constexpr auto r0 = ^::;
  static_assert(__reflect(query_is_namespace, r0));

  constexpr auto r1 = ^S;
  static_assert(__reflect(query_is_type, __reflect(query_get_type, r1)));
  static_assert(__reflect(query_is_class, r1));
  static_assert(!__reflect(query_is_namespace, r1));

  constexpr auto r2 = ^E;
  static_assert(__reflect(query_is_type, r2));
  static_assert(__reflect(query_is_unscoped_enum, r2));

  constexpr auto r3 = ^A;
  static_assert(__reflect(query_is_expression, r3));
  static_assert(__reflect(query_is_enumerator, r3));

  constexpr auto r4 = ^EC;
  static_assert(__reflect(query_is_type, r4));
  static_assert(__reflect(query_is_unscoped_enum_type, r4));
  static_assert(__reflect(query_is_scoped_enum_type, r4));

  constexpr auto r5 = ^EC::A;
  static_assert(__reflect(query_is_expression, r5));
  static_assert(__reflect(query_is_enumerator, r5));

  constexpr auto r6 = ^f;
  static_assert(__reflect(query_is_expression, r6));
  static_assert(__reflect(query_is_function, r6));

  constexpr auto r7 = ^global;

  constexpr auto r8 = ^ovl;

  constexpr auto r9 = ^fn_tmpl;

  constexpr auto r10 = ^ClassTemplate;

  constexpr auto r11 = ^ClassTemplate<int>;

  constexpr auto r12 = ^ClassTemplate<int>::function;

  constexpr auto r13 = ^ClassTemplate<int>::constexpr_function;

  constexpr auto r14 = ^ClassTemplate<int>::variable;

  constexpr auto r15 = ^ClassTemplate<int>::constexpr_variable;

  constexpr auto r16 = ^ClassTemplate<int>::static_variable;

  constexpr auto r17 = ^N;

  constexpr auto r18 = ^N::M;

  constexpr auto r19 = ^S::E;
  // FIXME: This is awkward.
  static_assert(__reflect(query_get_type, __reflect(query_get_parent, r19)) == ^S);

  constexpr auto r20 = ^S::X;
  (void)__reflect(query_get_type, r14);

  constexpr auto r21 = ^S::EC;

  constexpr auto r22 = ^S::EC::X;

  constexpr auto r23 = ^S::function;

  constexpr auto r24 = ^S::constexpr_function;

  constexpr auto r25 = ^S::variable;

  constexpr auto r26 = ^S::constexpr_variable;

  constexpr auto r27 = ^ClassTemplate<int>::static_variable;

  constexpr auto r28 = ^S::E;

  constexpr auto r29 = ^S::X;

  constexpr auto r30 = ^S::EC;

  constexpr auto r31 = ^S::EC::X;

  constexpr auto r32 = ^N::M::E;

  constexpr auto r33 = ^N::M::A;

  constexpr auto r34 = ^N::M::EC;

  constexpr auto r35 = ^N::M::EC::A;

  return 0;
}

template<typename T>
constexpr int type_template() {
  auto r = ^T;
  return 0;
}

template<int N>
constexpr int nontype_template() {
  auto r = ^N;
  return 0;
}

template<template<typename> class X>
constexpr int template_template() {
  auto r = ^X;
  return 0;
}

template<typename T>
struct temp { };

int main() {
  constexpr int n = test();

  constexpr int t = type_template<int>();
  constexpr int v = nontype_template<0>();
  constexpr int x = template_template<temp>();
}
