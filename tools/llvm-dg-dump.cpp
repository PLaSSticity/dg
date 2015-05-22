#include <assert.h>
#include <cstdio>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IRReader/IRReader.h>

#include <iostream>
#include <string>
#include <queue>
#include <set>

#include "../src/llvm/LLVMDependenceGraph.h"
#include "../src/analysis/DFS.h"

using namespace dg;
using llvm::errs;

static struct {
    bool printCFG;
    bool printRevCFG;
    bool printBB;
    bool printBBonly;
    bool printPostDom;
    bool printControlDep;
    bool printDataDep;
} OPTIONS;

std::queue<LLVMDependenceGraph *> toPrint;

static std::string& getValueName(const llvm::Value *val, std::string &str)
{
    llvm::raw_string_ostream s(str);

    str.clear();

    if (!val) {
        s << "[NULL]";
        return s.str();
    }

    if (llvm::isa<llvm::Function>(val))
        s << "ENTRY " << val->getName();
    else
        s << *val;

    return s.str();
}

static void dump_to_dot(LLVMNode *n, std::ostream& out)
{
    const llvm::Value *val;

    if (OPTIONS.printControlDep)
        for (auto I = n->control_begin(), E = n->control_end();
             I != E; ++I)
            out << "\tNODE" << n << " -> NODE" <<  *I << "[color=green]\n";

    if (OPTIONS.printDataDep)
        for (auto I = n->data_begin(), E = n->data_end();
             I != E; ++I)
            out << "\tNODE" << n << " -> NODE" <<  *I
                << " [color=red]\n";

    if (OPTIONS.printCFG && !OPTIONS.printBBonly) {
        if (n->hasSuccessor()) {
            LLVMNode *succ = n->getSuccessor();
            if (!succ)
                errs() << "n hasSuccessor NULL!\n";

            out << "\tNODE" << n << " -> NODE" << succ
                << " [style=dotted rank=source]\n";
        }
    }

    if (OPTIONS.printRevCFG && !OPTIONS.printBBonly) {
        if (n->hasPredcessor()) {
            out << "\tNODE" << n << " -> NODE" <<  n->getPredcessor()
                << " [style=dotted color=gray]\n";
        }
    }

    // print edges between blocks
    if (OPTIONS.printCFG || OPTIONS.printBB) {
        auto BB = n->getBasicBlock();
        if (BB && BB->getLastNode() == n) {
            for (auto pred : BB->successors()) {
                auto fn = pred->getFirstNode();
                out << "\tNODE" << n << " -> NODE" << fn
                    << " [color=black]\n";
            }
        }
    }

    /*
    if (OPTIONS.printRevCFG || OPTIONS.printBB) {
        auto BB = n->getBasicBlock();
        // if this is BB header, print the edges
        if (BB && BB->getFirstNode() == n) {
            auto fn = BB->getFirstNode();
            for (auto pred : BB->predcessors()) {
                auto ln = pred->getLastNode();
                out << "\tNODE" << fn << " -> NODE" << ln
                    << " [style=dotted color=purple]\n";
            }
        }
    }
    */

    if (OPTIONS.printPostDom) {
        auto BB = n->getBasicBlock();
        if (BB && BB->getFirstNode() == n
            && BB->getIPostDomBy()) {
            auto pdBB = BB->getIPostDomBy();
            if (pdBB) {
                out << "\tNODE" << pdBB->getLastNode()
                    << " -> NODE" << n
                    << " [style=dashed color=purple rank=source]\n";
            } else {
                errs() << "WARN: No post-dom by for basic block for "
                       << BB << "\n";
            }
        }
    }
}

static void print_to_dot(LLVMDependenceGraph *dg,
                         std::ostream& out = std::cout);

static void printNode(LLVMNode *n,
                      std::ostream& out = std::cout)
{
    std::string valName;
    LLVMBBlock *BB = n->getBasicBlock();
    const llvm::Value *val = n->getValue();
    bool isBBHead = BB && n == BB->getFirstNode();
    bool isBBTail = BB && n == BB->getLastNode();

    assert(n);

    for (auto sub : n->getSubgraphs())
        toPrint.push(sub);

    LLVMDependenceGraph *params = n->getParameters();
    if (params) {
        toPrint.push(params);
    }

    // do not print node if we'd like to print only bblocks
    if (!(isBBHead || isBBTail) && OPTIONS.printBBonly)
        return;

    getValueName(val, valName);
    out << "\tNODE" << n << " [";

    if (isBBHead) {
        out << "style=\"filled\" fillcolor=\"lightgray\"";
    }

    out << " label=\"" << valName;

    if (OPTIONS.printBB && BB) {
        auto fn = BB->getFirstNode();
        if (!fn) {
            errs() << "CRITICAL: No first value for "
                   << valName << "\n";
        } else {
            auto fval = fn->getValue();
            if (!fval) {
                errs() << "WARN: No value in first value for "
                       << valName << "\n";
            } else {
                getValueName(fval, valName);
                out << "\\nBB: " << valName;
                if (n == fn)
                    out << "\\nBB head (preds "
                        << BB->predcessorsNum() << ")";
                if (BB->getLastNode() == n)
                    out << "\\nBB tail (succs "
                        << BB->successorsNum() << ")";
            }
        }
    }

    /*
    for (auto d : n->getDefs()) {
        getValueName(d->getValue(), valName);
        out << "\\nDEF " << valName;
    }
    for (auto d : n->getPtrs()) {
        getValueName(d->getValue(), valName);
        out << "\\nPTR " << valName;
    }
    */

    out << "\"];\n";
}

