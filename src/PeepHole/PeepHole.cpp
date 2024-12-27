#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include <bits/stdc++.h>

using namespace llvm;
using namespace llvm::PatternMatch;

namespace {
class TransformationVerifier {
private:
  // Track dependencies between instructions
  struct DependencyInfo {
    SmallPtrSet<Instruction *, 8> RAW; // Read-after-write
    SmallPtrSet<Instruction *, 8> WAW; // Write-after-write
    SmallPtrSet<Instruction *, 8> WAR; // Write-after-read
  };

  // Verify type compatibility
  bool verifyTypes(Value *Original, Value *Transformed) {
    if (!Original || !Transformed)
      return false;
    return Original->getType() == Transformed->getType();
  }

  // Verify control flow preservation
  bool verifyControlFlow(Instruction *Original, Instruction *Transformed) {
    if (Original->isTerminator() != Transformed->isTerminator())
      return false;

    if (Original->mayHaveSideEffects() != Transformed->mayHaveSideEffects())
      return false;

    return true;
  }

  // Verify memory access patterns
  bool verifyMemoryAccess(Instruction *Original, Instruction *Transformed,
                          const MemorySSA &MSSA) {
    bool OriginalReads = Original->mayReadFromMemory();
    bool OriginalWrites = Original->mayWriteToMemory();
    bool TransformedReads = Transformed->mayReadFromMemory();
    bool TransformedWrites = Transformed->mayWriteToMemory();

    // Check if memory access patterns match
    if (OriginalReads != TransformedReads ||
        OriginalWrites != TransformedWrites)
      return false;

    // If there are memory operations, verify they're equivalent
    if (OriginalReads || OriginalWrites) {
      auto *OriginalMA = MSSA.getMemoryAccess(Original);
      auto *TransformedMA = MSSA.getMemoryAccess(Transformed);

      if (!OriginalMA || !TransformedMA)
        return false;

      // Verify memory dependencies are preserved
      return MSSA.dominates(OriginalMA, TransformedMA);
    }

    return true;
  }

  // Verify data dependencies
  bool verifyDataDependencies(Instruction *Original, Instruction *Transformed,
                              DependencyInfo &DepInfo) {
    // Check if all required operands are available
    for (Use &U : Transformed->operands()) {
      if (auto *Op = dyn_cast<Instruction>(U.get())) {
        // Verify no circular dependencies are introduced
        if (DepInfo.RAW.count(Op) || DepInfo.WAW.count(Op))
          return false;
      }
    }

    // Update dependency information
    for (Use &U : Original->operands()) {
      if (auto *Op = dyn_cast<Instruction>(U.get())) {
        DepInfo.RAW.insert(Op);
      }
    }

    return true;
  }

  // Verify arithmetic properties
  bool verifyArithmetic(Instruction *Original, Instruction *Transformed,
                        ScalarEvolution &SE) {
    // Only verify arithmetic instructions
    if (!Original->isBinaryOp() || !Transformed->isBinaryOp())
      return true;

    // Get SCEV expressions for both instructions
    const SCEV *OriginalSCEV = SE.getSCEV(Original);
    const SCEV *TransformedSCEV = SE.getSCEV(Transformed);

    // Compare SCEV expressions for equivalence
    return OriginalSCEV == TransformedSCEV;
  }

  // Verify exception behavior
  bool verifyExceptions(Instruction *Original, Instruction *Transformed) {
    return Original->mayThrow() == Transformed->mayThrow();
  }

public:
  bool verify(Instruction *Original, Value *Transformed, Function &F,
              FunctionAnalysisManager &FAM) {
    // Get required analyses
    auto &MSSA = FAM.getResult<MemorySSAAnalysis>(F).getMSSA();
    auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);

    // Cast transformed value to instruction if possible
    auto *TransformedInst = dyn_cast<Instruction>(Transformed);
    if (!TransformedInst)
      return false;

    // Initialize dependency tracking
    DependencyInfo DepInfo;

    // Run all verification checks
    if (!verifyTypes(Original, Transformed))
      return false;

    if (!verifyControlFlow(Original, TransformedInst))
      return false;

    if (!verifyMemoryAccess(Original, TransformedInst, MSSA))
      return false;

