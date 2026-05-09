// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassSettings)

// =============================================================================================
// MassSettings.cpp —— UMassSettings / UMassModuleSettings 的实现
// ---------------------------------------------------------------------------------------------
// 重点关注两件事：
//   1) UMassModuleSettings::PostInitProperties() 中的"自注册"机制，是 Mass 插件式扩展的核心；
//   2) UMassSettings::RegisterModuleSettings() 在编辑器下会读取 DisplayName meta 作为
//      Map 的 Key，从而让 UI 中的子页面标题更友好（例如 "Mass Entity" 而非 "MassEntitySettings"）。
// =============================================================================================


//----------------------------------------------------------------------//
//  UMassModuleSettings
//----------------------------------------------------------------------//
void UMassModuleSettings::PostInitProperties()
{
	// 必须先调用基类，让 UObject 完成 config 反序列化等标准动作
	Super::PostInitProperties();

	// 仅当当前对象是 *CDO*（Class Default Object）且类本身不是 Abstract 时才注册：
	//   - 非 CDO（普通实例）注册没有意义，UI 只展示 CDO；
	//   - Abstract 基类（例如本类自己）的 CDO 不应当作子页面出现在 UI 上。
	// 注意：派生类在 .h 中通过 UCLASS(Abstract) 显式标注后，CLASS_Abstract 才会被设置。
	if (HasAnyFlags(RF_ClassDefaultObject) && !GetClass()->HasAnyClassFlags(CLASS_Abstract))
	{
		// register with UMassGameplaySettings
		// 中文：把自己塞进 UMassSettings（Project Settings 中的 Mass 父页面）的 ModuleSettings 容器。
		// 由于 UMassSettings 也是 CDO 唯一存在的（DeveloperSettings 单例），这里使用
		// GetMutableDefault 拿到全局唯一的那个 CDO 指针。
		GetMutableDefault<UMassSettings>()->RegisterModuleSettings(*this);
	}
}

//----------------------------------------------------------------------//
//  UMassSettings
//----------------------------------------------------------------------//
void UMassSettings::RegisterModuleSettings(UMassModuleSettings& SettingsCDO)
{
	// 防御式断言：传入的对象必须是 CDO；非 CDO 注册到全局 UI 是无意义的。
	ensureMsgf(SettingsCDO.HasAnyFlags(RF_ClassDefaultObject), TEXT("Registered ModuleSettings need to be its class's CDO"));

	// we should consider a replacement in case we're hot-reloading
	// 中文 TODO 提醒：热重载（Live Coding）时旧 CDO 可能仍在 Map 中，理想情况下应替换而非新增。
	// 当前实现仅做覆盖（FindOrAdd 后再赋值），不显式释放旧条目。

	// 默认 Key：用类的 FName（例如 "MassEntitySettings"）。
	FName EntryName = SettingsCDO.GetClass()->GetFName();

#if WITH_EDITOR
	// 编辑器下尝试读取 UCLASS(DisplayName="...") 元数据作为更友好的 Key。
	// 例如 UMassEntitySettings 标注了 DisplayName="Mass Entity"，则 EntryName = "Mass Entity"。
	// runtime 下 meta data 可能不存在 / 已被裁剪，所以保留类名 fallback。
	static const FName DisplayNameMeta(TEXT("DisplayName")); 
	// try reading better name from meta data, available only in editor. Besides, we don't really care about this out 
	// side of editor. We could even skip populating ModuleSettings but we'll leave it as is for now.
	const FString& DisplayNameValue = SettingsCDO.GetClass()->GetMetaData(DisplayNameMeta);
	if (DisplayNameValue.Len())
	{
		EntryName = *DisplayNameValue;
	}
#endif // WITH_EDITOR

	// FindOrAdd 语义：若不存在则插入并返回引用；若已存在则返回已有引用。
	// 不论哪种情况，我们都用最新的 CDO 指针覆盖之，相当于"以最后一次注册为准"，
	// 简单地解决了热重载/重复注册的歧义。
	TObjectPtr<UMassModuleSettings>& FoundModuleEntry = ModuleSettings.FindOrAdd(EntryName, &SettingsCDO);
	FoundModuleEntry = &SettingsCDO;
}
