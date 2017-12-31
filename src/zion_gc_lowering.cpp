//===-- ZionGCLowering.cpp - Custom lowering for zion gc ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the custom lowering code required by the zion GC
// strategy.
//
// This pass implements the code transformation described in this paper:
//   "Accurate Garbage Collection in an Uncooperative Environment"
//   Fergus Henderson, ISMM, 2002
//
//===----------------------------------------------------------------------===//
#include "zion.h"
#include "dbg.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CodeGen/GCStrategy.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/EscapeEnumerator.h"
#include "llvm_utils.h"

#define GC_FRAME_TYPE "gc_frame_map"
#define GC_STACK_ENTRY_TYPE "gc_stack_entry"

namespace llvm {
	void initializeZionGCLoweringPass(PassRegistry&);
}

using namespace llvm;

#define DEBUG_TYPE "zion-gc-lowering"

namespace {

class ZionGCLowering : public FunctionPass {
  /// RootChain - This is the global linked-list that contains the chain of GC
  /// roots.
  GlobalVariable *Head;

  /// StackEntryTy - Abstract type of a link in the shadow stack.
  ///
  StructType *StackEntryTy;
  StructType *FrameMapTy;

  /// Roots - GC roots in the current function. Each is a pair of the
  /// intrinsic call and its corresponding alloca.
  std::vector<std::pair<CallInst *, AllocaInst *>> Roots;

public:
  static char ID;
  ZionGCLowering();
  ZionGCLowering(StructType *StackEntryTy, StructType *FrameMapTy);

  bool doInitialization(Module &M) override;
  bool runOnFunction(Function &F) override;

private:
  bool IsNullValue(Value *V);
  Constant *GetFrameMap(Function &F);
  Type *GetConcreteStackEntryType(Function &F);
  void CollectRoots(Function &F);
  static GetElementPtrInst *CreateGEP(LLVMContext &Context, IRBuilder<> &B,
                                      Type *Ty, Value *BasePtr, int Idx1,
                                      const char *Name);
  static GetElementPtrInst *CreateGEP(LLVMContext &Context, IRBuilder<> &B,
                                      Type *Ty, Value *BasePtr, int Idx1, int Idx2,
                                      const char *Name);
};
}


INITIALIZE_PASS_BEGIN(ZionGCLowering, DEBUG_TYPE, "Zion GC Lowering", false, false)
INITIALIZE_PASS_DEPENDENCY(GCModuleInfo)
INITIALIZE_PASS_END(ZionGCLowering, DEBUG_TYPE, "Zion GC Lowering", false, false)


char ZionGCLowering::ID = 0;

ZionGCLowering::ZionGCLowering() : ZionGCLowering(nullptr, nullptr) {}
ZionGCLowering::ZionGCLowering(StructType *StackEntryTy, StructType *FrameMapTy)
  : FunctionPass(ID), Head(nullptr), StackEntryTy(StackEntryTy),
    FrameMapTy(FrameMapTy) {
  initializeZionGCLoweringPass(*PassRegistry::getPassRegistry());
}

Constant *ZionGCLowering::GetFrameMap(Function &F) {
	Type *VoidPtr = Type::getInt8PtrTy(F.getContext());

	// Truncate the ZionDescriptor if some metadata is null.
	unsigned NumMeta = 0;
	SmallVector<Constant *, 16> Metadata;
	for (unsigned I = 0; I != Roots.size(); ++I) {
		Constant *C = cast<Constant>(Roots[I].first->getArgOperand(1));
		if (!C->isNullValue())
			NumMeta = I + 1;
		Metadata.push_back(ConstantExpr::getBitCast(C, VoidPtr));
	}
	Metadata.resize(NumMeta);

	Type *Int32Ty = Type::getInt32Ty(F.getContext());

	Constant *BaseElts[] = {
		ConstantInt::get(Int32Ty, Roots.size(), false),
		ConstantInt::get(Int32Ty, NumMeta, false),
	};

	Constant *DescriptorElts[] = {
		ConstantStruct::get(FrameMapTy, BaseElts),
		ConstantArray::get(ArrayType::get(VoidPtr, NumMeta), Metadata)};

	Type *EltTys[] = {DescriptorElts[0]->getType(), DescriptorElts[1]->getType()};
	StructType *STy = StructType::create(EltTys, GC_FRAME_TYPE "." + utostr(NumMeta));

	Constant *FrameMap = ConstantStruct::get(STy, DescriptorElts);

	Constant *GV = new GlobalVariable(*F.getParent(), FrameMap->getType(), true,
			GlobalVariable::InternalLinkage, FrameMap,
			"__gc_" + F.getName());

	Constant *GEPIndices[] = {
		ConstantInt::get(Type::getInt32Ty(F.getContext()), 0),
		ConstantInt::get(Type::getInt32Ty(F.getContext()), 0),
	};
	return ConstantExpr::getGetElementPtr(FrameMap->getType(), GV, GEPIndices);
}

