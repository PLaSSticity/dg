#ifndef DG_LLVM_SYSTEM_DEPENDNECE_GRAPH_H_
#define DG_LLVM_SYSTEM_DEPENDNECE_GRAPH_H_

#include <map>

#include "dg/SystemDependenceGraph/SystemDependenceGraph.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/llvm/LLVMAnalysisOptions.h"

namespace llvm {
    class Module;
    class Value;
}

namespace dg {
namespace llvmdg {

class SystemDependenceGraphOptions : public LLVMAnalysisOptions {
};

/* FIXME: hide this from the world */
struct SDGBuilder;

class SystemDependenceGraph {
    const SystemDependenceGraphOptions _options;
    llvm::Module *_module;
    sdg::SystemDependenceGraph _sdg;
    LLVMPointerAnalysis *_pta{nullptr};

    //SystemDependenceGraphBuilder _builder;
    // FIXME: do this unordered maps
    std::map<const llvm::Value *, sdg::DGElement *> _mapping;
    std::map<const sdg::DGElement *, llvm::Value *> _rev_mapping;
    // built functions
    std::map<const llvm::Function *, sdg::DependenceGraph *> _fun_mapping;

    void buildSDG();

    void addMapping(llvm::Value *v, sdg::DGElement *n) {
        assert(_mapping.find(v) == _mapping.end() &&
                "Already have this value");
        _mapping[v] = n;
        _rev_mapping[n] = v;
    }

    void addFunMapping(llvm::Function *F, sdg::DependenceGraph *g) {
        assert(_mapping.find(F) == _mapping.end() &&
                "Already have this function");
        _fun_mapping[F] = g;
    }

    friend struct SDGBuilder;

public:
    SystemDependenceGraph(llvm::Module *M,
                          LLVMPointerAnalysis *PTA,
                          const SystemDependenceGraphOptions& opts = {})
    :  _options(opts), _module(M), _sdg(), _pta{PTA} {
        buildSDG();
    }

    llvm::Module *getModule() { return _module; }
    const llvm::Module *getModule() const { return _module; }

    sdg::DGElement* getNode(const llvm::Value *v) const {
        auto it = _mapping.find(v);
        return it == _mapping.end() ? nullptr : it->second;
    }

    llvm::Value* getValue(const sdg::DGElement *n) const {
        auto it = _rev_mapping.find(n);
        return it == _rev_mapping.end() ? nullptr : it->second;
    }

    sdg::DependenceGraph* getDG(const llvm::Function *F) const {
        auto it = _fun_mapping.find(F);
        return it == _fun_mapping.end() ? nullptr : it->second;
    }

    sdg::SystemDependenceGraph& getSDG() { return _sdg; }
    const sdg::SystemDependenceGraph& getSDG() const { return _sdg; }
};

} // namespace llvmdg
} // namespace dg

#endif // DG_LLVM_SYSTEM_DEPENDNECE_GRAPH_H_
