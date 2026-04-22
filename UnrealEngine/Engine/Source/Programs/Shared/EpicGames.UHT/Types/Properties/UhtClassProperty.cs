// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Types
{
	/// <summary>
	/// FClassProperty
	/// </summary>
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "ClassProperty", IsProperty = true)]
	public class UhtClassProperty : UhtObjectProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName => "ClassProperty";

		/// <summary>
		/// Construct a new property
		/// </summary>
		/// <param name="propertySettings">Property settings</param>
		/// <param name="cppForm">Source code form of the property</param>
		/// <param name="referencedClass">Referenced class</param>
		/// <param name="extraFlags">Extra flags to apply to the property.</param>
		public UhtClassProperty(UhtPropertySettings propertySettings, UhtObjectCppForm cppForm, UhtClass referencedClass, EPropertyFlags extraFlags = EPropertyFlags.None)
			: base(propertySettings, cppForm, referencedClass, extraFlags, 0)
		{
			if (!cppForm.IsValidForClassProperty())
			{
				throw new UhtIceException($"Improper UhtObjectCppForm.{cppForm} for an UhtClassProperty");
			}
			PropertyCaps |= UhtPropertyCaps.CanHaveConfig;
			PropertyCaps &= ~(UhtPropertyCaps.CanBeInstanced);
		}

		/// <inheritdoc/>
		public override void CollectReferencesInternal(IUhtReferenceCollector collector, bool addForwardDeclarations, bool isTemplateProperty)
		{
			base.CollectReferencesInternal(collector, addForwardDeclarations, isTemplateProperty);
			if (addForwardDeclarations && MetaClass != null)
			{
				collector.AddForwardDeclaration(MetaClass);
			}
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder builder, UhtPropertyTextType textType, bool isTemplateArgument)
		{
			switch (textType)
			{
				case UhtPropertyTextType.Generic:
				case UhtPropertyTextType.Sparse:
				case UhtPropertyTextType.SparseShort:
				case UhtPropertyTextType.GenericFunctionArgOrRetVal:
				case UhtPropertyTextType.GenericFunctionArgOrRetValImpl:
				case UhtPropertyTextType.ClassFunctionArgOrRetVal:
				case UhtPropertyTextType.EventFunctionArgOrRetVal:
				case UhtPropertyTextType.InterfaceFunctionArgOrRetVal:
				case UhtPropertyTextType.ExportMember:
				case UhtPropertyTextType.Construction:
				case UhtPropertyTextType.RigVMType:
				case UhtPropertyTextType.GetterSetterArg:
					AppendTemplateType(builder);
					break;

				case UhtPropertyTextType.EventParameterMember:
				case UhtPropertyTextType.EventParameterFunctionMember:
					if (CppForm != UhtObjectCppForm.NativeClass)
					{
						AppendTemplateType(builder);
					}
					else 
					{
						builder.Append("UClass*");
					}
					break;

				case UhtPropertyTextType.FunctionThunkRetVal:
					if (PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm))
					{
						builder.Append("const ");
					}
					if (CppForm != UhtObjectCppForm.NativeClass)
					{
						AppendTemplateType(builder);
					}
					else
					{
						builder.Append("UClass*");
					}
					break;

				case UhtPropertyTextType.FunctionThunkParameterArgType:
					if ((PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm) && CppForm != UhtObjectCppForm.NativeClass) || isTemplateArgument)
					{
						AppendTemplateType(builder);
					}
					else
					{
						builder.Append(Class.Namespace.FullSourceName).Append(Class.SourceName);
					}
					break;

				case UhtPropertyTextType.FunctionThunkParameterArrayType:
					AppendTemplateType(builder);
					break;

				case UhtPropertyTextType.VerseMangledType:
					AppendVerseMangledType(builder, ReferencedClass);
					break;
			}
			return builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, int tabs)
		{
			return AppendMemberDecl(builder, context, name, nameSuffix, tabs, IsVerseClassType(CppForm) ? "FVerseClassPropertyParams" : "FClassPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, string? offset, int tabs)
		{
			string paramsGenFlags = CppForm switch
			{
				UhtObjectCppForm.NativeClass or
				UhtObjectCppForm.TSoftClassPtr or
				UhtObjectCppForm.TSubclassOf
					=> "UECodeGen_Private::EPropertyGenFlags::Class",

				UhtObjectCppForm.TObjectPtrClass
					=> "UECodeGen_Private::EPropertyGenFlags::Class | UECodeGen_Private::EPropertyGenFlags::ObjectPtr",

				UhtObjectCppForm.VerseType or
				UhtObjectCppForm.VerseSubtype or
				UhtObjectCppForm.VerseCastableType or
				UhtObjectCppForm.VerseCastableSubtype or
				UhtObjectCppForm.VerseConcreteType or
				UhtObjectCppForm.VerseConcreteSubtype
					=> "UECodeGen_Private::EPropertyGenFlags::VerseType",

				UhtObjectCppForm.NativeObject or
				UhtObjectCppForm.TObjectPtrObject or
				UhtObjectCppForm.TSoftObjectPtr or
				UhtObjectCppForm.TInterfaceInstance or
				_
					=> throw new NotImplementedException(),
			};

			bool isVerseClassTypeProperty = IsVerseClassType(CppForm);
			AppendMemberDefStart(builder, context, name, nameSuffix, offset, tabs, isVerseClassTypeProperty ? "FVerseClassPropertyParams" : "FClassPropertyParams", paramsGenFlags);
			AppendMemberDefRef(builder, context, Class, Exporters.CodeGen.UhtSingletonType.Unregistered);
			AppendMemberDefRef(builder, context, MetaClass, Exporters.CodeGen.UhtSingletonType.Unregistered);

			if (isVerseClassTypeProperty)
			{
				string requiresConcreteValue = CppForm == UhtObjectCppForm.VerseConcreteType || CppForm == UhtObjectCppForm.VerseConcreteSubtype ? "true" : "false";
				string requiresCastableValue = CppForm == UhtObjectCppForm.VerseCastableType || CppForm == UhtObjectCppForm.VerseCastableSubtype ? "true" : "false";
				builder.Append($"{requiresConcreteValue}, ");
				builder.Append($"{requiresCastableValue}, ");
			}

			AppendMemberDefEnd(builder, context, name, nameSuffix);
			return builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendConstInitMemberDef(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, Action<StringBuilder>? outerFunc, string? offset, int tabs)
		{
			AppendConstInitMemberDefStart(builder, context, name, nameSuffix, outerFunc, offset, tabs);
			AppendMemberDefRef(builder, context, Class, Exporters.CodeGen.UhtSingletonType.ConstInit);
			AppendMemberDefRef(builder, context, MetaClass, Exporters.CodeGen.UhtSingletonType.ConstInit);
			AppendConstInitMemberDefEnd(builder, context);
			return builder;
		}

		private static bool IsVerseClassType(UhtObjectCppForm cppForm)
		{
			return cppForm == UhtObjectCppForm.VerseConcreteType
				|| cppForm == UhtObjectCppForm.VerseConcreteSubtype
				|| cppForm == UhtObjectCppForm.VerseCastableType
				|| cppForm == UhtObjectCppForm.VerseCastableSubtype
				|| cppForm == UhtObjectCppForm.VerseType
				|| cppForm == UhtObjectCppForm.VerseSubtype;

		}

		/// <inheritdoc/>
		public override void Validate(UhtStruct outerStruct, UhtProperty outermostProperty, UhtValidationOptions options)
		{
			base.Validate(outerStruct, outermostProperty, options);

			// UFunctions with a smart pointer as input parameter wont compile anyway, because of missing P_GET_... macro.
			// UFunctions with a smart pointer as return type will crash when called via blueprint, because they are not supported in VM.
			if (!options.HasAnyFlags(UhtValidationOptions.IsKey) && PropertyCategory != UhtPropertyCategory.Member && CppForm == UhtObjectCppForm.TObjectPtrClass)
			{
				outerStruct.LogError("UFunctions cannot take a TObjectPtr as a function parameter or return value.");
			}
		}
	}
}
