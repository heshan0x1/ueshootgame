// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using EpicGames.Core;
using EpicGames.UHT.Exporters.CodeGen;
using EpicGames.UHT.Types;

namespace EpicGames.UHT.Utils
{
	/// <summary>
	/// Interface used to collect all the objects referenced by a given type.
	/// Not all types such as UhtPackage and UhtHeaderFile support collecting
	/// references due to assorted reasons.
	/// </summary>
	public interface IUhtReferenceCollector
	{

		/// <summary>
		/// Add a cross module reference to a given object type.
		/// </summary>
		/// <param name="obj">Object/type being referenced</param>
		/// <param name="type">Type of reference required</param>
		void AddCrossModuleReference(UhtObject? obj, UhtSingletonType type);

		/// <summary>
		/// Add an object declaration
		/// </summary>
		/// <param name="obj">Object in question</param>
		/// <param name="type">Type of reference required</param>
		void AddDeclaration(UhtObject obj, UhtSingletonType type);

		/// <summary>
		/// Add a field as a singleton for exporting
		/// </summary>
		/// <param name="field">Field to be added</param>
		void AddSingleton(UhtField field);

		/// <summary>
		/// Add a field as a type being exported
		/// </summary>
		/// <param name="field">Field to be added</param>
		void AddExportType(UhtField field);

		/// <summary>
		/// Add a forward declaration.  This is the preferred way of adding a forward declaration since the exporter will
		/// handle all the formatting requirements.
		/// </summary>
		/// <param name="field"></param>
		void AddForwardDeclaration(UhtField field);

		/// <summary>
		/// Add a forward declaration.  The string can contain multiple declarations but must only exist on one line.
		/// </summary>
		/// <param name="namespaceObj">The namespace to place the string</param>
		/// <param name="declaration">The declarations to add</param>
		void AddForwardDeclaration(UhtNamespace? namespaceObj, string? declaration);
	}

	/// <summary>
	/// Maintains a list of referenced object indices.
	/// </summary>
	public class UhtUniqueReferenceCollection
	{
		private const int Shift = 2;
		private const int Mask = 0x3;

		/// <summary>
		/// Returns whether there is anything in this collection
		/// </summary>
		public bool IsEmpty => Uniques.Count == 0;

		/// <summary>
		/// List of all unique reference keys.  Use UngetKey to get the object index and the flag.
		/// </summary>
		private HashSet<int> Uniques { get; } = new HashSet<int>();

		/// <summary>
		/// Return an encoded key that represents the object and type of reference.
		/// If the object has the alternate object set (i.e. native interfaces), then
		/// that object's index is used to generate the key.
		/// </summary>
		/// <param name="obj">Object being referenced</param>
		/// <param name="type">The type of reference required for linking.</param>
		/// <returns>Integer key value.</returns>
		public static int GetKey(UhtObject obj, UhtSingletonType type)
		{
			return obj.AlternateObject != null ? GetKey(obj.AlternateObject, type) : (obj.ObjectTypeIndex << Shift) | ((int)type);
		}

		/// <summary>
		/// Given a key, return the object index and registered flag
		/// </summary>
		/// <param name="key">The key in question</param>
		public static (int objectIndex, UhtSingletonType type) UngetKey(int key)
		{
			return (key >> Shift, (UhtSingletonType)(key & Mask));
		}

		/// <summary>
		/// Add the given object to the references
		/// </summary>
		/// <param name="obj">Object to be added</param>
		/// <param name="type">Type of reference required</param>
		public void Add(UhtObject? obj, UhtSingletonType type)
		{
			if (obj != null)
			{
				Uniques.Add(GetKey(obj, type));
			}
		}

		/// <summary>
		/// Return the collection of references sorted by the API string returned by the delegate.
		/// </summary>
		/// <param name="referenceToString">Function to invoke to return the requested object API string, taking the object index and the UhtSingletonType.</param>
		/// <returns>Read only memory region of all the string.</returns>
		public IReadOnlyList<string> GetSortedReferences(Func<int, UhtSingletonType, string?> referenceToString)
		{
			// Collect the unsorted array
			List<string> sorted = new(Uniques.Count);
			foreach(int key in Uniques)
			{
				(int objectIndex, UhtSingletonType type) = UngetKey(key);
				if (referenceToString(objectIndex, type) is string decl)
				{
					sorted.Add(decl);
				}
			}

			// Sort the array
			sorted.Sort(StringComparerUE.OrdinalIgnoreCase);

			// Remove duplicates.  In some instances the different keys might return the same string.
			// This removes those duplicates
			if (sorted.Count > 1)
			{
				int priorOut = 0;
				for (int index = 1; index < sorted.Count; ++index)
				{
					if (sorted[index] != sorted[priorOut])
					{
						++priorOut;
						sorted[priorOut] = sorted[index];
					}
				}

				if (priorOut < sorted.Count - 1)
				{
					sorted.RemoveRange(priorOut + 1, sorted.Count - priorOut - 1);
				}
			}
			return sorted;
		}
	}

	/// <summary>
	/// Standard implementation of the reference collector interface
	/// </summary>
	public class UhtReferenceCollector : IUhtReferenceCollector
	{

		/// <summary>
		/// Collection of unique cross module references
		/// </summary>
		public UhtUniqueReferenceCollection CrossModule { get; set; } = new();

		/// <summary>
		/// Collection of unique declarations
		/// </summary>
		public UhtUniqueReferenceCollection Declaration { get; set; } = new();

		/// <summary>
		/// Collection of singletons
		/// </summary>
		public List<UhtField> Singletons { get; } = [];

		/// <summary>
		/// Collection of types to export
		/// </summary>
		public List<UhtField> ExportTypes { get; } = [];

		/// <summary>
		/// Collection of fields needing forward declarations
		/// </summary>
		public HashSet<UhtField> ForwardDeclarations { get; } = [];

		/// <summary>
		/// Collection of forward declarations in text form
		/// </summary>
		public HashSet<string> ForwardDeclarationStrings { get; } = [];

		/// <summary>
		/// Collection of referenced headers
		/// </summary>
		public HashSet<UhtHeaderFile> ReferencedHeaders { get; } = [];

		/// <inheritdoc/>
		public void AddCrossModuleReference(UhtObject? obj, UhtSingletonType type)
		{
			CrossModule.Add(obj, type);
			if (obj != null && obj is not UhtPackage && type == UhtSingletonType.Registered)
			{
				ReferencedHeaders.Add(obj.HeaderFile);
			}
		}

		/// <inheritdoc/>
		public void AddDeclaration(UhtObject obj, UhtSingletonType type)
		{
			Declaration.Add(obj, type);
		}

		/// <inheritdoc/>
		public void AddSingleton(UhtField field)
		{
			Singletons.Add(field);
		}

		/// <inheritdoc/>
		public void AddExportType(UhtField field)
		{
			ExportTypes.Add(field);
		}

		/// <inheritdoc/>
		public void AddForwardDeclaration(UhtField field)
		{
			ForwardDeclarations.Add(field);
		}

		/// <inheritdoc/>
		public void AddForwardDeclaration(UhtNamespace? namespaceObj, string? declaration)
		{
			if (!String.IsNullOrEmpty(declaration))
			{
				if (namespaceObj == null || namespaceObj.IsGlobal)
				{
					ForwardDeclarationStrings.Add(declaration);
				}
				else
				{
					// This is hardly used so just wrap the declaration in the namespace instead of 
					// trying to group them all together in a multiple line namespace declaration.
					StringBuilder builder = new();
					namespaceObj.AppendSingleLine(builder, (builder) => builder.Append(declaration));
				}
			}
		}
	}
}