Type *ZionGCLowering::GetConcreteStackEntryType(Function &F) {
  // doInitialization creates the generic version of this type.
  std::vector<Type *> EltTys;
  EltTys.push_back(StackEntryTy);
  for (size_t I = 0; I != Roots.size(); I++)
    EltTys.push_back(Roots[I].second->getAllocatedType());

  return StructType::create(EltTys, (GC_STACK_ENTRY_TYPE "." + F.getName()).str());
}

/// doInitialization - If this module uses the GC intrinsics, find them now. If
/// not, exit fast.
bool ZionGCLowering::doInitialization(Module &M) {
  bool Active = false;
  for (Function &F : M) {
    if (F.hasGC() && F.getGC() == std::string("zion")) {
      Active = true;
      break;
    }
  }
  if (!Active)
    return false;
  
  PointerType *StackEntryPtrTy = PointerType::getUnqual(StackEntryTy);

  // Get the root chain if it already exists.
  Head = M.getGlobalVariable("llvm_gc_root_chain");
  if (!Head) {
	  assert(false);
    // If the root chain does not exist, insert a new one with linkonce
    // linkage!
    Head = new GlobalVariable(
        M, StackEntryPtrTy, false, GlobalValue::LinkOnceAnyLinkage,
        Constant::getNullValue(StackEntryPtrTy), "llvm_gc_root_chain");
  } else if (Head->hasExternalLinkage() && Head->isDeclaration()) {
    Head->setInitializer(Constant::getNullValue(StackEntryPtrTy));
    Head->setLinkage(GlobalValue::LinkOnceAnyLinkage);
  }

  return true;
}

bool ZionGCLowering::IsNullValue(Value *V) {
  if (Constant *C = dyn_cast<Constant>(V))
    return C->isNullValue();
  return false;
}

void ZionGCLowering::CollectRoots(Function &F) {
  // FIXME: Account for original alignment. Could fragment the root array.
  //   Approach 1: Null initialize empty slots at runtime. Yuck.
  //   Approach 2: Emit a map of the array instead of just a count.

  assert(Roots.empty() && "Not cleaned up?");

  SmallVector<std::pair<CallInst *, AllocaInst *>, 16> MetaRoots;

  for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB)
    for (BasicBlock::iterator II = BB->begin(), E = BB->end(); II != E;)
      if (IntrinsicInst *CI = dyn_cast<IntrinsicInst>(II++))
        if (Function *F = CI->getCalledFunction())
          if (F->getIntrinsicID() == Intrinsic::gcroot) {
            std::pair<CallInst *, AllocaInst *> Pair = std::make_pair(
                CI,
                cast<AllocaInst>(CI->getArgOperand(0)->stripPointerCasts()));
            if (IsNullValue(CI->getArgOperand(1)))
              Roots.push_back(Pair);
            else
              MetaRoots.push_back(Pair);
          }

  // Number roots with metadata (usually empty) at the beginning, so that the
  // FrameMap::Meta array can be elided.
  Roots.insert(Roots.begin(), MetaRoots.begin(), MetaRoots.end());
}

GetElementPtrInst *ZionGCLowering::CreateGEP(LLVMContext &Context,
		IRBuilder<> &B, Type *Ty,
		Value *BasePtr, int Idx,
		int Idx2,
		const char *Name)
{
	Value *Indices[] = {
		ConstantInt::get(Type::getInt32Ty(Context), 0),
		ConstantInt::get(Type::getInt32Ty(Context), Idx),
		ConstantInt::get(Type::getInt32Ty(Context), Idx2)
	};
	Value *Val = B.CreateGEP(Ty, BasePtr, Indices, Name);

	assert(isa<GetElementPtrInst>(Val) && "Unexpected folded constant");

	return dyn_cast<GetElementPtrInst>(Val);
}

GetElementPtrInst *ZionGCLowering::CreateGEP(LLVMContext &Context,
                                            IRBuilder<> &B, Type *Ty, Value *BasePtr,
                                            int Idx, const char *Name) {
  Value *Indices[] = {
	  ConstantInt::get(Type::getInt32Ty(Context), 0),
	  ConstantInt::get(Type::getInt32Ty(Context), Idx)
  };
  Value *Val = B.CreateGEP(Ty, BasePtr, Indices, Name);

  assert(isa<GetElementPtrInst>(Val) && "Unexpected folded constant");

  return dyn_cast<GetElementPtrInst>(Val);
}

