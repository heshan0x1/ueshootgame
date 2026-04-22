// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealShaderTargetUtils.h"

#include "iree/compiler/Codegen/Dialect/GPU/TargetUtils/KnownTargets.h"

namespace mlir::iree_compiler::IREE::GPU::UnrealShaderTargetUtils
{

TargetAttr getTargetDetails(llvm::StringRef target, MLIRContext *context)
{
	const TargetAttr targetAttr = mlir::iree_compiler::IREE::GPU::getVulkanTargetDetails(target, context);
	const ComputeBitwidths computeBits = targetAttr.getWgp().getCompute().getValue() & ~ComputeBitwidths::Int8;
	const StorageBitwidths storageBits = targetAttr.getWgp().getStorage().getValue() & ~StorageBitwidths::B8;

	// from Engine\Source\ThirdParty\ShaderConductor\ShaderConductor\External\DirectXShaderCompiler\include\dxc\DXIL\DxilConstants.h
	const int32_t maxWorkgroupMemoryBytes = 8192 * 4; // const unsigned kMaxTGSMSize = 8192 * 4;

	TargetWgpAttr wgp = TargetWgpAttr::get(context, 
		ComputeBitwidthsAttr::get(context, computeBits), 
		StorageBitwidthsAttr::get(context, storageBits), 
		targetAttr.getWgp().getSubgroup(),
		targetAttr.getWgp().getDot(),
		targetAttr.getWgp().getMma(),
		targetAttr.getWgp().getSubgroupSizeChoices(),
		targetAttr.getWgp().getMaxWorkgroupSizes(),
		targetAttr.getWgp().getMaxThreadCountPerWorkgroup(),
		maxWorkgroupMemoryBytes,
		targetAttr.getWgp().getMaxWorkgroupCounts(),
		targetAttr.getWgp().getMaxLoadInstructionBits(),
		targetAttr.getWgp().getSimdsPerWgp(),
		targetAttr.getWgp().getVgprSpaceBits(),
		targetAttr.getWgp().getExtra()
	);

	return TargetAttr::get(context, targetAttr.getArch(), targetAttr.getFeatures(), wgp, targetAttr.getChip());
}

} // namespace mlir::iree_compiler::IREE::GPU::UnrealShaderTargetUtils
