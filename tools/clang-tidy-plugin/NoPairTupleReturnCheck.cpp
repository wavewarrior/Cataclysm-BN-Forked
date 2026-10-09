#include "NoPairTupleReturnCheck.h"

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <clang/AST/Decl.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Basic/SourceLocation.h>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace cata {

void NoPairTupleReturnCheck::registerMatchers(MatchFinder* Finder) {
    // Look through aliases and deduced types to the record: `using P = std::pair<int, int>;`
    // and `auto f() { return std::make_pair( 1, 2 ); }` return a pair as much as the spelled form.
    // A dependent `std::pair<T, T>` is not a record type yet, so match its template name too.
    const auto IsPairOrTuple = hasAnyName("::std::pair", "::std::tuple");
    const auto PairOrTuple = anyOf(
        hasUnqualifiedDesugaredType(
            recordType(hasDeclaration(classTemplateSpecializationDecl(IsPairOrTuple)))),
        hasUnqualifiedDesugaredType(
            templateSpecializationType(hasDeclaration(namedDecl(IsPairOrTuple)))));
    Finder->addMatcher(
        functionDecl(returns(PairOrTuple), unless(isImplicit()),
                     unless(ast_matchers::isTemplateInstantiation()), unless(isDeleted()))
            .bind("fn"),
        this);
}

void NoPairTupleReturnCheck::check(const MatchFinder::MatchResult& Result) {
    const FunctionDecl* Fn = Result.Nodes.getNodeAs<FunctionDecl>("fn");
    if (!Fn || !Fn->getLocation().isValid()) { return; }
    // Declarations spelled by a macro expansion are not the author's to restructure.
    if (Fn->getLocation().isMacroID()) { return; }
    diag(Fn->getLocation(), "return a named struct instead of std::pair/std::tuple");
}

} // namespace cata
} // namespace tidy
} // namespace clang
