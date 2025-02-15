// RUN: %clang_cc1 -std=c++2a -freflection -verify %s

using info = decltype(^void);

template <info R>
struct inner : public typename [:R:] { // expected-error {{base specifier must name a class}}
};

struct base { };

auto y = inner<^base>();
auto x = inner<^int>(); // expected-note {{in instantiation of template class}}