    if (!verifyDataDependencies(Original, TransformedInst, DepInfo))
      return false;

    if (!verifyArithmetic(Original, TransformedInst, SE))
      return false;

    if (!verifyExceptions(Original, TransformedInst))
      return false;

    return true;
  }
};

struct PeepHolePass : public PassInfoMixin<PeepHolePass> {
private:
  TransformationVerifier Verifier;
  struct Pattern {
    std::function<bool(Instruction *)> matcher;
    std::function<Value *(Instruction *)> replacement;
    int costDelta;
  };

  std::vector<Pattern> patterns;

  void initializePatterns() {
    // 1. Multiply by power of 2 -> Shift left
    patterns.push_back(
        {[](Instruction *I) {
           if (auto *MI = dyn_cast<BinaryOperator>(I)) {
             if (MI->getOpcode() == Instruction::Mul) {
               if (auto *CI = dyn_cast<ConstantInt>(MI->getOperand(1))) {
                 return CI->getValue().isPowerOf2();
               }
             }
           }
           return false;
         },
         [](Instruction *I) {
           auto *MI = cast<BinaryOperator>(I);
           IRBuilder<> Builder(I);
           auto *CI = cast<ConstantInt>(MI->getOperand(1));
           return Builder.CreateShl(
               MI->getOperand(0), Builder.getInt32(CI->getValue().logBase2()));
         },
         -1});

    // 2. Division by power of 2 -> Shift right
    patterns.push_back(
        {[](Instruction *I) {
           if (auto *DI = dyn_cast<BinaryOperator>(I)) {
             if (DI->getOpcode() == Instruction::UDiv) {
               if (auto *CI = dyn_cast<ConstantInt>(DI->getOperand(1))) {
                 return CI->getValue().isPowerOf2();
               }
             }
           }
           return false;
         },
         [](Instruction *I) {
           auto *DI = cast<BinaryOperator>(I);
           IRBuilder<> Builder(I);
           auto *CI = cast<ConstantInt>(DI->getOperand(1));
           return Builder.CreateLShr(
               DI->getOperand(0), Builder.getInt32(CI->getValue().logBase2()));
         },
         -2});

    // 3. Add zero elimination
    patterns.push_back(
        {[](Instruction *I) {
           if (auto *AI = dyn_cast<BinaryOperator>(I)) {
             if (AI->getOpcode() == Instruction::Add) {
               for (int i = 0; i < 2; i++) {
                 if (auto *CI = dyn_cast<ConstantInt>(AI->getOperand(i))) {
                   if (CI->isZero())
                     return true;
                 }
               }
             }
           }
           return false;
         },
         [](Instruction *I) {
           auto *AI = cast<BinaryOperator>(I);
           return AI->getOperand(isa<ConstantInt>(AI->getOperand(0)) ? 1 : 0);
         },
         -1});

    // 4. Multiply by zero -> zero
    patterns.push_back(
        {[](Instruction *I) {
           if (auto *MI = dyn_cast<BinaryOperator>(I)) {
             if (MI->getOpcode() == Instruction::Mul) {
               for (int i = 0; i < 2; i++) {
                 if (auto *CI = dyn_cast<ConstantInt>(MI->getOperand(i))) {
                   if (CI->isZero())
                     return true;
                 }
               }
             }
           }
           return false;
         },
         [](Instruction *I) {
           IRBuilder<> Builder(I);
           return Builder.getInt32(0);
         },
         -1});

    // 5. XOR with self -> zero
    patterns.push_back({[](Instruction *I) {
                          if (auto *XI = dyn_cast<BinaryOperator>(I)) {
                            if (XI->getOpcode() == Instruction::Xor) {
                              return XI->getOperand(0) == XI->getOperand(1);
                            }
                          }
                          return false;
                        },
                        [](Instruction *I) {
                          IRBuilder<> Builder(I);
                          return Builder.getInt32(0);
                        },
                        -1});

    // 6. AND with self -> self
    patterns.push_back({[](Instruction *I) {
                          if (auto *AI = dyn_cast<BinaryOperator>(I)) {
                            if (AI->getOpcode() == Instruction::And) {
                              return AI->getOperand(0) == AI->getOperand(1);
                            }
                          }
                          return false;
                        },
                        [](Instruction *I) { return I->getOperand(0); }, -1});

    // 7. OR with self -> self
    patterns.push_back({[](Instruction *I) {
                          if (auto *OI = dyn_cast<BinaryOperator>(I)) {
                            if (OI->getOpcode() == Instruction::Or) {
                              return OI->getOperand(0) == OI->getOperand(1);
                            }
                          }
                          return false;
                        },
                        [](Instruction *I) { return I->getOperand(0); }, -1});

    // 8. NOT NOT -> original
    patterns.push_back(
        {[](Instruction *I) {
           if (auto *NI = dyn_cast<BinaryOperator>(I)) {
             if (NI->getOpcode() == Instruction::Xor) {
               if (auto *CI = dyn_cast<ConstantInt>(NI->getOperand(1))) {
                 if (CI->isAllOnesValue()) {
                   if (auto *PI = dyn_cast<BinaryOperator>(NI->getOperand(0))) {
                     if (PI->getOpcode() == Instruction::Xor) {
                       if (auto *PCI =
                               dyn_cast<ConstantInt>(PI->getOperand(1))) {
                         return PCI->isAllOnesValue();
                       }
                     }
                   }
                 }
               }
             }
           }
           return false;
         },
         [](Instruction *I) {
           auto *NI = cast<BinaryOperator>(I);
           auto *PI = cast<BinaryOperator>(NI->getOperand(0));
           return PI->getOperand(0);
         },
         -2});

    // 9. AND with all ones -> self
    patterns.push_back(
        {[](Instruction *I) {
           if (auto *AI = dyn_cast<BinaryOperator>(I)) {
             if (AI->getOpcode() == Instruction::And) {
               for (int i = 0; i < 2; i++) {
                 if (auto *CI = dyn_cast<ConstantInt>(AI->getOperand(i))) {
                   if (CI->isAllOnesValue())
                     return true;
                 }
               }
             }
           }
           return false;
         },
         [](Instruction *I) {
           auto *AI = cast<BinaryOperator>(I);
           return AI->getOperand(isa<ConstantInt>(AI->getOperand(0)) ? 1 : 0);
         },
         -1});

    // 10. OR with zero -> self
    patterns.push_back(
        {[](Instruction *I) {
           if (auto *OI = dyn_cast<BinaryOperator>(I)) {
             if (OI->getOpcode() == Instruction::Or) {
               for (int i = 0; i < 2; i++) {
                 if (auto *CI = dyn_cast<ConstantInt>(OI->getOperand(i))) {
                   if (CI->isZero())
                     return true;
                 }
               }
             }
           }
           return false;
         },
         [](Instruction *I) {
           auto *OI = cast<BinaryOperator>(I);
           return OI->getOperand(isa<ConstantInt>(OI->getOperand(0)) ? 1 : 0);
         },
         -1});
    // 11. Constant Propagation
    patterns.push_back(
        {[](Instruction *I) {
           if (auto *BinOp = dyn_cast<BinaryOperator>(I)) {
             if (auto *C1 = dyn_cast<ConstantInt>(BinOp->getOperand(0))) {
               if (auto *C2 = dyn_cast<ConstantInt>(BinOp->getOperand(1))) {
                 return true;
               }
             }
           }
           return false;
         },
         [](Instruction *I) {
           auto *BinOp = cast<BinaryOperator>(I);
           auto *C1 = cast<ConstantInt>(BinOp->getOperand(0));
           auto *C2 = cast<ConstantInt>(BinOp->getOperand(1));
           Constant *Folded = nullptr;
           switch (BinOp->getOpcode()) {
           case Instruction::Add:
             Folded = ConstantInt::get(BinOp->getType(),
                                       C1->getValue() + C2->getValue());
             break;
           case Instruction::Sub:
             Folded = ConstantInt::get(BinOp->getType(),
                                       C1->getValue() - C2->getValue());
             break;
           case Instruction::Mul:
             Folded = ConstantInt::get(BinOp->getType(),
                                       C1->getValue() * C2->getValue());
             break;
           case Instruction::UDiv:
             Folded = ConstantInt::get(BinOp->getType(),
                                       C1->getValue().udiv(C2->getValue()));
             break;
           case Instruction::SDiv:
             Folded = ConstantInt::get(BinOp->getType(),
                                       C1->getValue().sdiv(C2->getValue()));
             break;
           // Add more cases as needed
           default:
             break;
           }
           return Folded;
         },
         -1});
    // 12. Subtract zero elimination
    patterns.push_back(
        {[](Instruction *I) {
           if (auto *SI = dyn_cast<BinaryOperator>(I)) {
             if (SI->getOpcode() == Instruction::Sub) {
               if (auto *CI = dyn_cast<ConstantInt>(SI->getOperand(1))) {
                 return CI->isZero();
               }
             }
           }
           return false;
         },
         [](Instruction *I) {
           auto *SI = cast<BinaryOperator>(I);
           return SI->getOperand(0);
         },
         -1});

    // 13. Negate zero
    patterns.push_back(
        {[](Instruction *I) {
           if (auto *SI = dyn_cast<UnaryOperator>(I)) {
             if (SI->getOpcode() == Instruction::FNeg) {
               if (auto *CI = dyn_cast<ConstantInt>(SI->getOperand(0))) {
                 return CI->isZero();
               }
             }
           }
           return false;
         },
         [](Instruction *I) {
           IRBuilder<> Builder(I);
           return Builder.getInt32(0);
         },
         -1});

    // 14. Multiply by one
    patterns.push_back(
        {[](Instruction *I) {
           if (auto *MI = dyn_cast<BinaryOperator>(I)) {
             if (MI->getOpcode() == Instruction::Mul) {
               for (int i = 0; i < 2; i++) {
                 if (auto *CI = dyn_cast<ConstantInt>(MI->getOperand(i))) {
                   if (CI->isOne())
                     return true;
                 }
               }
             }
           }
           return false;
         },
         [](Instruction *I) {
           auto *MI = cast<BinaryOperator>(I);
           return MI->getOperand(isa<ConstantInt>(MI->getOperand(0)) ? 1 : 0);
         },
         -1});

    // 15. Divide by one
    patterns.push_back(
        {[](Instruction *I) {
           if (auto *DI = dyn_cast<BinaryOperator>(I)) {
             if (DI->getOpcode() == Instruction::UDiv ||
                 DI->getOpcode() == Instruction::SDiv) {
               if (auto *CI = dyn_cast<ConstantInt>(DI->getOperand(1))) {
                 return CI->isOne();
               }
             }
           }
           return false;
         },
         [](Instruction *I) {
           auto *DI = cast<BinaryOperator>(I);
           return DI->getOperand(0);
         },
         -1});
  }
  int performDCE(Function &F) {
    std::vector<Instruction *> toErase;

    for (auto &BB : F) {
      for (auto &I : BB) {
        if (I.use_empty() && !I.isTerminator() && !I.mayHaveSideEffects()) {
          toErase.push_back(&I);
        }
      }
    }

    int numInstructionsRemoved = toErase.size();
    for (auto *I : toErase) {
      I->eraseFromParent();
    }
    return numInstructionsRemoved;
  }

public:
  PeepHolePass() { initializePatterns(); }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    bool changed = false;
    int costDelta = 0;

    for (auto &BB : F) {
      for (auto &I : BB) {
        for (const auto &pattern : patterns) {
          if (pattern.matcher(&I)) {
            if (Value *replacement = pattern.replacement(&I)) {
              if (replacement && Verifier.verify(&I, replacement, F, FAM)) {
                I.replaceAllUsesWith(replacement);
                changed = true;
                costDelta += pattern.costDelta;
              }
            }
          }
        }
      }
    }
    // perform DCE
    if (changed) {
      errs() << "Total cost delta: " << costDelta << '\n';
      errs() << "Total instructions removed: " << performDCE(F) << '\n';
    }

    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {.APIVersion = LLVM_PLUGIN_API_VERSION,
          .PluginName = "PeepHole pass",
          .PluginVersion = "v0.1",
          .RegisterPassBuilderCallbacks = [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [&](StringRef Name, FunctionPassManager &MPM,
                    ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "peephole") {
                    MPM.addPass(PeepHolePass());
                    return true;
                  }
                  return false;
                });
          }};
}
