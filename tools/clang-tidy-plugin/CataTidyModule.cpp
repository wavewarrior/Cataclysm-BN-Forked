#include "DeterminismCheck.h"
#include "NoLongCheck.h"
#include "TestFilenameCheck.h"

#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>
#include <clang/Basic/Version.h>
#include <llvm/ADT/StringRef.h>

namespace clang {
namespace tidy {
namespace cata {

class CataModule: public ClangTidyModule {
public:
    void addCheckFactories(ClangTidyCheckFactories& CheckFactories) override {
        // Sanity check the clang version to verify that we're loaded into
        // the same version we linked against

        std::string RuntimeVersion = getClangFullVersion();
        if (!StringRef(RuntimeVersion).contains("clang version " CLANG_VERSION_STRING)) {
            llvm::report_fatal_error(Twine(
                "clang version mismatch in CataTidyModule.  Compiled against " CLANG_VERSION_STRING
                " but loaded by ",
                RuntimeVersion));
            abort();
        }
        CheckFactories.registerCheck<DeterminismCheck>("cata-determinism");
        CheckFactories.registerCheck<NoLongCheck>("cata-no-long");
        CheckFactories.registerCheck<TestFilenameCheck>("cata-test-filename");
    }
};

} // namespace cata

// Register the CataModule using this statically initialized variable.
static ClangTidyModuleRegistry::Add<cata::CataModule> X(
    "cata-module", "Adds Cataclysm: Bright Nights checks.");

} // namespace tidy
} // namespace clang