/// runOnFunction - Insert code to maintain the shadow stack.
bool ZionGCLowering::runOnFunction(Function &F) {
  // Quick exit for functions that do not use the shadow stack GC.
  if (!F.hasGC() ||
      F.getGC() != std::string("zion"))
    return false;
  
  LLVMContext &Context = F.getContext();

  // Find calls to llvm.gcroot.
  CollectRoots(F);

  // If there are no roots in this function, then there is no need to add a
  // stack map entry for it.
  if (Roots.empty())
    return false;

  // Build the constant map and figure the type of the shadow stack entry.
  Value *FrameMap = GetFrameMap(F);
  Type *ConcreteStackEntryTy = GetConcreteStackEntryType(F);

  // Build the shadow stack entry at the very start of the function.
  BasicBlock::iterator IP = F.getEntryBlock().begin();
  IRBuilder<> AtEntry(IP->getParent(), IP);

  Instruction *StackEntry = AtEntry.CreateAlloca(ConcreteStackEntryTy, nullptr, "gc_frame");

  while (isa<AllocaInst>(IP)) {
	  ++IP;
  }

  AtEntry.SetInsertPoint(IP->getParent(), IP);

  // Initialize the map pointer and load the current head of the shadow stack.
  Instruction *CurrentHead = AtEntry.CreateLoad(Head, "gc_currhead");
  Instruction *EntryMapPtr = CreateGEP(Context, AtEntry, ConcreteStackEntryTy,
                                       StackEntry, 0, 1, "gc_frame.map");
  AtEntry.CreateStore(FrameMap, EntryMapPtr);

  // After all the allocas...
  for (unsigned I = 0, E = Roots.size(); I != E; ++I) {
    // For each root, find the corresponding slot in the aggregate...
    Value *SlotPtr = CreateGEP(Context, AtEntry, ConcreteStackEntryTy,
                               StackEntry, 1 + I, "gc_root");

    // And use it in lieu of the alloca.
    AllocaInst *OriginalAlloca = Roots[I].second;
    SlotPtr->takeName(OriginalAlloca);
    OriginalAlloca->replaceAllUsesWith(SlotPtr);
  }

  // Move past the original stores inserted by GCStrategy::InitRoots. This isn't
  // really necessary (the collector would never see the intermediate state at
  // runtime), but it's nicer not to push the half-initialized entry onto the
  // shadow stack.
  while (isa<StoreInst>(IP))
    ++IP;
  AtEntry.SetInsertPoint(IP->getParent(), IP);

  // Push the entry onto the shadow stack.
  Instruction *EntryNextPtr = CreateGEP(Context, AtEntry, ConcreteStackEntryTy,
                                        StackEntry, 0, 0, "gc_frame.next");
  Instruction *NewHeadVal = CreateGEP(Context, AtEntry, ConcreteStackEntryTy,
                                      StackEntry, 0, "gc_newhead");
  AtEntry.CreateStore(CurrentHead, EntryNextPtr);
  AtEntry.CreateStore(NewHeadVal, Head);

  // For each instruction that escapes...
  EscapeEnumerator EE(F, "gc_cleanup");
  while (IRBuilder<> *AtExit = EE.Next()) {
    // Pop the entry from the shadow stack. Don't reuse CurrentHead from
    // AtEntry, since that would make the value live for the entire function.
    Instruction *EntryNextPtr2 =
        CreateGEP(Context, *AtExit, ConcreteStackEntryTy, StackEntry, 0, 0,
                  "gc_frame.next");
    Value *SavedHead = AtExit->CreateLoad(EntryNextPtr2, "gc_savedhead");
    AtExit->CreateStore(SavedHead, Head);
  }

  // Delete the original allocas (which are no longer used) and the intrinsic
  // calls (which are no longer valid). Doing this last avoids invalidating
  // iterators.
  for (unsigned I = 0, E = Roots.size(); I != E; ++I) {
    Roots[I].first->eraseFromParent();
    Roots[I].second->eraseFromParent();
  }

  Roots.clear();
  return true;
}

class ZionGC : public GCStrategy {
public:
	ZionGC() {
		InitRoots = true;
		CustomRoots = true;
	}
};
static GCRegistry::Add<ZionGC> C("zion", "Zion GC");

namespace llvm {
	FunctionPass *createZionGCLoweringPass(llvm::StructType *StackEntryTy, llvm::StructType *FrameMapTy) {
		return new ZionGCLowering(StackEntryTy, FrameMapTy);
	}
}
