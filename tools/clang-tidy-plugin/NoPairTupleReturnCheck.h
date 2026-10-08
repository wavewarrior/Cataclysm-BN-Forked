#ifndef CATA_TOOLS_CLANG_TIDY_PLUGIN_NOPAIRTUPLERETURNCHECK_H
#define CATA_TOOLS_CLANG_TIDY_PLUGIN_NOPAIRTUPLERETURNCHECK_H

#include <clang-tidy/ClangTidyCheck.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <llvm/ADT/StringRef.h>

namespace clang {
namespace tidy {
class ClangTidyContext;

namespace cata {

/// Flags functions that return std::pair or std::tuple by value: multiple return values belong in
/// a named struct (AGENTS.md "MUST NOT use std::pair/std::tuple for multiple return values").
class NoPairTupleReturnCheck: public ClangTidyCheck {
public:
    NoPairTupleReturnCheck(StringRef Name, ClangTidyContext* Context): ClangTidyCheck(Name,
                Context) {}
    void registerMatchers(ast_matchers::MatchFinder* Finder) override;
    void check(const ast_matchers::MatchFinder::MatchResult& Result) override;
};

} // namespace cata
} // namespace tidy
} // namespace clang

#endif // CATA_TOOLS_CLANG_TIDY_PLUGIN_NOPAIRTUPLERETURNCHECK_H
