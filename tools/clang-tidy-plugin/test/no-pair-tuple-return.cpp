// RUN: %check_clang_tidy %s cata-no-pair-tuple-return %t -- --load=%cata_plugin --

#include <tuple>
#include <utility>

std::pair<int, int> by_value();
// CHECK-MESSAGES: warning: return a named struct instead of std::pair/std::tuple [cata-no-pair-tuple-return]

auto trailing() -> std::tuple<int, int, int>;
// CHECK-MESSAGES: warning: return a named struct instead of std::pair/std::tuple [cata-no-pair-tuple-return]

using alias_pair = std::pair<int, int>;
alias_pair through_alias();
// CHECK-MESSAGES: warning: return a named struct instead of std::pair/std::tuple [cata-no-pair-tuple-return]

const std::pair<int, int> const_value();
// CHECK-MESSAGES: warning: return a named struct instead of std::pair/std::tuple [cata-no-pair-tuple-return]

auto deduced() {
    // CHECK-MESSAGES: warning: return a named struct instead of std::pair/std::tuple [cata-no-pair-tuple-return]
    return std::make_pair( 1, 2 );
}

struct holder {
    std::pair<int, int> member();
    // CHECK-MESSAGES: warning: return a named struct instead of std::pair/std::tuple [cata-no-pair-tuple-return]
};

// A named struct is what the check asks for.
struct result {
    int a;
    int b;
};
result named();

// Only a by-value return of the pair itself counts.
std::pair<int, int> &by_reference();
const std::pair<int, int> &by_const_reference();
void takes_pair( std::pair<int, int> p );
std::pair<int, int> *by_pointer();

// A function template is reported once for its declaration, not per instantiation.
template<typename T>
std::pair<T, T> templated();
// CHECK-MESSAGES: warning: return a named struct instead of std::pair/std::tuple [cata-no-pair-tuple-return]

void use_templated() {
    templated<int>();
    templated<long>();
}
