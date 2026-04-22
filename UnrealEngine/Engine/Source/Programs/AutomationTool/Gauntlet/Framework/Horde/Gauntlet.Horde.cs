// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;

namespace Gauntlet
{
	public static class Horde
	{

		static public bool IsHordeJob
		{
			get
			{
				return !string.IsNullOrEmpty(JobId);
			}
		}

		static public string JobId
		{
			get
			{
				return Environment.GetEnvironmentVariable("UE_HORDE_JOBID");
			}
		}

		static public string StepId
		{
			get
			{
				return Environment.GetEnvironmentVariable("UE_HORDE_STEPID");
			}
		}
		public class JobStepReport
		{
			public string scope { get; } = "Step";
			public string name { get; set; }
			public string placement { get; } = "Summary";
			public string fileName { get; set; }
		}


		public delegate string SummaryLineDelegate();
		/// <summary>
		/// Delegate called when the Horde step summary is generated.
		/// Returned string is appended as a line to the summary report.
		/// </summary>
		/// <returns></returns>
		public static SummaryLineDelegate OnStepSummary;

		public static void AddJobBuildMetaData(IBuildSource buildSource)
		{
			try
			{
				if (!IsHordeJob)
				{
					return;
				}

				if (buildSource != null)
				{
					CommandUtils.AddHordeJobMetadata(EpicGames.Horde.Jobs.JobId.Parse(JobId), null, new Dictionary<string, List<string>> { { StepId, new List<string> { $"BuildName=${buildSource.BuildName}?display=true" } } }).Wait();
				}

			}
			catch (Exception Ex)
			{
				Log.Info("Exception while tagging Horde job with build metadata\n{0}\n", Ex.Message);
			}
		}

		public static string GenerateArtifactLink(string ArtifactPath)
		{
			// file:// is attractive here, and file://\\computer\share\folder\file.txt does work ok in Chrome, but it does not Firefox
			// so, lacking a uniform way to link to a network filesystem, just put the path here as static text and escaped for markdown
			return $"* **Netshare Artifacts**: {System.Text.RegularExpressions.Regex.Replace(ArtifactPath, @"([|\\*\+])", @"\$1")}";
		}

		public static void GenerateSummary()
		{
			try
			{
				if (!IsHordeJob)
				{
					return;
				}

				StringBuilder Markdown = new StringBuilder();

				if (OnStepSummary != null)
				{
					foreach (SummaryLineDelegate callback in OnStepSummary.GetInvocationList())
					{
						Markdown.AppendLine(callback());
					}
				}

				// detect if any testdata was generated
				if (Globals.Params.ParseParam("UseTestDataV2"))
				{
					DirectoryInfo TestDataOutputFolder = new DirectoryInfo(Path.Combine(CommandUtils.CmdEnv.EngineSavedFolder, "TestData"));
					if (TestDataOutputFolder.Exists && TestDataOutputFolder.GetFiles("*.json", SearchOption.TopDirectoryOnly).Any())
					{
						Markdown.AppendLine($"* **Test Results**: [Open report](/test-automation?job={JobId}&step={StepId})");
					}
					else
					{
						Log.Verbose("Did not add Horde report link as any test data files in '{0}' was found.\n", TestDataOutputFolder.FullName);
					}
				}

				if (Markdown.Length > 0)
				{
					string LogFolder = CommandUtils.CmdEnv.LogFolder;
					string MarkdownFilename = "GauntletStepDetails.md";

					File.WriteAllText(Path.Combine(LogFolder, MarkdownFilename), Markdown.ToString());

					JobStepReport report = new JobStepReport() { name = "Gauntlet Step Details", fileName = MarkdownFilename };
					File.WriteAllText(Path.Combine(LogFolder, "GauntletStepDetails.report.json"), JsonSerializer.Serialize(report));
				}
			}
			catch (Exception Ex)
			{
				Log.Info("Exception while generating Horde summary\n{0}\n", Ex);
			}
		}

	}
}
