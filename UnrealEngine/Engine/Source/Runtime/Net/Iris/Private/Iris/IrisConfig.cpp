// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// IrisConfig.cpp —— IrisConfig.h 中三个全局开关函数 + Iris CSV 类别的实现
// -----------------------------------------------------------------------------
// 设计要点：
//   1) 用一个静态 int32 + FAutoConsoleVariableRef 实现 CVar 绑定，使得
//      net.Iris.UseIrisReplication 既可在 ini / 命令行 / 控制台修改，也可被
//      C++ 代码（SetUseIrisReplication）原子写入；
//   2) 命令行解析仅识别 `-UseIrisReplication=N` 这一种形式：
//        N>0  -> 强制选 Iris；
//        N<=0 -> 强制选 Generic（旧复制）；
//        未指定 -> 返回 EReplicationSystem::Default，表示「不覆盖、用引擎默认」。
//      `-LegacyReplication` 这种「无值」开关由 FIrisCoreModule 调用方按需自行
//      处理（见 IrisCoreModule.cpp 注释），本文件保持职责单一。
//   3) 文件末尾 CSV_DEFINE_CATEGORY(Iris, WITH_SERVER_CODE) 在「服务器构建」
//      下默认启用 Iris CSV 类别，便于线上服务采集复制系统性能。
// =============================================================================

#include "Iris/IrisConfig.h"
#include "Iris/Core/IrisCsv.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace UE::Net
{

// CVar 后端整型存储；0 = 关闭 Iris（走旧 Replication），>0 = 启用 Iris。
// 默认 0：保守起见，必须显式打开才使用 Iris。
static int32 CVarUseIrisReplication = 0;
// 把上述变量挂到「net.Iris.UseIrisReplication」这一控制台名字上，使其可在
// .ini / 命令行 -ExecCmds / 编辑器控制台修改。ECVF_Default 表示无特殊 flag。
static FAutoConsoleVariableRef CVarUseIrisReplicationRef(TEXT("net.Iris.UseIrisReplication"), CVarUseIrisReplication, TEXT("Enables Iris replication system. 0 will fallback to legacy replicationsystem."), ECVF_Default );

// IrisConfig.h 三个对外函数的实现：

// 读 CVar：>0 即视为「应启用 Iris」。
// 调用方通常是 UNetDriver::IsUsingIrisReplication()。
bool ShouldUseIrisReplication()
{
	return CVarUseIrisReplication > 0;
}

// 写 CVar；调用方负责决定时机。FIrisCoreModule::StartupModule() 在解析完
// 命令行 -UseIrisReplication=N 后会调用本函数把命令行意图固化到 CVar，
// 这一步必须发生在 NetDriver 创建之前。
void SetUseIrisReplication(bool EnableIrisReplication)
{
	CVarUseIrisReplication = EnableIrisReplication ? 1 : 0;
}

// 解析命令行 -UseIrisReplication=N。
// 返回值含义见 IrisConfig.h；本函数本身不修改 CVar，仅返回「命令行意图」，
// 由调用方决定是否覆盖 CVar（见 FIrisCoreModule::StartupModule()）。
EReplicationSystem GetUseIrisReplicationCmdlineValue()
{
	int32 UseIrisReplication=0;
	if (FParse::Value(FCommandLine::Get(), TEXT("UseIrisReplication="), UseIrisReplication))
	{
		// Try to force the requested system if the cmdline is present
		// 命令行存在 -UseIrisReplication=N：N>0 -> Iris，N<=0 -> Generic（旧）。
		return UseIrisReplication > 0 ? EReplicationSystem::Iris : EReplicationSystem::Generic;
	}

	// Use the default engine value
	// 命令行未指定时返回 Default，表示「不覆盖 CVar，让引擎默认 / ini 决策生效」。
	return EReplicationSystem::Default;
}

}


// Enable Iris category by default on servers
// 注册 Iris 这个 CSV Profiler 类别。WITH_SERVER_CODE 控制是否「默认启用」：
// 服务器构建默认 ON（线上需要监控 Iris 复制性能）；
// 客户端构建默认 OFF，需要时通过控制台 csv.Iris 1 显式打开。
CSV_DEFINE_CATEGORY(Iris, WITH_SERVER_CODE);