struct data
{
    data(LLVMDependenceGraph *d, std::ostream& o = std::cout)
        : dg(d), out(o) {}

    LLVMDependenceGraph *dg;
    std::ostream& out;
};

static void printBB(LLVMBBlock *BB, struct data& data)
{
    assert(BB);
    LLVMDependenceGraph *dg = data.dg;
    std::ostream& out = data.out;

    LLVMNode *n = BB->getFirstNode();
    static unsigned int bbid = 0;

    if (!n) {
        std::string valName;
        getValueName(dg->getEntry()->getValue(), valName);

        errs() << "WARN no first value in a block for "
               << valName << "\n";
        return;
    }

    out << "subgraph cluster_bb_" << bbid++;
    out << "{\n";
    out << "\tstyle=dotted\n";
    out << "label=\"LLVMBBlock " << BB;

    if (BB == dg->getEntryBB())
        out << " |> ENTRY <|";
    if (BB == dg->getExitBB())
        out << " |> EXIT <|";

    if (OPTIONS.printPostDom) {
        out << "\\n";
        for (LLVMBBlock *b : BB->getPostdominates())
            out << "\\nPDOM " << b;
    }

    out << "\"\n";

    do {
        printNode(n, out);
        n = n->getSuccessor();
    } while (n);

    out << "}\n";
}

static void printNodesOnly(LLVMDependenceGraph *dg,
                           std::ostream& out)
{
    for (auto I = dg->begin(), E = dg->end(); I != E; ++I) {
        printNode(I->second, out);
    }
}

static void dg_print_edges(LLVMDependenceGraph *dg,
                           std::ostream& out)
{
    for (auto I = dg->begin(), E = dg->end(); I != E; ++I) {
        auto n = I->second;
        if (!n) {
                errs() << "WARN [" << dg
                       << "]: Node is NULL for value: "
                       << *I->first << "\n";

            continue;
        }

        dump_to_dot(I->second, out);
    }

    // if this is a params dg, print edge from callsite to
    // this dg
        // add control edge from call-site to the parameters subgraph
        //out << "\tNODE" << n << " -> NODE" <<  params->getEntry()
        //    << "[label=\"params\"]\n";
        //

    // if this is a function, print edges here from callsites
   //for (auto sub : n->getSubgraphs()) {
   //    out << "\tNODE" << n << " -> NODE" <<  sub->getEntry()
   //        << " [style=dashed label=\"call\"]\n";
   //}

}

static void print_to_dot(LLVMDependenceGraph *dg,
                         std::ostream& out)
{
    static unsigned subgraph_no = 0;
    const llvm::Value *val;
    std::string valName;

    if (!dg)
        return;

    out << "subgraph \"cluster_" << subgraph_no++;
    out << "\" {\n";

    getValueName(dg->getEntry()->getValue(), valName);
    out << "label=\"<" << valName << ">";
    out << "\\n Nodes: " << dg->size() << "\"";

    out << " style=\"solid\"";

    LLVMBBlock *entryBB = dg->getEntryBB();
    if (!entryBB) {
        errs() << "No entry block in graph for " << valName << "\n";
        errs() << "  ^^^ printing only nodes\n";

        printNodesOnly(dg, out);
        out << "}\n";
        return;
    }

    printNodesOnly(dg, out);

    data d(dg);
    analysis::BBlockDFS<LLVMNode *> dfs;

    // run dfs and print every basic block you find
    dfs(entryBB, printBB, d);

    // print edges in dg
    dg_print_edges(dg, out);

    out << "}\n";
}

int main(int argc, char *argv[])
{
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    std::unique_ptr<llvm::Module> M;
    const char *module = NULL;

    // default
    OPTIONS.printControlDep = true;
    OPTIONS.printDataDep = true;
    OPTIONS.printCFG = true;

    // parse options
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-no-control") == 0) {
            OPTIONS.printControlDep = false;
        } else if (strcmp(argv[i], "-no-data") == 0) {
            OPTIONS.printDataDep = false;
        } else if (strcmp(argv[i], "-bb") == 0) {
            OPTIONS.printBB = true;
        } else if (strcmp(argv[i], "-bbonly") == 0) {
            OPTIONS.printBB = true;
            OPTIONS.printBBonly = true;
            OPTIONS.printDataDep = false;
        } else if (strcmp(argv[i], "-cfg") == 0) {
            OPTIONS.printCFG = true;
        } else if (strcmp(argv[i], "-cfgall") == 0) {
            OPTIONS.printCFG = true;
            OPTIONS.printRevCFG = true;
        } else if (strcmp(argv[i], "-pd") == 0) {
            OPTIONS.printPostDom = true;
        } else {
            module = argv[i];
        }
    }

    if (!module) {
        errs() << "Usage: % IR_module [output_file]\n";
        return 1;
    }

    M = llvm::parseIRFile(module, SMD, context);
    if (!M) {
        SMD.print(argv[0], errs());
        return 1;
    }

    LLVMDependenceGraph d;
    d.build(&*M);
    d.computePDTree();

    std::ostream& out = std::cout;
    std::set<LLVMDependenceGraph *> printed;

    out << "digraph \"DependencyGraph\" {\n";

    // print main procedure
    toPrint.push(&d);

    // print subgraphs that should be printed
    while (!toPrint.empty()) {
        auto sg = toPrint.front();
        toPrint.pop();

        if (printed.insert(sg).second)
            print_to_dot(sg, out);
    }

    out << "}\n";

    return 0;
}
