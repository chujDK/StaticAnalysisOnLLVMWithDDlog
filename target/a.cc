#include <iostream>

class A {
 public:
  int f;
  A* o;
};

class B : public A {
 public:
  int g;
};

int main() {
  auto b = new B();
  auto a = new A();

  a->o = b;
  b->o = a;

  int i1 = 1;
  int i2 = 2;

  b->g = i1;

  a = new B();

  i1 = i2;
  i2 = 1;
  return 0;
}
