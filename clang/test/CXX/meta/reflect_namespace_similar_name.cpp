// RUN: %clang_cc1 -std=c++1z -freflection %s

namespace parent_ns {
  class parent_ns_foo {
    int var = 3;
  };
}

int main() {
  constexpr auto reflection = ^parent_ns::parent_ns_foo;
  return 0;
}
