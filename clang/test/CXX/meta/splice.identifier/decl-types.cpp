// RUN: %clang_cc1 -std=c++2a -freflection -verify %s
// expected-no-diagnostics

namespace enums {

enum [# "enum_t" #] { };

enum class [# "enum_class_t" #];
enum class [# "enum_class_t" #] { };

void test() {
  enum_t et;
  enum_class_t ect;
}

} // end namespace enums

namespace classes {

struct [# "struct_t" #];
struct [# "struct_t" #] { };

class [# "class_t" #];
class [# "class_t" #] { };

void test() {
  struct_t st;
  class_t ct;
}

} // end namespace classes

namespace namespaces {

namespace [# "namespace_ns" #] {
  int x;
}

namespace [# "namespace_ns" #]::[# "nested" #] {
  int x;
}

void test() {
  namespace_ns::x += 1;
  namespace_ns::nested::x += 1;
}

} // end namespace namespaces
