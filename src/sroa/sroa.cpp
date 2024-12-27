#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"

using namespace llvm;

namespace {
class SROA : public PassInfoMixin<SROA> {
  const bool RequiresDomTree;

public:
  explicit SROA(bool RequiresDomTree = true)
      : RequiresDomTree(RequiresDomTree) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    auto *DT =
        RequiresDomTree ? &AM.getResult<DominatorTreeAnalysis>(F) : nullptr;
    if (!runOnFunction(F, DT))
      return PreservedAnalyses::all();

    PreservedAnalyses PA;
    PA.preserveSet<CFGAnalyses>();
    return PA;
  }

private:
  bool runOnFunction(Function &F, DominatorTree *DT) {
    bool Changed = false;
    // Phase 1: Promote allocas used only as single values
    Changed |= promoteAllocas(F, DT);

    return Changed;
  }

  bool promoteAllocas(Function &F, DominatorTree *DT) {
    std::vector<AllocaInst *> Allocas;
    BasicBlock &BB = F.getEntryBlock();

    bool Changed = false;
    while (1) {
      Allocas.clear();

      // Find allocas that are safe to promote, by looking at all instructions
      for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I) {
        if (AllocaInst *AI = dyn_cast<AllocaInst>(I)) {
          if (isAllocaPromotable(AI))
            Allocas.push_back(AI);
        }
      }
      if (Allocas.empty())
        break;

      // Promote the allocas
      Changed |= !Allocas.empty();

      SmallVector<AllocaInst *, 32> Worklist(Allocas.begin(), Allocas.end());
      PromoteMemToReg(Worklist, *DT);
    }

    return Changed;
  }

  bool isAllocaPromotable(AllocaInst *AI) {
    // Only handle first-class values
    if (!AI->getAllocatedType()->isSized())
      return false;

    // No dynamic allocations
    if (!AI->isStaticAlloca())
      return false;

    // Check for instructions that make promotion unsafe
    for (User *U : AI->users()) {
      if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
        if (LI->isVolatile())
          return false;
      } else if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
        if (SI->isVolatile())
          return false;
      } else if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(U)) {
        // Only allow simple GEPs
        if (!GEP->hasAllConstantIndices())
          return false;
      } else {
        // Unknown instruction
        return false;
      }
    }

    return true;
  }
};
} // namespace

// New Pass Manager registration
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "SROA", "v0.1", [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "mysroa") {
                    FPM.addPass(SROA());
                    return true;
                  }
                  return false;
                });
          }};
}
