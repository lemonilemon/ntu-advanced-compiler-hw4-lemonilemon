#include "llvm/Analysis/ValueTracking.h"
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

class EnhancedPeepholePass : public PassInfoMixin<EnhancedPeepholePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    bool Changed = false;
    SmallVector<Instruction *, 32> WorkList;

    // Build initial worklist
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        WorkList.push_back(&I);
      }
    }

    // Process worklist until empty
    while (!WorkList.empty()) {
      Instruction *I = WorkList.pop_back_val();

      // Skip deleted instructions
      if (!I->getParent())
        continue;

      if (Value *V = optimizeInstruction(I)) {
        // Add users to worklist since they might become optimizable
        for (User *U : I->users()) {
          if (Instruction *UserI = dyn_cast<Instruction>(U))
            WorkList.push_back(UserI);
        }

        I->replaceAllUsesWith(V);
        I->eraseFromParent();
        Changed = true;
        continue;
      }

      // Try to optimize instruction sequences
      if (Instruction *Next = I->getNextNode()) {
        if (Value *V = optimizeInstructionPair(I, Next)) {
          WorkList.push_back(Next);
          Next->replaceAllUsesWith(V);
          Next->eraseFromParent();
          I->eraseFromParent();
          Changed = true;
        }
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  Value *optimizeInstruction(Instruction *I) {
    // Skip instructions that are likely not worth optimizing
    if (!I->hasOneUse() && !isa<BinaryOperator>(I) && !isa<CmpInst>(I))
      return nullptr;

    switch (I->getOpcode()) {
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::UDiv:
    case Instruction::SDiv:
      if (Value *V = optimizeArithmeticInst(I))
        return V;
      break;

    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
      if (Value *V = optimizeBitwiseInst(I))
        return V;
      break;

    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
      if (Value *V = optimizeShiftInst(I))
        return V;
      break;

    case Instruction::ICmp:
      if (Value *V = optimizeICmpInst(cast<ICmpInst>(I)))
        return V;
      break;

    case Instruction::Select:
      if (Value *V = optimizeSelectInst(cast<SelectInst>(I)))
        return V;
      break;
    }

    return nullptr;
  }

  Value *optimizeArithmeticInst(Instruction *I) {
    if (!I->getType()->isIntegerTy())
      return nullptr;

    Value *LHS = I->getOperand(0);
    Value *RHS = I->getOperand(1);
    ConstantInt *C1, *C2;

    // Constant folding
    if (match(RHS, m_ConstantInt(C1))) {
      // X + 0 = X
      if (I->getOpcode() == Instruction::Add && C1->isZero())
        return LHS;

      // X - 0 = X
      if (I->getOpcode() == Instruction::Sub && C1->isZero())
        return LHS;

      // X * 0 = 0
      if (I->getOpcode() == Instruction::Mul && C1->isZero())
        return C1;

      // X * 1 = X
      if (I->getOpcode() == Instruction::Mul && C1->isOne())
        return LHS;

      // X / 1 = X
      if ((I->getOpcode() == Instruction::UDiv ||
           I->getOpcode() == Instruction::SDiv) &&
          C1->isOne())
        return LHS;
    }

    // Algebraic simplifications
    switch (I->getOpcode()) {
    case Instruction::Add:
      // (X + C1) + C2 -> X + (C1 + C2)
      if (match(I, m_Add(m_Add(m_Value(LHS), m_ConstantInt(C1)),
                         m_ConstantInt(C2)))) {
        APInt Combined = C1->getValue() + C2->getValue();
        return BinaryOperator::CreateAdd(
            LHS, ConstantInt::get(I->getType(), Combined));
      }
      break;

    case Instruction::Sub:
      // X - X = 0
      if (match(I, m_Sub(m_Value(LHS), m_Specific(LHS))))
        return Constant::getNullValue(I->getType());
      break;

    case Instruction::Mul:
      // Strength reduction for multiplication by powers of 2
      if (match(RHS, m_ConstantInt(C1)) && C1->getValue().isPowerOf2()) {
        unsigned ShiftAmt = C1->getValue().logBase2();
        return BinaryOperator::CreateShl(
            LHS, ConstantInt::get(I->getType(), ShiftAmt));
      }
      break;

    case Instruction::UDiv:
    case Instruction::SDiv:
      // Strength reduction for division by powers of 2
      if (match(RHS, m_ConstantInt(C1)) && C1->getValue().isPowerOf2()) {
        unsigned ShiftAmt = C1->getValue().logBase2();
        auto ShiftOpcode = (I->getOpcode() == Instruction::UDiv)
                               ? Instruction::LShr
                               : Instruction::AShr;
        return BinaryOperator::Create(ShiftOpcode, LHS,
                                      ConstantInt::get(I->getType(), ShiftAmt));
      }
      break;
    }

    return nullptr;
  }

  Value *optimizeBitwiseInst(Instruction *I) {
    if (!I->getType()->isIntegerTy())
      return nullptr;

    Value *LHS = I->getOperand(0);
    Value *RHS = I->getOperand(1);
    ConstantInt *C;

    switch (I->getOpcode()) {
    case Instruction::And:
      // X & 0 = 0
      if (match(RHS, m_Zero()))
        return RHS;
      // X & -1 = X
      if (match(RHS, m_AllOnes()))
        return LHS;
      break;

    case Instruction::Or:
      // X | 0 = X
      if (match(RHS, m_Zero()))
        return LHS;
      // X | -1 = -1
      if (match(RHS, m_AllOnes()))
        return RHS;
      break;

    case Instruction::Xor:
      // X ^ X = 0
      if (LHS == RHS)
        return Constant::getNullValue(I->getType());
      // X ^ 0 = X
      if (match(RHS, m_Zero()))
        return LHS;
      break;
    }

    return nullptr;
  }

  Value *optimizeShiftInst(Instruction *I) {
    Value *LHS = I->getOperand(0);
    Value *RHS = I->getOperand(1);
    ConstantInt *C1, *C2;

    // X << 0 = X
    if (match(RHS, m_Zero()))
      return LHS;

    // Fold constant shifts
    if (match(LHS, m_ConstantInt(C1)) && match(RHS, m_ConstantInt(C2))) {
      if (I->getOpcode() == Instruction::Shl)
        return ConstantInt::get(I->getContext(),
                                C1->getValue().shl(C2->getValue()));
      else if (I->getOpcode() == Instruction::LShr)
        return ConstantInt::get(I->getContext(),
                                C1->getValue().lshr(C2->getValue()));
      else // AShr
        return ConstantInt::get(I->getContext(),
                                C1->getValue().ashr(C2->getValue()));
    }

    return nullptr;
  }

  Value *optimizeICmpInst(ICmpInst *ICI) {
    Value *LHS = ICI->getOperand(0);
    Value *RHS = ICI->getOperand(1);

    // Compare with self
    if (LHS == RHS) {
      switch (ICI->getPredicate()) {
      case ICmpInst::ICMP_EQ:  // X == X -> true
      case ICmpInst::ICMP_ULE: // X <= X -> true
      case ICmpInst::ICMP_SLE: // X <= X -> true
      case ICmpInst::ICMP_UGE: // X >= X -> true
      case ICmpInst::ICMP_SGE: // X >= X -> true
        return ConstantInt::getTrue(ICI->getContext());

      case ICmpInst::ICMP_NE:  // X != X -> false
      case ICmpInst::ICMP_ULT: // X < X -> false
      case ICmpInst::ICMP_SLT: // X < X -> false
      case ICmpInst::ICMP_UGT: // X > X -> false
      case ICmpInst::ICMP_SGT: // X > X -> false
        return ConstantInt::getFalse(ICI->getContext());
      }
    }

    // Compare with 0
    ConstantInt *C;
    if (match(RHS, m_ConstantInt(C)) && C->isZero()) {
      Value *Op;
      // Check if LHS is a NOT operation
      if (match(LHS, m_Not(m_Value(Op)))) {
        // !(X) == 0 -> X != 0
        if (ICI->getPredicate() == ICmpInst::ICMP_EQ)
          return new ICmpInst(ICmpInst::ICMP_NE, Op, RHS);
        // !(X) != 0 -> X == 0
        if (ICI->getPredicate() == ICmpInst::ICMP_NE)
          return new ICmpInst(ICmpInst::ICMP_EQ, Op, RHS);
      }
    }

    return nullptr;
  }

  Value *optimizeSelectInst(SelectInst *SI) {
    Value *Cond = SI->getCondition();
    Value *TrueVal = SI->getTrueValue();
    Value *FalseVal = SI->getFalseValue();

    // select true, X, Y -> X
    if (auto *C = dyn_cast<ConstantInt>(Cond))
      return C->isOne() ? TrueVal : FalseVal;

    // select C, X, X -> X
    if (TrueVal == FalseVal)
      return TrueVal;

    return nullptr;
  }
  Value *optimizeInstructionPair(Instruction *First, Instruction *Second) {
    // Skip if instructions are not in the same basic block
    if (First->getParent() != Second->getParent())
      return nullptr;

    // Pattern 1: (X << C1) >> C1 -> X & mask
    Value *X;
    ConstantInt *C1, *C2;
    if (match(Second, m_LShr(m_Shl(m_Value(X), m_ConstantInt(C1)),
                             m_ConstantInt(C2))) &&
        C1->getValue() == C2->getValue()) {
      unsigned ShiftAmt = C1->getValue().getZExtValue();
      APInt Mask = APInt::getAllOnes(X->getType()->getIntegerBitWidth());
      Mask = Mask.lshr(ShiftAmt).shl(ShiftAmt);
      return BinaryOperator::CreateAnd(X, ConstantInt::get(X->getType(), Mask),
                                       "opt.shiftmask", Second);
    }

    // Pattern 2: (X >> C1) << C1 -> X & mask
    if (match(Second, m_Shl(m_LShr(m_Value(X), m_ConstantInt(C1)),
                            m_ConstantInt(C2))) &&
        C1->getValue() == C2->getValue()) {
      unsigned ShiftAmt = C1->getValue().getZExtValue();
      APInt Mask = APInt::getAllOnes(X->getType()->getIntegerBitWidth());
      Mask = Mask.lshr(ShiftAmt).shl(ShiftAmt);
      return BinaryOperator::CreateAnd(X, ConstantInt::get(X->getType(), Mask),
                                       "opt.shiftmask", Second);
    }

    // Pattern 3: (X + C1) - C2 -> X + (C1 - C2)
    Value *Y;
    if (match(Second,
              m_Sub(m_Add(m_Value(X), m_ConstantInt(C1)), m_ConstantInt(C2)))) {
      APInt Combined = C1->getValue() - C2->getValue();
      return BinaryOperator::CreateAdd(X,
                                       ConstantInt::get(X->getType(), Combined),
                                       "opt.addsubcombine", Second);
    }

    // Pattern 4: (X - C1) + C2 -> X + (C2 - C1)
    if (match(Second,
              m_Add(m_Sub(m_Value(X), m_ConstantInt(C1)), m_ConstantInt(C2)))) {
      APInt Combined = C2->getValue() - C1->getValue();
      return BinaryOperator::CreateAdd(X,
                                       ConstantInt::get(X->getType(), Combined),
                                       "opt.subaddcombine", Second);
    }

    // Pattern 5: (X * C1) * C2 -> X * (C1 * C2)
    if (match(Second,
              m_Mul(m_Mul(m_Value(X), m_ConstantInt(C1)), m_ConstantInt(C2)))) {
      APInt Combined = C1->getValue() * C2->getValue();
      return BinaryOperator::CreateMul(X,
                                       ConstantInt::get(X->getType(), Combined),
                                       "opt.mulcombine", Second);
    }

    // Pattern 6: (X & C1) & C2 -> X & (C1 & C2)
    if (match(Second,
              m_And(m_And(m_Value(X), m_ConstantInt(C1)), m_ConstantInt(C2)))) {
      APInt Combined = C1->getValue() & C2->getValue();
      return BinaryOperator::CreateAnd(X,
                                       ConstantInt::get(X->getType(), Combined),
                                       "opt.andcombine", Second);
    }

    // Pattern 7: (X | C1) | C2 -> X | (C1 | C2)
    if (match(Second,
              m_Or(m_Or(m_Value(X), m_ConstantInt(C1)), m_ConstantInt(C2)))) {
      APInt Combined = C1->getValue() | C2->getValue();
      return BinaryOperator::CreateOr(
          X, ConstantInt::get(X->getType(), Combined), "opt.orcombine", Second);
    }

    // Pattern 8: (~X) & (~Y) -> ~(X | Y)
    if (match(Second, m_And(m_Not(m_Value(X)), m_Not(m_Value(Y))))) {
      Value *Or = BinaryOperator::CreateOr(X, Y, "opt.or", Second);
      return BinaryOperator::CreateNot(Or, "opt.notcombine");
    }

    // Pattern 9: (X << C1) << C2 -> X << (C1 + C2)
    if (match(Second,
              m_Shl(m_Shl(m_Value(X), m_ConstantInt(C1)), m_ConstantInt(C2)))) {
      APInt Combined = C1->getValue() + C2->getValue();
      unsigned BitWidth = X->getType()->getIntegerBitWidth();
      if (Combined.ult(BitWidth)) {
        return BinaryOperator::CreateShl(
            X, ConstantInt::get(X->getType(), Combined), "opt.shlcombine",
            Second);
      }
    }

    return nullptr;
  }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {.APIVersion = LLVM_PLUGIN_API_VERSION,
          .PluginName = "Enhanced Peephole Optimizations",
          .PluginVersion = "v0.3",
          .RegisterPassBuilderCallbacks = [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "enhanced-peephole") {
                    FPM.addPass(EnhancedPeepholePass());
                    return true;
                  }
                  return false;
                });

            PB.registerScalarOptimizerLateEPCallback(
                [](FunctionPassManager &FPM, OptimizationLevel Level) {
                  FPM.addPass(EnhancedPeepholePass());
                });
          }};
}
