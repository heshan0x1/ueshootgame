// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "iree/compiler/Codegen/Dialect/GPU/IR/IREEGPUAttrs.h"
#include "llvm/ADT/StringRef.h"

namespace mlir::iree_compiler::IREE::GPU::UnrealShaderTargetUtils {

// Returns a TargetAttr to describe the details of the given |target|, which can
// be a product name like "rtx3090"/"mali-g710"/"adreno" or an microarchitecture
// name like "ampere"/"valhall". Returns a null TargetAttr if the given |target|
// is not recognized.
TargetAttr getTargetDetails(llvm::StringRef target, MLIRContext *context);

} // namespace mlir::iree_compiler::IREE::GPU::UnrealShaderTargetUtils
