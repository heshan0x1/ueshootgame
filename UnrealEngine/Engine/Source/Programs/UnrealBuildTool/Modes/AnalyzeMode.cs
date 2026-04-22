// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace UnrealBuildTool.Modes
{
	/// <summary>
	/// Outputs information about the given target, including a module dependecy graph (in .gefx format and list of module references)
	/// </summary>
	[ToolMode("Analyze", ToolModeOptions.XmlConfig | ToolModeOptions.BuildPlatforms | ToolModeOptions.SingleInstance | ToolModeOptions.StartPrefetchingEngine | ToolModeOptions.ShowExecutionTime)]
	class AnalyzeMode : ToolMode
	{
		/// <summary>
		/// Execute the command
		/// </summary>
		/// <param name="Arguments">Command line arguments</param>
		/// <returns>Exit code</returns>
		/// <param name="Logger"></param>
		public override Task<int> ExecuteAsync(CommandLineArguments Arguments, ILogger Logger)
		{
			Arguments.ApplyTo(this);

			// Create the build configuration object, and read the settings
			BuildConfiguration BuildConfiguration = new();
			XmlConfig.ApplyTo(BuildConfiguration);
			Arguments.ApplyTo(BuildConfiguration);

			// Parse all the target descriptors
			List<TargetDescriptor> TargetDescriptors = TargetDescriptor.ParseCommandLine(Arguments, BuildConfiguration, Logger);

			// Generate the compile DB for each target
			using (ISourceFileWorkingSet WorkingSet = new EmptySourceFileWorkingSet())
			{
				// Find the compile commands for each file in the target
				Dictionary<FileReference, string> FileToCommand = [];
				foreach (TargetDescriptor TargetDescriptor in TargetDescriptors)
				{
					AnalyzeTarget(TargetDescriptor, BuildConfiguration, Logger);
				}
			}

			return Task.FromResult(0);
		}

		class ModuleInfo
		{
			public UEBuildModule Module;
			public string[] IncludeChain;
			public HashSet<UEBuildModule> InwardRefs = [];
			public HashSet<UEBuildModule> UniqueInwardRefs = [];
			public HashSet<UEBuildModule> OutwardRefs = [];
			public HashSet<UEBuildModule> UniqueOutwardRefs = [];

			public List<FileReference> ObjectFiles = [];
			public long ObjSize = 0;
			public List<FileReference> BinaryFiles = [];
			public long BinSize = 0;

			public ModuleInfo(UEBuildModule Module, params string[] IncludeChain)
			{
				this.Module = Module;
				this.IncludeChain = IncludeChain;
			}

			public string Chain => String.Join(" -> ", IncludeChain.Where(x => !String.IsNullOrEmpty(x)));
		}

		private static void AnalyzeModuleChains(string ModuleName, List<string> ParentChain, UEBuildTarget Target, Dictionary<UEBuildModule, ModuleInfo> ModuleToInfo, HashSet<UEBuildModule> Visited, ILogger Logger)
		{
			// Prevent recursive includes, they'll never be shorter
			if (ParentChain.Contains(ModuleName))
			{
				return;
			}

			UEBuildModule Module = Target.GetModuleByName(ModuleName);

			List<string> CurrentChain = [.. ParentChain, Module.Name];

			if (!ModuleToInfo.TryGetValue(Module, out ModuleInfo? moduleInfo))
			{
				moduleInfo = new ModuleInfo(Module, [.. CurrentChain]);
				ModuleToInfo[Module] = moduleInfo;
			}

			if (moduleInfo.IncludeChain.Length > CurrentChain.Count)
			{
				// Now we need to recheck all downstream dependencies again because the chain may be shorter
				List<UEBuildModule> RecheckModules = [];
				Module.GetAllDependencyModules(RecheckModules, [], true, false, true);
				Visited.ExceptWith(RecheckModules);
				Logger.LogDebug("Found shorter chain for {Module} {Prev} -> {New}, rechecking {Count} already visited dependencies", Module.Name, moduleInfo.IncludeChain.Length, CurrentChain.Count, RecheckModules.Count);
				moduleInfo.IncludeChain = [.. CurrentChain];
			}
			else if (Visited.Contains(Module))
			{
				return;
			}

			Visited.Add(Module);

			List<UEBuildModule> TargetModules = [];
			Module.GetAllDependencyModules(TargetModules, [], true, false, true);
			TargetModules.ForEach(x => AnalyzeModuleChains(x.Name, CurrentChain, Target, ModuleToInfo, Visited, Logger));
		}

		private static void AnalyzeTarget(TargetDescriptor TargetDescriptor, BuildConfiguration BuildConfiguration, ILogger Logger)
		{
			// Create a makefile for the target
			UEBuildTarget Target = UEBuildTarget.Create(TargetDescriptor, BuildConfiguration, Logger);
			DirectoryReference.CreateDirectory(Target.ReceiptFileName.Directory);

			// Find the shortest path from the target to each module
			HashSet<UEBuildModule> Visited = [];
			Dictionary<UEBuildModule, ModuleInfo> ModuleToInfo = [];

			if (Target.Rules.LaunchModuleName != null)
			{
				AnalyzeModuleChains(Target.Rules.LaunchModuleName, ["target"], Target, ModuleToInfo, Visited, Logger);
			}

			foreach (string RootModuleName in Target.Rules.ExtraModuleNames)
			{
				AnalyzeModuleChains(RootModuleName, ["target"], Target, ModuleToInfo, Visited, Logger);
			}

			// Also enable all the plugin modules
			foreach (UEBuildPlugin Plugin in Target.BuildPlugins!)
			{
				foreach (UEBuildModule Module in Plugin.Modules)
				{
					if (!ModuleToInfo.ContainsKey(Module))
					{
						AnalyzeModuleChains(Module.Name, ["target", Plugin.ReferenceChain, Plugin.File.GetFileName()], Target, ModuleToInfo, Visited, Logger);
					}
				}
			}

			if (Target.Rules.bBuildAllModules)
			{
				foreach (UEBuildBinary Binary in Target.Binaries)
				{
					foreach (UEBuildModule Module in Binary.Modules)
					{
						if (!ModuleToInfo.ContainsKey(Module))
						{
							// quick hack to make allmodules always worse (empty entries are ignored when writing)
							List<string> IncludeChain = [.. Enumerable.Repeat(String.Empty, 1000)];
							IncludeChain.Add("allmodules option");
							if (Module.Rules.Plugin != null)
							{
								IncludeChain.Add(Module.Rules.Plugin.File.GetFileName());
								AnalyzeModuleChains(Module.Name, IncludeChain, Target, ModuleToInfo, Visited, Logger);
							}
							else
							{
								AnalyzeModuleChains(Module.Name, IncludeChain, Target, ModuleToInfo, Visited, Logger);
							}
						}
					}
				}
			}

			// Find all the outward dependencies of each module
			foreach ((UEBuildModule SourceModule, ModuleInfo SourceModuleInfo) in ModuleToInfo)
			{
				SourceModuleInfo.OutwardRefs.Add(SourceModule);
				SourceModule.GetAllDependencyModules([], SourceModuleInfo.OutwardRefs, false, false, false);
				SourceModuleInfo.OutwardRefs.Remove(SourceModule);
			}

			// Find the direct output dependencies of each module
			foreach ((UEBuildModule SourceModule, ModuleInfo SourceModuleInfo) in ModuleToInfo)
			{
				SourceModuleInfo.UniqueOutwardRefs = [.. SourceModuleInfo.OutwardRefs];

				foreach (UEBuildModule TargetModule in SourceModuleInfo.OutwardRefs)
				{
					HashSet<UEBuildModule> VisitedTargetModules = [SourceModule];

					List<UEBuildModule> DependencyModules = [];
					TargetModule.GetAllDependencyModules(DependencyModules, VisitedTargetModules, false, false, false);
					DependencyModules.Remove(TargetModule);

					SourceModuleInfo.UniqueOutwardRefs.ExceptWith(DependencyModules);
				}
			}

			// Find the direct inward dependencies of each module
			foreach ((UEBuildModule SourceModule, ModuleInfo SourceModuleInfo) in ModuleToInfo)
			{
				foreach (UEBuildModule TargetModule in SourceModuleInfo.OutwardRefs)
				{
					ModuleToInfo[TargetModule].InwardRefs.Add(SourceModule);
				}
				foreach (UEBuildModule TargetModule in SourceModuleInfo.UniqueOutwardRefs)
				{
					ModuleToInfo[TargetModule].UniqueInwardRefs.Add(SourceModule);
				}
			}

			// Estimate the size of object files for each module
			foreach ((UEBuildModule SourceModule, ModuleInfo SourceModuleInfo) in ModuleToInfo)
			{
				if (DirectoryReference.Exists(SourceModule.IntermediateDirectory))
				{
					foreach (FileReference IntermediateFile in DirectoryReference.EnumerateFiles(SourceModule.IntermediateDirectory, "*", SearchOption.AllDirectories))
					{
						if (IntermediateFile.HasExtension(".obj") || IntermediateFile.HasExtension(".o"))
						{
							SourceModuleInfo.ObjectFiles.Add(IntermediateFile);
							SourceModuleInfo.ObjSize += IntermediateFile.ToFileInfo().Length;
						}
					}
				}
			}

			HashSet<UEBuildModule> MissingModules = [];
			foreach (UEBuildBinary Binary in Target.Binaries)
			{
				long BinSize = 0;
				foreach (FileReference OutputFilePath in Binary.OutputFilePaths)
				{
					FileInfo OutputFileInfo = OutputFilePath.ToFileInfo();
					if (OutputFileInfo.Exists)
					{
						BinSize += OutputFileInfo.Length;
					}
				}
				foreach (UEBuildModule Module in Binary.Modules)
				{
					if (!ModuleToInfo.TryGetValue(Module, out ModuleInfo? ModuleInfo))
					{
						MissingModules.Add(Module);
						continue;
					}

					ModuleInfo.BinaryFiles.AddRange(Binary.OutputFilePaths);
					ModuleInfo.BinSize += BinSize;
				}
			}

			// Warn about any missing modules
			foreach (UEBuildModule MissingModule in MissingModules.OrderBy(x => x.Name))
			{
				Logger.LogInformation("Missing module '{MissingModuleName}'", MissingModule.Name);
			}

			List<KeyValuePair<FileReference, BuildProductType>> AnalyzeProducts = [];

			// Generate the dependency graph between modules
			FileReference DependencyGraphFile = Target.ReceiptFileName.ChangeExtension(".Dependencies.gexf");
			Logger.LogInformation("Writing dependency graph to {DependencyGraphFile}...", DependencyGraphFile);
			WriteDependencyGraph(Target, ModuleToInfo, DependencyGraphFile);
			AnalyzeProducts.Add(new(DependencyGraphFile, BuildProductType.BuildResource));

			// Generate the dependency graph between modules
			FileReference ShortestPathGraphFile = Target.ReceiptFileName.ChangeExtension(".ShortestPath.gexf");
			Logger.LogInformation("Writing shortest-path graph to {ShortestPathGraphFile}...", ShortestPathGraphFile);
			WriteShortestPathGraph(Target, ModuleToInfo, ShortestPathGraphFile);
			AnalyzeProducts.Add(new(ShortestPathGraphFile, BuildProductType.BuildResource));

			// Write all the target stats as a text file
			FileReference TextFile = Target.ReceiptFileName.ChangeExtension(".txt");
			Logger.LogInformation("Writing module information to {TextFile}", TextFile);
			using (StreamWriter Writer = new(TextFile.FullName))
			{
				Writer.WriteLine("All modules in {0}, ordered by number of indirect references", Target.TargetName);

				foreach (ModuleInfo ModuleInfo in ModuleToInfo.Values.OrderByDescending(x => x.InwardRefs.Count).ThenBy(x => x.BinSize))
				{
					Writer.WriteLine("");
					Writer.WriteLine("Module:                  \"{0}\"", ModuleInfo.Module.Name);
					Writer.WriteLine("Shortest path:           {0}", ModuleInfo.Chain);
					WriteDependencyList(Writer, "Unique inward refs:     ", ModuleInfo.UniqueInwardRefs);
					WriteDependencyList(Writer, "Unique outward refs:    ", ModuleInfo.UniqueOutwardRefs);
					WriteDependencyList(Writer, "Recursive inward refs:  ", ModuleInfo.InwardRefs);
					WriteDependencyList(Writer, "Recursive outward refs: ", ModuleInfo.OutwardRefs);
					Writer.WriteLine("Object size:             {0:n0}kb", (ModuleInfo.ObjSize + 1023) / 1024);
					Writer.WriteLine("Object files:            {0}", String.Join(", ", ModuleInfo.ObjectFiles.Select(x => x.GetFileName())));
					Writer.WriteLine("Binary size:             {0:n0}kb", (ModuleInfo.BinSize + 1023) / 1024);
					Writer.WriteLine("Binary files:            {0}", String.Join(", ", ModuleInfo.BinaryFiles.Select(x => x.GetFileName())));
				}
			}
			AnalyzeProducts.Add(new(TextFile, BuildProductType.BuildResource));

			// Write all the target stats as a CSV file
			FileReference CsvFile = Target.ReceiptFileName.ChangeExtension(".csv");
			Logger.LogInformation("Writing module information to {CsvFile}", CsvFile);
			using (StreamWriter Writer = new(CsvFile.FullName))
			{
				List<string> Columns =
				[
					"Module",
					"ShortestPath",
					"NumUniqueInwardRefs",
					"UniqueInwardRefs",
					"NumRecursiveInwardRefs",
					"RecursiveInwardRefs",
					"NumUniqueOutwardRefs",
					"UniqueOutwardRefs",
					"NumRecursiveOutwardRefs",
					"RecursiveOutwardRefs",
					"ObjSize",
					"ObjFiles",
					"BinSize",
					"BinFiles",
				];
				Writer.WriteLine(String.Join(",", Columns));

				foreach (ModuleInfo ModuleInfo in ModuleToInfo.Values.OrderByDescending(x => x.InwardRefs.Count).ThenBy(x => x.BinSize))
				{
					Columns.Clear();
					Columns.Add(ModuleInfo.Module.Name);
					Columns.Add(ModuleInfo.Chain);
					Columns.Add($"{ModuleInfo.UniqueInwardRefs.Count}");
					Columns.Add($"\"{String.Join(", ", ModuleInfo.UniqueInwardRefs.Select(x => x.Name))}\"");
					Columns.Add($"{ModuleInfo.InwardRefs.Count}");
					Columns.Add($"\"{String.Join(", ", ModuleInfo.InwardRefs.Select(x => x.Name))}\"");
					Columns.Add($"{ModuleInfo.UniqueOutwardRefs.Count}");
					Columns.Add($"\"{String.Join(", ", ModuleInfo.UniqueOutwardRefs.Select(x => x.Name))}\"");
					Columns.Add($"{ModuleInfo.OutwardRefs.Count}");
					Columns.Add($"\"{String.Join(", ", ModuleInfo.OutwardRefs.Select(x => x.Name))}\"");
					Columns.Add($"{ModuleInfo.ObjSize}");
					Columns.Add($"\"{String.Join(", ", ModuleInfo.ObjectFiles.Select(x => x.GetFileName()))}\"");
					Columns.Add($"{ModuleInfo.BinSize}");
					Columns.Add($"\"{String.Join(", ", ModuleInfo.BinaryFiles.Select(x => x.GetFileName()))}\"");
					Writer.WriteLine(String.Join(",", Columns));
				}
			}
			AnalyzeProducts.Add(new(CsvFile, BuildProductType.BuildResource));

			foreach (FileReference ManifestFileName in Target.Rules.ManifestFileNames)
			{
				Target.GenerateManifest(ManifestFileName, AnalyzeProducts, Logger);
			}
		}

		private static void WriteDependencyList(TextWriter Writer, string Prefix, HashSet<UEBuildModule> Modules)
		{
			if (Modules.Count == 0)
			{
				Writer.WriteLine("{0} 0", Prefix);
			}
			else
			{
				Writer.WriteLine("{0} {1} ({2})", Prefix, Modules.Count, String.Join(", ", Modules.Select(x => x.Name).OrderBy(x => x)));
			}
		}

		private static void WriteDependencyGraph(UEBuildTarget Target, Dictionary<UEBuildModule, ModuleInfo> ModuleToInfo, FileReference FileName)
		{
			List<GraphNode> Nodes = [];

			Dictionary<UEBuildModule, GraphNode> ModuleToNode = [];
			foreach (ModuleInfo ModuleInfo in ModuleToInfo.Values)
			{
				GraphNode Node = new(ModuleInfo.Module.Name);

				long Size = Target.ShouldCompileMonolithic() ? ModuleInfo.ObjSize : ModuleInfo.BinSize;
				Node.Size = 1.0f + (Size / (50.0f * 1024.0f * 1024.0f));
				Nodes.Add(Node);
				ModuleToNode[ModuleInfo.Module] = Node;
			}

			List<GraphEdge> Edges = [];
			foreach ((UEBuildModule SourceModule, ModuleInfo SourceModuleInfo) in ModuleToInfo)
			{
				GraphNode SourceNode = ModuleToNode[SourceModule];
				foreach (UEBuildModule TargetModule in SourceModuleInfo.UniqueOutwardRefs)
				{
					ModuleInfo TargetModuleInfo = ModuleToInfo[TargetModule];

					if (ModuleToNode.TryGetValue(TargetModule, out GraphNode? TargetNode))
					{
						Edges.Add(new(SourceNode, TargetNode)
						{
							Thickness = TargetModuleInfo.InwardRefs.Count
						});
					}
				}
			}

			GraphVisualization.WriteGraphFile(FileName, $"Module dependency graph for {Target.TargetName}", Nodes, Edges);
		}

		private static void WriteShortestPathGraph(UEBuildTarget Target, Dictionary<UEBuildModule, ModuleInfo> ModuleToInfo, FileReference FileName)
		{
			Dictionary<string, GraphNode> NameToNode = new(StringComparer.Ordinal);

			HashSet<(GraphNode, GraphNode)> EdgesSet = [];
			List<GraphEdge> Edges = [];

			foreach ((UEBuildModule Module, ModuleInfo ModuleInfo) in ModuleToInfo)
			{
				string[] Parts = ModuleInfo.Chain.Split(" -> ");

				GraphNode? PrevNode = null;
				foreach (string Part in Parts)
				{
					if (!NameToNode.TryGetValue(Part, out GraphNode? NextNode))
					{
						NextNode = new GraphNode(Part);
						NameToNode[Part] = NextNode;
					}
					if (PrevNode != null && EdgesSet.Add((PrevNode, NextNode)))
					{
						Edges.Add(new(PrevNode, NextNode));
					}
					PrevNode = NextNode;
				}
			}

			GraphVisualization.WriteGraphFile(FileName, $"Module dependency graph for {Target.TargetName}", [.. NameToNode.Values], Edges);
		}

		private static HashSet<UEBuildModule> GetDirectDependencyModules(UEBuildModule Module)
		{
			HashSet<UEBuildModule> ReferencedModules = [];
			Module.GetAllDependencyModules([], ReferencedModules, true, false, false);

			HashSet<UEBuildModule> Modules = [.. Module.GetDirectDependencyModules()];
			Modules.ExceptWith(ReferencedModules);
			return Modules;
		}
	}
}

