#include "space/space.h"

#include <iostream>
#include <type_traits>

using namespace my_log;

static int g_failed = 0;
static void CHECK(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "  [FAIL] " << what << "\n";
        ++g_failed;
    }
}

// case 1: construction & count
void TestConstruct() {
    bytes b(42);
    CHECK(b.count() == 42, "bytes(42).count() should be 42");

    kilobytes k(3);
    CHECK(k.count() == 3, "kilobytes(3).count() should be 3");

    bytes empty;
    CHECK(empty.count() == 0, "default constructed space should be 0");

    std::cout << "case 1: construct & count\n";
}

// case 2: type identity (compile-time)
void TestTypeTraits() {
    static_assert(std::is_same_v<kilobytes::period, kilo>, "kilobytes::period should be kilo");
    static_assert(std::is_same_v<megabytes::period, mega>, "megabytes::period should be mega");
    static_assert(std::is_same_v<bytes::rep, size_t>, "bytes::rep should be size_t");
    static_assert(!std::is_same_v<bytes, kilobytes>, "bytes and kilobytes must be different types");
    std::cout << "case 2: type traits (static_assert)\n";
}

// case 3: small -> large scale conversion (KB -> B)
void TestConvertDown() {
    bytes b = kilobytes(1);
    CHECK(b.count() == 1024, "kilobytes(1) -> bytes should be 1024");

    bytes b2 = megabytes(2);
    CHECK(b2.count() == 2u * 1024 * 1024, "megabytes(2) -> bytes should be 2097152");

    std::cout << "case 3: KB/MB -> bytes\n";
}

// case 4: large -> small scale conversion (B -> KB), incl. truncation
void TestConvertUp() {
    kilobytes k = bytes(2048);
    CHECK(k.count() == 2, "bytes(2048) -> kilobytes should be 2");

    megabytes m = kilobytes(3072);
    CHECK(m.count() == 3, "kilobytes(3072) -> megabytes should be 3");

    // integer division truncates: 1025 KB = 1 MB (remainder dropped)
    megabytes m2 = kilobytes(1025);
    CHECK(m2.count() == 1, "kilobytes(1025) -> megabytes should truncate to 1");

    std::cout << "case 4: bytes -> KB/MB (truncation)\n";
}

// case 5: unary operators
void TestUnary() {
    kilobytes k(5);
    CHECK((+k).count() == 5, "unary + should keep value");
    CHECK((-k).count() == static_cast<size_t>(-5), "unary - should negate");

    kilobytes a(1);
    kilobytes old = a++;
    CHECK(old.count() == 1 && a.count() == 2, "postfix ++ should return old value");

    kilobytes b(1);
    kilobytes& ref = ++b;
    CHECK(ref.count() == 2 && b.count() == 2, "prefix ++ should return new value");

    kilobytes c(3);
    --c;
    CHECK(c.count() == 2, "prefix -- should decrement");
    c--;
    CHECK(c.count() == 1, "postfix -- should decrement");

    std::cout << "case 5: unary +/-/++/--\n";
}

// case 6: compound assignment with same unit
void TestCompoundSpace() {
    kilobytes a(10);
    a += kilobytes(5);
    CHECK(a.count() == 15, "10KB += 5KB should be 15KB");

    a -= kilobytes(3);
    CHECK(a.count() == 12, "15KB -= 3KB should be 12KB");

    std::cout << "case 6: += / -=\n";
}

// case 7: scalar multiply / divide / modulo (space op number)
void TestCompoundScalar() {
    kilobytes k(4);
    k *= 3;
    CHECK(k.count() == 12, "4KB *= 3 should be 12KB");

    k /= 2;
    CHECK(k.count() == 6, "12KB /= 2 should be 6KB");

    k %= 4;
    CHECK(k.count() == 2, "6KB %= 4 should be 2KB");

    std::cout << "case 7: *= /= %= with scalar\n";
}

// case 8: binary + / - return new object, do not touch operands
void TestBinary() {
    kilobytes a(1);
    kilobytes b(2);
    auto c = a + b;
    CHECK(c.count() == 3, "1KB + 2KB should be 3KB");
    CHECK(a.count() == 1 && b.count() == 2, "operator+ must not modify operands");

    auto d = b - a;
    CHECK(d.count() == 1, "2KB - 1KB should be 1KB");

    std::cout << "case 8: binary + / -\n";
}

// case 9: mixed-unit assignment through conversion constructor
void TestMixedAssign() {
    // implicit conversion between typed units is allowed (safe, compiler does the math)
    bytes buffer = kilobytes(4);          // 4KB buffer, counted in bytes
    CHECK(buffer.count() == 4096, "buffer should hold 4096 bytes");

    kilobytes used = bytes(1024);
    CHECK(used.count() == 1, "1024 bytes used should be 1 KB");

    std::cout << "case 9: mixed-unit assign\n";
}

int main() {
    TestConstruct();
    TestTypeTraits();
    TestConvertDown();
    TestConvertUp();
    TestUnary();
    TestCompoundSpace();
    TestCompoundScalar();
    TestBinary();
    TestMixedAssign();

    std::cout << (g_failed == 0 ? "ALL PASSED\n" : "SOME CASES FAILED\n");
    return g_failed == 0 ? 0 : 1;
}
