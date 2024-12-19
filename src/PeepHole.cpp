#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;
using namespace llvm::PatternMatch;

namespace {

class PeepholePass : public PassInfoMixin<PeepholePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    bool Changed = false;

    // Iterate over all basic blocks
    for (BasicBlock &BB : F) {
      Changed |= optimizeBasicBlock(BB);
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  bool optimizeBasicBlock(BasicBlock &BB) {
    bool Changed = false;

    // We need to use an iterator that won't be invalidated by deletion
    for (auto I = BB.begin(); I != BB.end();) {
      Instruction &Inst = *I++;

      if (Value *V = optimizeInstruction(&Inst)) {
        // Replace all uses of the original instruction with the optimized value
        Inst.replaceAllUsesWith(V);
        Inst.eraseFromParent();
        Changed = true;
      }
    }

    return Changed;
  }

  Value *optimizeInstruction(Instruction *I) {
    // Skip non-binary operators
    if (!isa<BinaryOperator>(I))
      return nullptr;

    // Try different optimization patterns
    if (Value *V = foldConstantArithmetic(I))
      return V;
    if (Value *V = optimizeIdentityOps(I))
      return V;
    if (Value *V = strengthReduction(I))
      return V;

    return nullptr;
  }

  Value *foldConstantArithmetic(Instruction *I) {
    // Match patterns for constant folding
    Value *Op0 = I->getOperand(0);
    Value *Op1 = I->getOperand(1);

    // Only handle integer arithmetic for now
    if (!I->getType()->isIntegerTy())
      return nullptr;

    ConstantInt *C1, *C2;
    // Check if both operands are constants
    if (match(Op0, m_ConstantInt(C1)) && match(Op1, m_ConstantInt(C2))) {
      const APInt &A = C1->getValue();
      const APInt &B = C2->getValue();

      switch (I->getOpcode()) {
      case Instruction::Add:
        return ConstantInt::get(I->getContext(), A + B);
      case Instruction::Mul:
        return ConstantInt::get(I->getContext(), A * B);
      case Instruction::Sub:
        return ConstantInt::get(I->getContext(), A - B);
      default:
        return nullptr;
      }
    }

    return nullptr;
  }

  Value *optimizeIdentityOps(Instruction *I) {
    Value *Op0 = I->getOperand(0);
    Value *Op1 = I->getOperand(1);

    // Match patterns like: X * 1 = X, X + 0 = X
    switch (I->getOpcode()) {
    case Instruction::Mul: {
      ConstantInt *C;
      // X * 1 = X
      if (match(Op1, m_ConstantInt(C)) && C->isOne())
        return Op0;
      break;
    }
    case Instruction::Add:
    case Instruction::Sub: {
      ConstantInt *C;
      // X + 0 = X, X - 0 = X
      if (match(Op1, m_ConstantInt(C)) && C->isZero())
        return Op0;
      break;
    }
    default:
      break;
    }

    return nullptr;
  }

  Value *strengthReduction(Instruction *I) {
    // Skip non-multiply instructions
    if (I->getOpcode() != Instruction::Mul)
      return nullptr;

    Value *Op0 = I->getOperand(0);
    Value *Op1 = I->getOperand(1);
    ConstantInt *C;

    // Match pattern: X * 2 = X << 1
    if (match(Op1, m_ConstantInt(C)) && C->getValue() == 2) {
      return BinaryOperator::CreateShl(Op0, ConstantInt::get(I->getType(), 1),
                                       "opt.mul2", I);
    }

    return nullptr;
  }
};

} // end anonymous namespace

// Plugin registration
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {.APIVersion = LLVM_PLUGIN_API_VERSION,
          .PluginName = "Peephole Optimizations",
          .PluginVersion = "v0.1",
          .RegisterPassBuilderCallbacks = [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "peephole") {
                    FPM.addPass(PeepholePass());
                    return true;
                  }
                  return false;
                });

            // Register as a step in optimization pipeline
            PB.registerScalarOptimizerLateEPCallback(
                [](FunctionPassManager &FPM, OptimizationLevel Level) {
                  FPM.addPass(PeepholePass());
                });
          }};
}
