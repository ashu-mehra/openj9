/*******************************************************************************
 * Copyright (c) 2000, 2020 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/

#include "codegen/AheadOfTimeCompile.hpp"
#include "env/FrontEnd.hpp"
#include "codegen/Instruction.hpp"
#include "compile/AOTClassInfo.hpp"
#include "compile/Compilation.hpp"
#include "compile/ResolvedMethod.hpp"
#include "compile/VirtualGuard.hpp"
#include "env/CHTable.hpp"
#include "env/ClassLoaderTable.hpp"
#include "env/CompilerEnv.hpp"
#include "env/SharedCache.hpp"
#include "env/jittypes.h"
#include "env/VMJ9.h"
#include "il/Node.hpp"
#include "il/Node_inlines.hpp"
#include "il/SymbolReference.hpp"
#include "ras/DebugCounter.hpp"
#include "runtime/CodeCacheConfig.hpp"
#include "runtime/CodeCacheManager.hpp"
#include "runtime/RelocationRuntime.hpp"
#include "runtime/RelocationRecord.hpp"
#include "runtime/SymbolValidationManager.hpp"

#define NON_HELPER   0x00

void J9::X86::AheadOfTimeCompile::processRelocations()
   {
   TR::Compilation *comp = _cg->comp();

   // calculate the amount of memory needed to hold the relocation data
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(_cg->fe());

   if (comp->target().is64Bit()
       && TR::CodeCacheManager::instance()->codeCacheConfig().needsMethodTrampolines()
       && _cg->getPicSlotCount())
      {
      _cg->addExternalRelocation(new (_cg->trHeapMemory()) TR::ExternalRelocation(NULL,
                                                                                 (uint8_t *)(uintptr_t)_cg->getPicSlotCount(),
                                                                                 TR_PicTrampolines, _cg),
                            __FILE__,
                            __LINE__,
                            NULL);
      }


   for (auto aotIterator = _cg->getExternalRelocationList().begin(); aotIterator != _cg->getExternalRelocationList().end(); ++aotIterator)
	  (*aotIterator)->addExternalRelocation(_cg);

   TR::IteratedExternalRelocation *r;
   for (r = self()->getAOTRelocationTargets().getFirst();
        r != NULL;
        r = r->getNext())
      {
      self()->addToSizeOfAOTRelocations(r->getSizeOfRelocationData());
      }

   // now allocate the memory  size of all iterated relocations + the header (total length field)

   // Note that when using the SymbolValidationManager, the well-known classes
   // must be checked even if no explicit records were generated, since they
   // might be responsible for the lack of records.
   bool useSVM = comp->getOption(TR_UseSymbolValidationManager);
   if (self()->getSizeOfAOTRelocations() != 0 || useSVM)
      {
      // It would be more straightforward to put the well-known classes offset
      // in the AOT method header, but that would use space for AOT bodies that
      // don't use the SVM. TODO: Move it once SVM takes over?
      int wellKnownClassesOffsetSize = useSVM ? SIZEPOINTER : 0;
      uintptr_t reloBufferSize =
         self()->getSizeOfAOTRelocations() + SIZEPOINTER + wellKnownClassesOffsetSize;
      uint8_t *relocationDataCursor = self()->setRelocationData(
         fej9->allocateRelocationData(comp, reloBufferSize));
      // set up the size for the region
      *(uintptr_t *)relocationDataCursor = reloBufferSize;
      relocationDataCursor += SIZEPOINTER;

      if (useSVM)
         {
         TR::SymbolValidationManager *svm = comp->getSymbolValidationManager();
         void *offsets = const_cast<void*>(svm->wellKnownClassChainOffsets());
         *(uintptr_t *)relocationDataCursor =
            self()->offsetInSharedCacheFromPointer(fej9->sharedCache(), offsets);
         relocationDataCursor += SIZEPOINTER;
         }

      // set up pointers for each iterated relocation and initialize header
      TR::IteratedExternalRelocation *s;
      for (s = self()->getAOTRelocationTargets().getFirst();
           s != 0;
           s = s->getNext())
         {
         s->setRelocationData(relocationDataCursor);
         s->initializeRelocation(_cg);
         relocationDataCursor += s->getSizeOfRelocationData();
         }
      }
   }

uint8_t *J9::X86::AheadOfTimeCompile::initializeAOTRelocationHeader(TR::IteratedExternalRelocation *relocation)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(_cg->fe());
   TR_SharedCache *sharedCache = fej9->sharedCache();
   TR::Compilation *comp = _cg->comp();
   TR::SymbolValidationManager *symValManager = comp->getSymbolValidationManager();
   uintptr_t numTrampolines;
   TR::LabelSymbol *label;
   TR_ResolvedMethod *resolvedMethod;
   TR_VirtualGuard *guard;
   uint8_t flags = 0;
   uint8_t *cursor = relocation->getRelocationData();

   TR_RelocationRuntime *reloRuntime = comp->reloRuntime();
   TR_RelocationTarget *reloTarget = reloRuntime->reloTarget();

   // size of relocation goes first in all types
   *(uint16_t *)cursor = relocation->getSizeOfRelocationData();
   cursor += 2;
   uint8_t  wideOffsets = relocation->needsWideOffsets() ? RELOCATION_TYPE_WIDE_OFFSET : 0;
   *cursor++ = (uint8_t)relocation->getTargetKind();
   uint8_t *flagsCursor = cursor++;
   *flagsCursor = wideOffsets;
   uint32_t *wordAfterHeader = (uint32_t*)cursor;
#if defined(TR_HOST_64BIT)
   cursor += 4; // padding
#endif

   // This has to be created after the kind has been written into the header
   TR_RelocationRecord storage;
   TR_RelocationRecord *reloRecord = TR_RelocationRecord::create(&storage, reloRuntime, reloTarget, reinterpret_cast<TR_RelocationRecordBinaryTemplate *>(relocation->getRelocationData()));

   switch (relocation->getTargetKind())
      {
      case TR_GlobalValue:
         *(uintptr_t*)cursor = (uintptr_t) relocation->getTargetAddress();
         cursor += SIZEPOINTER;
         break;

      case TR_ArbitraryClassAddress:
         {
         // ExternalRelocation data is as expected for TR_ClassAddress
         auto symRef = (TR::SymbolReference *)relocation->getTargetAddress();
         auto sym = symRef->getSymbol()->castToStaticSymbol();
         auto j9class = (TR_OpaqueClassBlock *)sym->getStaticAddress();
         uintptr_t inlinedSiteIndex = self()->findCorrectInlinedSiteIndex(symRef->getOwningMethod(comp)->constantPool(), (uintptr_t)relocation->getTargetAddress2());

         // Data identifying the class is as though for TR_ClassPointer
         // (TR_RelocationRecordPointerBinaryTemplate)
         *(uintptr_t *)cursor = inlinedSiteIndex;
         cursor += SIZEPOINTER;

         uintptr_t classChainOffsetInSharedCache = sharedCache->getClassChainOffsetOfIdentifyingLoaderForClazzInSharedCache(j9class);
         *(uintptr_t *)cursor = classChainOffsetInSharedCache;
         cursor += SIZEPOINTER;

         cursor = self()->emitClassChainOffset(cursor, j9class);
         }
         break;

      case TR_MethodObject:
      //case TR_InterfaceObject: Shouldn't have branch that create interface object for X86.
         {
         TR_ASSERT((relocation->getTargetKind() != TR_MethodObject), "relocation TR_MethodObject being created");
         TR::SymbolReference *tempSR = (TR::SymbolReference *)relocation->getTargetAddress();
         // next word is the address of the constant pool to which the index refers
         // index in the below stored constant pool that indicates the particular
         // relocation target

         *(uintptr_t *)cursor = (uintptr_t)relocation->getTargetAddress2(); // inlinedSiteIndex
         cursor += SIZEPOINTER;

         *(uintptr_t *)cursor = (uintptr_t)tempSR->getOwningMethod(comp)->constantPool();
         cursor += SIZEPOINTER;
         }
         break;

      case TR_ClassAddress:
         {
         TR::SymbolReference *tempSR = (TR::SymbolReference *)relocation->getTargetAddress();
         uintptr_t inlinedSiteIndex = (uintptr_t)relocation->getTargetAddress2();

         inlinedSiteIndex = self()->findCorrectInlinedSiteIndex(tempSR->getOwningMethod(comp)->constantPool(), inlinedSiteIndex);

         *(uintptr_t *)cursor = inlinedSiteIndex; // inlinedSiteIndex

         cursor += SIZEPOINTER;

         *(uintptr_t *)cursor = (uintptr_t)tempSR->getOwningMethod(comp)->constantPool(); // constantPool
         cursor += SIZEPOINTER;

         *(uintptr_t *)cursor = tempSR->getCPIndex(); // cpIndex
         cursor += SIZEPOINTER;
         }
         break;
      case TR_DataAddress:
         {
         // next word is the address of the constant pool to which the index refers
         TR::SymbolReference *tempSR = (TR::SymbolReference *)relocation->getTargetAddress();
         uintptr_t inlinedSiteIndex = (uintptr_t)relocation->getTargetAddress2();

         inlinedSiteIndex = self()->findCorrectInlinedSiteIndex(tempSR->getOwningMethod(comp)->constantPool(), inlinedSiteIndex);

         // relocation target
         *(uintptr_t *)cursor = inlinedSiteIndex; // inlinedSiteIndex
         //*(uintptr_t *)cursor = inlinedSiteIndex; //(uintptr_t)relocation->getTargetAddress2(); // inlinedSiteIndex

         cursor += SIZEPOINTER;

         *(uintptr_t *)cursor = (uintptr_t)tempSR->getOwningMethod(comp)->constantPool(); // constantPool
         cursor += SIZEPOINTER;

         *(uintptr_t *)cursor = tempSR->getCPIndex(); // cpIndex
         cursor += SIZEPOINTER;

         *(uintptr_t *)cursor = tempSR->getOffset(); // offset
         cursor += SIZEPOINTER;

         }
    break;

      case TR_AbsoluteMethodAddressOrderedPair:
         break;

      case TR_ConstantPoolOrderedPair:
         {
         // Note: thunk relos should only be created for 64 bit
         *(uintptr_t *)cursor = (uintptr_t)relocation->getTargetAddress2(); // inlined site index
         cursor += SIZEPOINTER;

         *(uintptr_t *)cursor = (uintptr_t)relocation->getTargetAddress(); // constantPool
         cursor += SIZEPOINTER;
         }
         break;

      case TR_PicTrampolines:
         {
         TR_ASSERT(comp->target().is64Bit(), "TR_PicTrampolines not supported on 32-bit");
         numTrampolines = (uintptr_t)relocation->getTargetAddress();
         *wordAfterHeader = numTrampolines;
         cursor = (uint8_t*)wordAfterHeader;
         cursor += 4;
         }
        break;

     case TR_J2IThunks:
         {
         // Note: thunk relos should only be created for 64 bit
         // *(uintptr_t *)cursor = (uintptr_t)relocation->getTargetAddress2(); // inlined site index

         TR::Node *node = (TR::Node*)relocation->getTargetAddress();
         TR::SymbolReference *symRef = node->getSymbolReference();

         *(uintptr_t *)cursor = (uintptr_t)node->getInlinedSiteIndex();
         cursor += SIZEPOINTER;

         *(uintptr_t *)cursor = (uintptr_t)symRef->getOwningMethod(comp)->constantPool(); // cp address
         cursor += SIZEPOINTER;


         *(uintptr_t *)cursor = (uintptr_t)symRef->getCPIndex(); // cp index
         cursor += SIZEPOINTER;

         break;
         }

      case TR_HCR:
         {
         *(uintptr_t*)cursor = (uintptr_t)relocation->getTargetAddress();
         cursor += SIZEPOINTER;
         }
         break;

      case TR_DebugCounter:
         {
         TR::DebugCounterBase *counter = (TR::DebugCounterBase *) relocation->getTargetAddress();
         if (!counter || !counter->getReloData() || !counter->getName())
            comp->failCompilation<TR::CompilationException>("Failed to generate debug counter relo data");

         TR::DebugCounterReloData *counterReloData = counter->getReloData();

         uintptr_t offset = (uintptr_t)fej9->sharedCache()->rememberDebugCounterName(counter->getName());

         *(uintptr_t *)cursor = (uintptr_t)counterReloData->_callerIndex;
         cursor += SIZEPOINTER;
         *(uintptr_t *)cursor = (uintptr_t)counterReloData->_bytecodeIndex;
         cursor += SIZEPOINTER;
         *(uintptr_t *)cursor = offset;
         cursor += SIZEPOINTER;
         *(uintptr_t *)cursor = (uintptr_t)counterReloData->_delta;
         cursor += SIZEPOINTER;
         *(uintptr_t *)cursor = (uintptr_t)counterReloData->_fidelity;
         cursor += SIZEPOINTER;
         *(uintptr_t *)cursor = (uintptr_t)counterReloData->_staticDelta;
         cursor += SIZEPOINTER;
         }
         break;

      default:
         // initializeCommonAOTRelocationHeader is currently in the process
         // of becoming the canonical place to initialize the platform agnostic
         // relocation headers; new relocation records' header should be
         // initialized here.
         cursor = self()->initializeCommonAOTRelocationHeader(relocation, reloRecord);
      }
   return cursor;
   }

uint32_t J9::X86::AheadOfTimeCompile::_relocationTargetTypeToHeaderSizeMap[TR_NumExternalRelocationKinds] =
   {
// FIXME this code needs to be cleaned up by having here access to the platform pointer size
//       or by defining in the runtime.hpp the sizes of the relocation items
#if defined (TR_HOST_64BIT)
   24,                                              // TR_ConstantPool                        = 0
   8,                                               // TR_HelperAddress                       = 1
   24,                                              // TR_RelativeMethodAddress               = 2
   8,                                               // TR_AbsoluteMethodAddress               = 3
   40,                                              // TR_DataAddress                         = 4
   24,                                              // TR_ClassObject                         = 5
   24,                                              // TR_MethodObject                        = 6
   24,                                              // TR_InterfaceObject                     = 7
   8,                                               // TR_AbsoluteHelperAddress               = 8
   16,                                              // TR_FixedSeqAddress                     = 9
   16,                                              // TR_FixedSeq2Address                    = 10
   32,                                              // TR_JNIVirtualTargetAddress	      = 11
   32,                                              // TR_JNIStaticTargetAddress              = 12
   4,                                               // Dummy for TR_ArrayCopyHelper           = 13
   4,                                               // Dummy for TR_ArrayCopyToc              = 14
   8,                                               // TR_BodyInfoAddress                     = 15
   24,                                              // TR_Thunks                              = 16
   32,                                              // TR_StaticRamMethodConst                = 17
   24,                                              // TR_Trampolines                         = 18
   8,                                               // TR_PicTrampolines                      = 19
   16,                                              // TR_CheckMethodEnter                    = 20
   8,                                               // TR_RamMethod                           = 21
   16,                                              // TR_RamMethodSequence                   = 22
   16,                                              // TR_RamMethodSequenceReg                = 23
   48,                                              // TR_VerifyClassObjectForAlloc           = 24
   24,                                              // TR_ConstantPoolOrderedPair             = 25
   8,                                               // TR_AbsoluteMethodAddressOrderedPair    = 26
   40,                                              // TR_VerifyRefArrayForAlloc              = 27
   24,                                              // TR_J2IThunks                           = 28
   16,                                              // TR_GlobalValue                         = 29
   4,                                               // dummy for TR_BodyInfoAddress           = 30
   40,                                              // TR_ValidateInstanceField               = 31
   48,                                              // TR_InlinedStaticMethodWithNopGuard     = 32
   48,                                              // TR_InlinedSpecialMethodWithNopGuard    = 33
   48,                                              // TR_InlinedVirtualMethodWithNopGuard    = 34
   48,                                              // TR_InlinedInterfaceMethodWithNopGuard  = 35
   32,                                              // TR_SpecialRamMethodConst               = 36
   48,                                              // TR_InlinedHCRMethod                    = 37
   40,                                              // TR_ValidateStaticField                 = 38
   40,                                              // TR_ValidateClass                       = 39
   32,                                              // TR_ClassAddress                        = 40
   16,                                              // TR_HCR                                 = 41
   64,                                              // TR_ProfiledMethodGuardRelocation       = 42
   64,                                              // TR_ProfiledClassGuardRelocation        = 43
   0,                                               // TR_HierarchyGuardRelocation            = 44
   0,                                               // TR_AbstractGuardRelocation             = 45
   64,                                              // TR_ProfiledInlinedMethodRelocation     = 46
   40,                                              // TR_MethodPointer                       = 47
   32,                                              // TR_ClassPointer                        = 48
   16,                                              // TR_CheckMethodExit                     = 49
   24,                                              // TR_ValidateArbitraryClass              = 50
   0,                                               // TR_EmitClass(not used)                 = 51
   32,                                              // TR_JNISpecialTargetAddress             = 52
   32,                                              // TR_VirtualRamMethodConst               = 53
   40,                                              // TR_InlinedInterfaceMethod              = 54
   40,                                              // TR_InlinedVirtualMethod                = 55
   0,                                               // TR_NativeMethodAbsolute                = 56,
   0,                                               // TR_NativeMethodRelative                = 57,
   32,                                              // TR_ArbitraryClassAddress               = 58,
   56,                                              // TR_DebugCounter                        = 59
   8,                                               // TR_ClassUnloadAssumption               = 60
   32,                                              // TR_J2IVirtualThunkPointer              = 61,
   48,                                              // TR_InlinedAbstractMethodWithNopGuard   = 62,
   0,                                                                  // TR_ValidateRootClass                   = 63,
   sizeof(TR_RelocationRecordValidateClassByNameBinaryTemplate),       // TR_ValidateClassByName                 = 64,
   sizeof(TR_RelocationRecordValidateProfiledClassBinaryTemplate),     // TR_ValidateProfiledClass               = 65,
   sizeof(TR_RelocationRecordValidateClassFromCPBinaryTemplate),       // TR_ValidateClassFromCP                 = 66,
   sizeof(TR_RelocationRecordValidateDefiningClassFromCPBinaryTemplate),//TR_ValidateDefiningClassFromCP         = 67,
   sizeof(TR_RelocationRecordValidateClassFromCPBinaryTemplate),       // TR_ValidateStaticClassFromCP           = 68,
   0,                                                                  // TR_ValidateClassFromMethod             = 69,
   0,                                                                  // TR_ValidateComponentClassFromArrayClass= 70,
   sizeof(TR_RelocationRecordValidateArrayFromCompBinaryTemplate),     // TR_ValidateArrayClassFromComponentClass= 71,
   sizeof(TR_RelocationRecordValidateSuperClassFromClassBinaryTemplate),//TR_ValidateSuperClassFromClass         = 72,
   sizeof(TR_RelocationRecordValidateClassInstanceOfClassBinaryTemplate),//TR_ValidateClassInstanceOfClass       = 73,
   sizeof(TR_RelocationRecordValidateSystemClassByNameBinaryTemplate), //TR_ValidateSystemClassByName            = 74,
   sizeof(TR_RelocationRecordValidateClassFromCPBinaryTemplate),       //TR_ValidateClassFromITableIndexCP       = 75,
   sizeof(TR_RelocationRecordValidateClassFromCPBinaryTemplate),       //TR_ValidateDeclaringClassFromFieldOrStatic=76,
   0,                                                                  // TR_ValidateClassClass                  = 77,
   sizeof(TR_RelocationRecordValidateSuperClassFromClassBinaryTemplate),//TR_ValidateConcreteSubClassFromClass   = 78,
   sizeof(TR_RelocationRecordValidateClassChainBinaryTemplate),        // TR_ValidateClassChain                  = 79,
   0,                                                                  // TR_ValidateRomClass                    = 80,
   0,                                                                  // TR_ValidatePrimitiveClass              = 81,
   0,                                                                  // TR_ValidateMethodFromInlinedSite       = 82,
   0,                                                                  // TR_ValidatedMethodByName               = 83,
   sizeof(TR_RelocationRecordValidateMethodFromClassBinaryTemplate),   // TR_ValidatedMethodFromClass            = 84,
   sizeof(TR_RelocationRecordValidateMethodFromCPBinaryTemplate),      // TR_ValidateStaticMethodFromCP          = 85,
   sizeof(TR_RelocationRecordValidateMethodFromCPBinaryTemplate),      //TR_ValidateSpecialMethodFromCP         = 86,
   sizeof(TR_RelocationRecordValidateMethodFromCPBinaryTemplate),      //TR_ValidateVirtualMethodFromCP         = 87,
   sizeof(TR_RelocationRecordValidateVirtualMethodFromOffsetBinaryTemplate),//TR_ValidateVirtualMethodFromOffset = 88,
   sizeof(TR_RelocationRecordValidateInterfaceMethodFromCPBinaryTemplate),//TR_ValidateInterfaceMethodFromCP     = 89,
   sizeof(TR_RelocationRecordValidateMethodFromClassAndSigBinaryTemplate),//TR_ValidateMethodFromClassAndSig     = 90,
   sizeof(TR_RelocationRecordValidateStackWalkerMaySkipFramesBinaryTemplate),//TR_ValidateStackWalkerMaySkipFramesRecord= 91,
   0,                                                                  // TR_ValidateArrayClassFromJavaVM        = 92,
   sizeof(TR_RelocationRecordValidateClassInfoIsInitializedBinaryTemplate),//TR_ValidateClassInfoIsInitialized   = 93,
   sizeof(TR_RelocationRecordValidateMethodFromSingleImplBinaryTemplate),//TR_ValidateMethodFromSingleImplementer= 94,
   sizeof(TR_RelocationRecordValidateMethodFromSingleInterfaceImplBinaryTemplate),//TR_ValidateMethodFromSingleInterfaceImplementer= 95,
   sizeof(TR_RelocationRecordValidateMethodFromSingleAbstractImplBinaryTemplate),//TR_ValidateMethodFromSingleAbstractImplementer= 96,
   sizeof(TR_RelocationRecordValidateMethodFromCPBinaryTemplate),      //TR_ValidateImproperInterfaceMethodFromCP= 97,
   sizeof(TR_RelocationRecordSymbolFromManagerBinaryTemplate),         // TR_SymbolFromManager = 98,
   sizeof(TR_RelocationRecordMethodCallAddressBinaryTemplate),         // TR_MethodCallAddress                   = 99,
   sizeof(TR_RelocationRecordSymbolFromManagerBinaryTemplate),         // TR_DiscontiguousSymbolFromManager = 100,
   sizeof(TR_RelocationRecordResolvedTrampolinesBinaryTemplate),       // TR_ResolvedTrampolines = 101,
   sizeof(TR_RelocationRecordBlockFrequencyBinaryTemplate),            // TR_BlockFrequency = 102,
   sizeof(TR_RelocationRecordRecompQueuedFlag),                        // TR_RecompQueuedFlag = 103
#else

   12,                                              // TR_ConstantPool                        = 0
   8,                                               // TR_HelperAddress                       = 1
   12,                                              // TR_RelativeMethodAddress               = 2
   4,                                               // TR_AbsoluteMethodAddress               = 3
   20,                                              // TR_DataAddress                         = 4
   12,                                              // TR_ClassObject                         = 5
   12,                                              // TR_MethodObject                        = 6
   12,                                              // TR_InterfaceObject                     = 7
   8,                                               // TR_AbsoluteHelperAddress               = 8
   8,                                               // TR_FixedSeqAddress                     = 9
   8,                                               // TR_FixedSeq2Address                    = 10
   16,                                              // TR_JNIVirtualTargetAddress             = 11
   16,                                              // TR_JNIStaticTargetAddress              = 12
   4,                                               // Dummy for TR_ArrayCopyHelper           = 13
   4,                                               // Dummy for TR_ArrayCopyToc              = 14
   4,                                               // TR_BodyInfoAddress                     = 15
   12,                                              // TR_Thunks                              = 16
   16,                                              // TR_StaticRamMethodConst                = 17
   12,                                              // TR_Trampolines                         = 18
   8,                                               // TR_PicTrampolines                      = 19
   8,                                               // TR_CheckMethodEnter                    = 20
   4,                                               // TR_RamMethod                           = 21
   8,                                               // TR_RamMethodSequence                   = 22
   8,                                               // TR_RamMethodSequenceReg                = 23
   24,                                              // TR_VerifyClassObjectForAlloc           = 24
   12,                                              // TR_ConstantPoolOrderedPair             = 25
   8,                                               // TR_AbsoluteMethodAddressOrderedPair    = 26
   20,                                              // TR_VerifyRefArrayForAlloc              = 27
   12,                                              // TR_J2IThunks                           = 28
   8,                                               // TR_GlobalValue                         = 29
   4,                                               // TR_BodyInfoAddressLoad                 = 30
   20,                                              // TR_ValidateInstanceField               = 31
   24,                                              // TR_InlinedStaticMethodWithNopGuard     = 32
   24,                                              // TR_InlinedSpecialMethodWithNopGuard    = 33
   24,                                              // TR_InlinedVirtualMethodWithNopGuard    = 34
   24,                                              // TR_InlinedInterfaceMethodWithNopGuard  = 35
   16,                                              // TR_SpecialRamMethodConst               = 36
   24,                                              // TR_InlinedHCRMethod                    = 37
   20,                                              // TR_ValidateStaticField                 = 38
   20,                                              // TR_ValidateClass                       = 39
   16,                                              // TR_ClassAddress                        = 40
   8,                                               // TR_HCR                                 = 41
   32,                                              // TR_ProfiledMethodGuardRelocation       = 42
   32,                                              // TR_ProfiledClassGuardRelocation        = 43
   0,                                               // TR_HierarchyGuardRelocation            = 44
   0,                                               // TR_AbstractGuardRelocation             = 45
   32,                                              // TR_ProfiledInlinedMethodRelocation     = 46
   20,                                              // TR_MethodPointer                       = 47
   16,                                              // TR_ClassPointer                        = 48
   8,                                               // TR_CheckMethodExit                     = 49
   12,                                              // TR_ValidateArbitraryClass              = 50
   0,                                               // TR_EmitClass(not used)                 = 51
   16,                                              // TR_JNISpecialTargetAddress             = 52
   16,                                              // TR_VirtualRamMethodConst               = 53
   20,                                              // TR_InlinedInterfaceMethod              = 54
   20,                                              // TR_InlinedVirtualMethod                = 55
   0,                                               // TR_NativeMethodAbsolute                = 56,
   0,                                               // TR_NativeMethodRelative                = 57,
   16,                                              // TR_ArbitraryClassAddress               = 58,
   28,                                               // TR_DebugCounter                        = 59
   4,                                               // TR_ClassUnloadAssumption               = 60
   16,                                              // TR_J2IVirtualThunkPointer              = 61,
   24,                                              // TR_InlinedAbstractMethodWithNopGuard   = 62,
#endif
   };
