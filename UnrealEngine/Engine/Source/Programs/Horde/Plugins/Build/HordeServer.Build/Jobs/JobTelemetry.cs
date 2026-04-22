// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Commits;
using EpicGames.Horde.Jobs;
using EpicGames.Horde.Jobs.Templates;
using EpicGames.Horde.Streams;

namespace HordeServer.Jobs
{
	/// <summary>
	/// Record used for Job Summary telemetry. Models a <see cref="IJob"/>.
	/// </summary>
	public record JobSummaryTelemetry
	{
		/// <summary>
		/// The telemetry event name.
		/// </summary>
		public string EventName { get; init; }

		/// <summary>
		/// The job Id.
		/// </summary>
		public JobId JobId { get; init; }

		/// <summary>
		/// The template of the job.
		/// </summary>
		public TemplateId TemplateId { get; init; }

		/// <summary>
		/// The stream of the job.
		/// </summary>
		public StreamId StreamId { get; init; }

		/// <summary>
		/// The commit of the job. <see cref="IJob.CommitId"/>.
		/// </summary>
		public CommitIdWithOrder CommitId { get; init; }

		/// <summary>
		/// The code commit of the job. <see cref="IJob.CodeCommitId"/>.
		/// </summary>
		public CommitIdWithOrder CodeCommitId { get; init; }

		/// <summary>
		/// The created time of the job.
		/// </summary>
		public DateTime CreateTimeUtc { get; init; }

		/// <summary>
		/// The finish time of the job.
		/// </summary>
		public DateTime FinishTimeUtc { get; init; }

		/// <summary>
		/// 
		/// </summary>
		public float JobStepsTotalTime { get; init; }

		/// <summary>
		/// The wall time of the job, in seconds.
		/// </summary>
		public float JobWallTime { get; init; }

		/// <summary>
		/// <see cref="JobStepsCompletionInfo.PassRatio"/>.
		/// </summary>
		public float PassRatio { get; init; }

		/// <summary>
		/// <see cref="JobStepsCompletionInfo.PassWithWarningRatio"/>.
		/// </summary>
		public float PassWithWarningRatio { get; init; }

		/// <summary>
		/// <see cref="JobStepsCompletionInfo.WarningRatio"/>.
		/// </summary>
		public float WarningRatio { get; init; }

		/// <summary>
		/// <see cref="JobStepsCompletionInfo.FailureRatio"/>.
		/// </summary>
		public float FailureRatio { get; init; }

		/// <summary>
		/// <see cref="JobStepsCompletionInfo.StepPassCount"/>.
		/// </summary>
		public int StepPassCount { get; init; }

		/// <summary>
		/// <see cref="JobStepsCompletionInfo.StepWarningCount"/>.
		/// </summary>
		public int StepWarningCount { get; init; }

		/// <summary>
		/// <see cref="JobStepsCompletionInfo.StepFailureCount"/>.
		/// </summary>
		public int StepFailureCount { get; init; }

		/// <summary>
		/// <see cref="JobStepsCompletionInfo.StepTotalCount"/>.
		/// </summary>
		public int StepTotalCount { get; init; }

		/// <summary>
		/// Default event name for the JobSummaryTelemetry.
		/// </summary>
		public const string DefaultEventName = "State.JobSummary";

		/// <summary>
		/// Current schema version.
		/// </summary>
		public static readonly int CurrentSchemaVersion = 1;

		/// <summary>
		/// Constructs a JobSummary Telemetry event.
		/// </summary>
		/// <param name="job">The job to base this telemetry event on.</param>
		/// <param name="finishTimeUtc">The completion time of this job.</param>
		public JobSummaryTelemetry(IJob job, DateTime finishTimeUtc)
		{
			EventName = DefaultEventName;
			JobId = job.Id;
			TemplateId = job.TemplateId;
			StreamId = job.StreamId;
			CommitId = job.CommitId;
			CodeCommitId = job.CodeCommitId ?? CommitIdWithOrder.Empty;
			CreateTimeUtc = job.CreateTimeUtc;
			FinishTimeUtc = finishTimeUtc;
			JobWallTime = (float)(FinishTimeUtc - CreateTimeUtc).TotalSeconds;

			JobStepsCompletionInfo jobStepsCompletionInfo = job.GetStepCompletionInfo();

			StepTotalCount = jobStepsCompletionInfo.StepTotalCount;
			StepPassCount = jobStepsCompletionInfo.StepPassCount;
			StepWarningCount = jobStepsCompletionInfo.StepWarningCount;
			StepFailureCount = jobStepsCompletionInfo.StepFailureCount;
			JobStepsTotalTime = jobStepsCompletionInfo.JobStepsTotalTime;
			PassRatio = jobStepsCompletionInfo.PassRatio;
			PassWithWarningRatio = jobStepsCompletionInfo.PassWithWarningRatio;
			WarningRatio = jobStepsCompletionInfo.WarningRatio;
			FailureRatio = jobStepsCompletionInfo.FailureRatio;
		}
	}
}