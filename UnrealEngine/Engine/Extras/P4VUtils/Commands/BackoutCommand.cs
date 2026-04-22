// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using P4VUtils.Perforce;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace P4VUtils.Commands
{

	// In our P4 API, we internally launch p4.exe with -G which neatly returns all results as a serialized Python dictionary,
	// and there's some clever reflection code that automatically maps this serialized data to our structures so adding new fields or supporting new commands is a breeze.
	// It's great that all p4 commands support this!
	//
	// Well, all except p4 -G tickets
	//
	// So, back to text parsing for that one

	class TicketInfo
	{
		public string? ServerCluster { get; set; }
		public string? Username { get; set; }
		public string? Ticket { get; set; }
	}

	// Basic wrapper over launching p4.exe and parsing the text output
	class RawP4
	{
		public static async Task<List<string>> Execute(string arguments)
		{
			var outputLines = new List<string>();
			ProcessStartInfo processStartInfo = new ProcessStartInfo
			{
				FileName = PerforceChildProcess.GetExecutable(),
				Arguments = arguments,
				RedirectStandardOutput = true,
				UseShellExecute = false,
				CreateNoWindow = true,
			};

			using (Process process = new Process { StartInfo = processStartInfo })
			{
				var tcs = new TaskCompletionSource<bool>();
				process.EnableRaisingEvents = true;
				process.Exited += (sender, args) => tcs.TrySetResult(true);

				process.OutputDataReceived += (sender, e) =>
				{
					if (e.Data != null)
					{
						lock (outputLines)
						{
							outputLines.Add(e.Data);
						}
					}
				};

				process.Start();
				process.BeginOutputReadLine();
				await tcs.Task;
			}
			return outputLines;
		}

		public static async Task<List<TicketInfo>> Tickets()
		{
			List<TicketInfo> Results = new List<TicketInfo>();

			List<string> RawTickets = await Execute("tickets");
			
			Regex TicketRegex = new Regex(@"^[^:]+:(?<cluster>[^ ]+) \((?<username>[^)]+)\) (?<ticket>[A-F0-9]+)$", RegexOptions.Compiled);

			foreach (string RawTicket in RawTickets)
			{
				Match m = TicketRegex.Match(RawTicket.Trim());
				TicketInfo ti = new TicketInfo { 
					ServerCluster = m.Groups["cluster"].Value,
					Username      = m.Groups["username"].Value,
					Ticket        = m.Groups["ticket"].Value,
				};
				Results.Add(ti);
			}
			return Results;
		}
	} // class RawP4


	class BackoutValidationResult
	{
		public List<String> Errors = new List<String>();
	}

	abstract class BaseBackoutCommand : Command
	{
		private readonly int MAX_ERROR_LENGTH = 2048;

		public abstract Task<BackoutValidationResult> ExtraValidation(PerforceConnection Perforce, int Change, ILogger Logger);

		public abstract Task PostBackoutOperation(PerforceConnection Perforce, ChangeRecord Change, ILogger Logger);

		public virtual string GenerateDescription(DescribeRecord OldChangelist)
		{
			// Sanitize the existing description of tags we don't want to retain
			string[] TagsToRemove = { "#lockdown" };
			return string.Join("\n", OldChangelist.Description.Split("\n").Where(DescLine => !TagsToRemove.Any(Tag => DescLine.StartsWith(Tag, StringComparison.OrdinalIgnoreCase))));
		}

		private async Task<BackoutValidationResult> ValidateBackoutSafety(PerforceConnection Perforce, int Change, ILogger Logger)
		{
			BackoutValidationResult Result = new BackoutValidationResult();

			Logger.LogInformation("Examining CL {Change} for Safe Backout", Change);

			// was this change cherry picked using ushell or p4vutil or was it robomerged?
			DescribeRecord DescribeRec = await Perforce.DescribeAsync(Change, CancellationToken.None);
			if (DescribeRec.Description.Contains("#ushell-cherrypick", StringComparison.InvariantCultureIgnoreCase)
			|| DescribeRec.Description.Contains("#p4v-cherrypick", StringComparison.InvariantCultureIgnoreCase)
			|| DescribeRec.Description.Contains("#ROBOMERGE-SOURCE", StringComparison.InvariantCultureIgnoreCase))
			{
				// it was a cherry pick or a robomerged CL, just bail
				Logger.LogError("CL {Change} did not originate in the current stream,\nto be safe, CLs should be backed out where the change was originally submitted", Change);
				Result.Errors.Add(string.Format($"CL {Change} did not originate in the current stream, to be safe, CLs should be backed out where the change was originally submitted.", Change));
			}

			List<DescribeFileRecord> BinaryFiles = new List<DescribeFileRecord>();

			Regex VersionFileRegex = new Regex(@"(/|Object|Custom)Versions?\.(h|inl)", RegexOptions.Compiled);
			foreach (DescribeFileRecord DescFileRec in DescribeRec.Files)
			{
				// is this an edit/add/delete operation?
				if (!P4ActionGroups.EditActions.Contains(DescFileRec.Action))
				{
					Logger.LogError("In CL {Change},\nthe action for the file listed below isn't considered an 'edit',\ntherefore in isn't safe to back it out.\n\n{FileName}", Change, DescFileRec.DepotFile);
					Result.Errors.Add($"In CL {Change},\nthe action for the file listed below isn't considered an 'edit', therefore in isn't safe to back it out.\n\n{DescFileRec.DepotFile}");
				}

				// is it an versioning file?
				if (VersionFileRegex.IsMatch(DescFileRec.DepotFile))
				{
					Logger.LogError("In CL {Change}, the file below affects asset versioning,\ntherefore it isn't safe to back it out.\n\n{FileName}", Change, DescFileRec.DepotFile);
					Result.Errors.Add($"In CL {Change}, the file below affects asset versioning, therefore it isn't safe to back it out.\n\n{DescFileRec.DepotFile}");
				}

				// binary files need another round of validation later
				if (DescFileRec.Type.Contains("+l", StringComparison.InvariantCultureIgnoreCase))
				{
					BinaryFiles.Add(DescFileRec);
				}
			}

			if (BinaryFiles.Count > 0)
			{
				// we can only safely backout binary files that are at the head revision, so check for that/

				FileSpecList FileSpecs = BinaryFiles.Select(x => x.DepotFile).ToList();

				List<FStatRecord> StatRecord = await Perforce.FStatAsync(FileSpecs).ToListAsync();

				if (BinaryFiles.Count == StatRecord.Count)
				{
					for (int Index = 0; Index < BinaryFiles.Count; ++Index)
					{
						if (StatRecord[Index].HeadRevision != BinaryFiles[Index].Revision)
						{
							Logger.LogError("\nIn CL {Change}, the file below is a binary file and revision #{Rev} isn't the head revision (#{HeadRev}),\ntherefore it isn't safe to back it out\n\n{FileName}",
								Change, BinaryFiles[Index].Revision, StatRecord[Index].HeadRevision, BinaryFiles[Index].DepotFile);
							Result.Errors.Add($"\nIn CL {Change}, the file below is a binary file and revision #{BinaryFiles[Index].Revision} isn't the head revision (#{StatRecord[Index].HeadRevision}), therefore it isn't safe to back it out\n\n{BinaryFiles[Index].DepotFile}");
						}
					}
				}
				else
				{
					// This shouldn't happen but if fstat did not return the expected number of records then we cannot safely continue
					throw new FatalErrorException("Error running ValidateBackoutSafety\nThe number of records returned from p4 fstat {0} does not match the number of records requested {1}'", StatRecord.Count, BinaryFiles.Count);
				}
			}

			BackoutValidationResult ExtraResults = await ExtraValidation(Perforce, Change, Logger);
			Result.Errors.AddRange(ExtraResults.Errors);

			if (Result.Errors.Count == 0)
			{
				Logger.LogInformation("CL {Change} seems safe to backout", Change);
			}
			return Result;
		}



		public override async Task<int> Execute(string[] Args, IReadOnlyDictionary<string, string> ConfigValues, ILogger Logger)
		{
			int Change;
			if (Args.Length < 2)
			{
				Logger.LogError("Missing changelist number");
				return 1;
			}
			if (!int.TryParse(Args[1], out Change))
			{
				Logger.LogError("'{Argument}' is not a numbered changelist", Args[1]);
				return 1;
			}
			string? ClientName = null;
			if (Args.Length > 2)
			{
				ClientName = Args[2];
			}

			bool Debug = Args.Any(x => x.Equals("-Debug", StringComparison.OrdinalIgnoreCase));

			using PerforceConnection Perforce = new PerforceConnection(null, null, ClientName, Logger);

			BackoutValidationResult ValidationResult = await ValidateBackoutSafety(Perforce, Change, Logger);

			if (ValidationResult.Errors.Count > 0)
			{
				Logger.LogInformation("\r\nCL {Change} isn't safe to backout, please confirm if you want to proceed.\r\n", Change);

				string ErrorString = String.Join("\r\n", ValidationResult.Errors);
				if (ErrorString.Length > MAX_ERROR_LENGTH)
				{
					ErrorString = $"{ErrorString.Substring(0, MAX_ERROR_LENGTH)}\r\n (...)";
				}

				StringBuilder MessageText = new StringBuilder();
				MessageText.Append("This backout is potentially unsafe:\r\n");
				MessageText.Append("\r\n");
				MessageText.Append(ErrorString);
				MessageText.Append("\r\n\r\n");
				MessageText.Append("Are you sure you want to proceed with the backout operation?\r\n");
				MessageText.Append("\r\n");

				if (ConfigValues.TryGetValue("SafeBackoutHelpText", out string? HelpText))
				{
					MessageText.Append(HelpText);
					MessageText.Append("\r\n");
				}

				// warn user
				UserInterface.Button result = UserInterface.ShowDialog(MessageText.ToString(), "Unsafe backout detected", UserInterface.YesNo, UserInterface.Button.No, Logger);

				if (result == UserInterface.Button.No)
				{
					Logger.LogInformation("\r\nOperation canceled.");
					return 0;
				}
				else
				{
					Logger.LogInformation("\r\nProceeding with backout operation.");
				}
			}

			DescribeRecord ExistingChangeRecord = await Perforce.DescribeAsync(Change, CancellationToken.None);

			// P4V undo checks for files opened before executing 'undo'
			List<OpenedRecord> OpenedRecords = await Perforce.OpenedAsync(OpenedOptions.AllWorkspaces | OpenedOptions.ShortOutput, ExistingChangeRecord.Files.Select(x => x.DepotFile).ToArray(), CancellationToken.None).ToListAsync();
			if (OpenedRecords.Count > 0)
			{
				HashSet<string> UniqueDepotFiles = (OpenedRecords.Where(x => !String.IsNullOrEmpty(x.DepotFile)).Select(x => x.DepotFile!)).ToHashSet();
				string FileListString = string.Join("\r\n", UniqueDepotFiles);

				Logger.LogInformation("\r\nSome files are checked out on another workspace, please confirm that you wish to continue.\r\n");
				Logger.LogInformation("{FileList}", FileListString);

				string FileListTruncated = FileListString;
				if (FileListString.Length > MAX_ERROR_LENGTH)
				{
					FileListTruncated = FileListTruncated.Substring(0, MAX_ERROR_LENGTH);
					FileListTruncated += "(...)";
				}

				// prompt
				UserInterface.Button result = UserInterface.ShowDialog(
					"The following files are checked out on another workspace:\r\n" +
					"\r\n" +
					FileListTruncated +
					"\r\n\r\n" +
					"Do you want to proceed with the backout operation?\r\n" +
					"\r\n",
					"Warning",
					UserInterface.YesNo, UserInterface.Button.Yes, Logger);

				if (result == UserInterface.Button.No)
				{
					Logger.LogInformation("\r\nOperation canceled.");
					return 0;
				}
				else
				{
					Logger.LogInformation("\r\nProceeding with backout operation.");
				}
			}

			InfoRecord Info = await Perforce.GetInfoAsync(InfoOptions.None, CancellationToken.None);

			// Create a new CL
			ChangeRecord NewChangeRecord = new ChangeRecord();
			NewChangeRecord.User = Info.UserName;
			NewChangeRecord.Client = Info.ClientName;
			NewChangeRecord.Description = GenerateDescription(ExistingChangeRecord);
			
			NewChangeRecord = await Perforce.CreateChangeAsync(NewChangeRecord, CancellationToken.None);

			Logger.LogInformation("Created pending changelist {Change}", NewChangeRecord.Number);

			// Undo the passed in CL
			PerforceResponseList<UndoRecord> UndoResponses = await Perforce.TryUndoChangeAsync(Change, NewChangeRecord.Number, CancellationToken.None);

			// Grab new CL info
			DescribeRecord RefreshNewRecord = await Perforce.DescribeAsync(NewChangeRecord.Number, CancellationToken.None);

			// if the original CL and the new CL differ in file count then an error occurs, abort and clean up
			if (RefreshNewRecord.Files.Count != ExistingChangeRecord.Files.Count)
			{
				Logger.LogError("Undo on CL {Change} failed (probably due to an exclusive check out)", Change);
				foreach (PerforceResponse Response in UndoResponses)
				{
					Logger.LogError("  {Response}", Response.ToString());
				}

				// revert files in the new CL
				List<OpenedRecord> NewCLOpenedRecords = await Perforce.OpenedAsync(OpenedOptions.None, NewChangeRecord.Number, null, null, -1, FileSpecList.Any, CancellationToken.None).ToListAsync();

				if (OpenedRecords.Count > 0)
				{
					Logger.LogError("  Reverting");
					await Perforce.RevertAsync(NewChangeRecord.Number, null, RevertOptions.DeleteAddedFiles, NewCLOpenedRecords.Select(x => x.ClientFile!).ToArray(), CancellationToken.None);
				}

				// delete the new CL
				Logger.LogError("  Deleting newly created CL {Change}", NewChangeRecord.Number);
				await Perforce.DeleteChangeAsync(DeleteChangeOptions.None, RefreshNewRecord.Number, CancellationToken.None);

				return 1;
			}
			else
			{
				Logger.LogInformation("Undo of {Change} created CL {NewChange}", Change, NewChangeRecord.Number);
			}

			// Convert the undo CL over to an edit.
			if (!await ConvertToEditCommand.ConvertToEditAsync(Perforce, NewChangeRecord.Number, Debug, Logger))
			{
				return 1;
			}

			await PostBackoutOperation(Perforce, NewChangeRecord, Logger);

			return 0;
		}

	} //class BaseBackoutCommand

	[Command("backout", CommandCategory.Toolbox, 0)]
	class BackoutCommand : BaseBackoutCommand
	{
		public override string Description => "P4 Admin sanctioned method of backing out a CL";

		public override CustomToolInfo CustomTool => new CustomToolInfo("Safe Backout tool", "%S $c") { ShowConsole = true };

		public override async Task<BackoutValidationResult> ExtraValidation(PerforceConnection Perforce, int Change, ILogger Logger)
		{
			BackoutValidationResult Result = new BackoutValidationResult();

			Logger.LogInformation("\n\nExamining CL {Change} for extra unwanted Backout-related marks\n", Change);

			// was this change cherry picked using ushell or p4vutil or was it robomerged?
			DescribeRecord DescribeRec = await Perforce.DescribeAsync(Change, CancellationToken.None);
			if (DescribeRec.Description.Contains("[Backout]", StringComparison.InvariantCultureIgnoreCase))
			{
				// Don't allow backing out of backouts, demand a restore operation instead
				Logger.LogError("CL {Change} was a backout. To restore it, use the Safe Restore tool!", Change);
				Result.Errors.Add(string.Format($"CL {Change} was a backout. To restore it, use the Safe Restore tool!", Change));
			}

			return Result;
		}

		public override string GenerateDescription(DescribeRecord OldChangelist)
		{
			string OldDescription = base.GenerateDescription(OldChangelist);

			return $"[Backout] - CL{OldChangelist.Number}\n#fyi {OldChangelist.User}\n#submittool safebackout\n#rnx\nOriginal CL Desc\n-----------------------------------------------------------------\n{OldDescription.TrimEnd()}\n";
		}

		public override async Task PostBackoutOperation(PerforceConnection Perforce, ChangeRecord Change, ILogger Logger)
		{
			await Task.CompletedTask;
		}

	} // class BackoutCommand

	[Command("restore", CommandCategory.Toolbox, 1)]
	class RestoreCommand : BaseBackoutCommand
	{
		public override string Description => "P4 Admin sanctioned method of restoring a previously backed-out CL";

		public override CustomToolInfo CustomTool => new CustomToolInfo("Safe Restore tool", "%S $c") { ShowConsole = true };

		public override async Task<BackoutValidationResult> ExtraValidation(PerforceConnection Perforce, int Change, ILogger Logger)
		{
			BackoutValidationResult Result = new BackoutValidationResult();

			Logger.LogInformation("\n\nExamining CL {Change} for Restore-related marks\n", Change);

			// Don't allow restoring of non-backouts
			DescribeRecord DescribeRec = await Perforce.DescribeAsync(Change, CancellationToken.None);
			if (!DescribeRec.Description.Contains("[Backout]", StringComparison.InvariantCultureIgnoreCase))
			{
				Logger.LogError("CL {Change} was not a backout, so doing a Restore makes no sense.", Change);
				Result.Errors.Add(string.Format($"CL {Change} was not a backout, so doing a Restore makes no sense.", Change));
			}

			return Result;
		}

		public override string GenerateDescription(DescribeRecord OldChangelist)
		{
			string OldDescription = base.GenerateDescription(OldChangelist);


			string[] TagsToIgnore = { "#changelist validated", "#rb", "#rnx", "#submittool", "#preflight", "[FYI]", "[Review]", "#robomerge", "#okforgithub" };
			var TagsToRename = new Dictionary<string, string>();
			TagsToRename["#review"] = "#original-review";

			List<string> NewLines = new List<string>();
			string[] OldLines = OldDescription.Split('\n');
			for (int i = 0; i < OldLines.Length; i++)
			{
				string Line = OldLines[i].Trim();

				// Find and skip potentially multiple subsequent [Backout] blocks 
				if (Line.StartsWith("[Backout]", StringComparison.InvariantCultureIgnoreCase))
				{
					// Look for a nearby "-----" terminator
					int skip = 1;
					for (; skip < 7 && i + skip < OldLines.Length; ++skip)
					{
						if (OldLines[i + skip].Trim().StartsWith("------", StringComparison.InvariantCultureIgnoreCase))
						{
							break;
						}
					}
					i += skip;
					continue;
				}

				if (TagsToIgnore.Any(Tag => Line.StartsWith(Tag, StringComparison.InvariantCultureIgnoreCase)))
				{
					continue;
				}

				foreach (var pair in TagsToRename)
				{
					if (Line.StartsWith(pair.Key, StringComparison.InvariantCultureIgnoreCase))
					{
						Line = string.Concat(pair.Value, Line.AsSpan(pair.Key.Length));
						break;
					}
				}

				NewLines.Add(Line);
			}

			NewLines.Add($"#was-backout {OldChangelist.Number}");
			return string.Join("\n", NewLines);
		}

		public override async Task PostBackoutOperation(PerforceConnection Perforce, ChangeRecord Change, ILogger Logger)
		{
			// Re-get the list of opened files in the CL along with their metadata
			Logger.LogInformation("Getting the list of files in changelist {Change}...", Change.Number);
			List<OpenedRecord> OpenedRecords = await Perforce.OpenedAsync(OpenedOptions.None, Change.Number, null, null, -1, FileSpecList.Any, CancellationToken.None).ToListAsync();
			if (OpenedRecords.Count == 0)
			{
				Logger.LogInformation("Change has no opened files. Aborting.");
				return;
			}

			if (!OpenedRecords.Any(Record => PerforceUtils.IsCodeFile(Record.DepotFile)))
			{
				Logger.LogInformation("\nNo source files found in the changelist, nothing else to do.");
				return;
			}

			string SwarmBaseUrl;
			try
			{
				SwarmBaseUrl = await Perforce.GetPropertyAsync("P4.Swarm.URL");
			}
			catch
			{
				// Silently abort if there is no Swarm integration
				return;
			}

			Logger.LogInformation("\nSource files found in the changelist, creating a shelf");

			// Parse the previous review id
			string? OriginalReviewId = null;
			DescribeRecord ExistingChangeRecord = await Perforce.DescribeAsync(Change.Number, CancellationToken.None);
			Regex ReviewIdRegex = new Regex(@"#original-review-(\d+)", RegexOptions.Compiled);
			Match match = ReviewIdRegex.Match(ExistingChangeRecord.Description);
			if (match.Success)
			{
				OriginalReviewId = match.Groups[1].Value;
			}

			await Perforce.ShelveAsync(Change.Number, ShelveOptions.Overwrite, new[] { "//..." }, CancellationToken.None);

			// This can fail, but we don't care that much as that's an optional, cherry-on-top operation
			Logger.LogInformation("Creating a swarm review...");
			try
			{
				Logger.LogInformation("Getting P4 auth token");

				InfoRecord userInfo = await Perforce.GetInfoAsync(InfoOptions.None);
				List<TicketInfo> tickets = await RawP4.Tickets();
				TicketInfo ticket = tickets.Where(Ticket => Ticket.ServerCluster == userInfo.ServerCluster && Ticket.Username == userInfo.UserName).First();
				string AuthToken = Convert.ToBase64String(Encoding.ASCII.GetBytes($"{ticket.Username}:{ticket.Ticket}"));

				Uri ReviewEndpoint  = new Uri(SwarmBaseUrl + "/api/v9/reviews");
				Uri CommentEndpoint = new Uri(SwarmBaseUrl + "/api/v9/comments");

				using (HttpClient client = new HttpClient())
				{
					client.DefaultRequestHeaders.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Basic", AuthToken);

					string? ReviewId;

					Logger.LogInformation("HTTP: Create review");
					using (StringContent Content = new StringContent($"change={Change.Number}", Encoding.UTF8, "application/x-www-form-urlencoded"))
					{
						HttpResponseMessage Response = await client.PostAsync(ReviewEndpoint, Content);
						Response.EnsureSuccessStatusCode();
						string ResponseString = await Response.Content.ReadAsStringAsync();
						JsonObject ResponseJson = JsonObject.Parse(ResponseString);
						JsonObject ReviewObject = ResponseJson.GetObjectField("review");
						ReviewId = ReviewObject.GetIntegerField("id").ToString();
					}

					if (ReviewId is not null && OriginalReviewId is not null)
					{
						Logger.LogInformation("HTTP: Post comment");
						string[] CommentRequest = { 
							$"topic=reviews/{ReviewId}",
							$"body=Original review: {SwarmBaseUrl}/reviews/{OriginalReviewId}",
							"silenceNotification=true"
						};
						using (StringContent Content = new StringContent(String.Join('&', CommentRequest), Encoding.UTF8, "application/x-www-form-urlencoded"))
						{
							HttpResponseMessage Response = await client.PostAsync(CommentEndpoint, Content);
							Response.EnsureSuccessStatusCode();
						}
					}
				}

				Logger.LogInformation("Swarm review created");
			}
			catch
			{
				Logger.LogInformation("Review submit failed. Please manually submit your shelve to Swarm before you make any changes!");
			}
		}

	} // class RestoreCommand
} //namespace P4VUtils.Commands
