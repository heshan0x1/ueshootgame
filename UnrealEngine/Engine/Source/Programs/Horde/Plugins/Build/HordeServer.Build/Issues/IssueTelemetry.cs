// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Commits;
using EpicGames.Horde.Issues;
using EpicGames.Horde.Jobs;
using EpicGames.Horde.Jobs.Templates;
using EpicGames.Horde.Streams;
using EpicGames.Horde.Users;
using MongoDB.Bson;

namespace HordeServer.Issues
{
	/// <summary>
	/// Record used for Issue telemetry. Models a <see cref="IIssue"/>.
	/// </summary>
	/// <remarks>Modifications to this record's structure should be versioned appropriately through <see cref="SchemaVersion"/> via <see cref="CurrentSchemaVersion"/>, and call sites should continue to publish in a backwards compatible manner.</remarks>
	public record IssueTelemetry(string EventName, int SchemaVersion, int Id, DateTime? AcknowledgedAt, DateTime CreatedAt, UserId? OwnerId, string? ExternalIssueKey, CommitId? FixChange, bool FixedSystemic, DateTime LastSeenAt, DateTime? NominatedAt, UserId? NominatedById, DateTime? ResolvedAt, UserId? ResolvedById, IssueSeverity Severity, string Summary, DateTime? VerifiedAt)
	{
		/// <summary>
		/// Default event name for the IssueTelemetry.
		/// </summary>
		public static readonly string DefaultEventName = "State.Issue";

		/// <summary>
		/// Current schema version.
		/// </summary>
		public static readonly int CurrentSchemaVersion = 1;

		/// <summary>
		/// Telemetry payload for <see cref="IIssue"/>.
		/// </summary>
		public IssueTelemetry(IIssue issue)
			: this(
					DefaultEventName,
					CurrentSchemaVersion,
					issue.Id,
					issue.AcknowledgedAt,
					issue.CreatedAt,
					issue.OwnerId,
					issue.ExternalIssueKey,
					issue.FixCommitId,
					issue.FixSystemic,
					issue.LastSeenAt,
					issue.NominatedAt,
					issue.NominatedById,
					issue.ResolvedAt,
					issue.ResolvedById,
					issue.Severity,
					issue.Summary,
					issue.VerifiedAt
				  )
		{
		}
	}

	/// <summary>
	/// Record used for IssueFingerprint telemetry. Models a <see cref="IIssueFingerprint"/>.
	/// </summary>
	/// <remarks>Modifications to this record's structure should be versioned appropriately, and call sites should continue to publish in a backwards compatible manner.</remarks>
	public record FingerprintTelemetry(string Type, IReadOnlySet<IssueKey> Keys)
	{
		/// <summary>
		/// Constructor.
		/// </summary>
		/// <param name="issueFingerprint">The issueFingerprint to use when constructing the telemetry.</param>
		public FingerprintTelemetry(IIssueFingerprint issueFingerprint)
			: this(issueFingerprint.Type, issueFingerprint.Keys)
		{
		}
	}

	/// <summary>
	/// Record used for FailreInfo telemetry.
	/// </summary>
	/// <remarks>Modifications to this record's structure should be versioned appropriately, and call sites should continue to publish in a backwards compatible manner.</remarks>
	public record FailureInfoTelemetry(JobId JobId, string JobName, CommitIdWithOrder Change, ObjectId StepId);

	/// <summary>
	/// Record used for IssueSpan telemetry. Modles a <see cref="IIssueSpan"/>.
	/// </summary>
	/// <remarks>Modifications to this record's structure should be versioned appropriately through <see cref="SchemaVersion"/> via <see cref="CurrentSchemaVersion"/>, and call sites should continue to publish in a backwards compatible manner.</remarks>
	public record IssueSpanTelemetry(string EventName, int SchemaVersion, ObjectId Id, int IssueId, FingerprintTelemetry Fingerprint, FailureInfoTelemetry FirstFailure, FailureInfoTelemetry? LastFailure, StreamId StreamId, string StreamName, TemplateId TemplateRefId)
	{
		/// <summary>
		/// Default event name for the IssueTelemetry.
		/// </summary>
		public const string DefaultEventName = "State.IssueSpan";

		/// <summary>
		/// Current schema version.
		/// </summary>
		public static readonly int CurrentSchemaVersion = 1;

		/// <summary>
		/// Constructor.
		/// </summary>
		/// <param name="issueSpan">The issueSpan to use when constructing the telemetry.</param>
		public IssueSpanTelemetry(IIssueSpan issueSpan) : this(
			DefaultEventName,
			CurrentSchemaVersion,
			issueSpan.Id,
			issueSpan.IssueId,
			new FingerprintTelemetry(issueSpan.Fingerprint),
			new FailureInfoTelemetry(issueSpan.FirstFailure.JobId, issueSpan.FirstFailure.JobName, issueSpan.FirstFailure.CommitId, issueSpan.FirstFailure.SpanId),
			issueSpan.LastFailure != null
				? new FailureInfoTelemetry(issueSpan.LastFailure.JobId, issueSpan.LastFailure.JobName, issueSpan.LastFailure.CommitId, issueSpan.LastFailure.SpanId)
				: null,
			issueSpan.StreamId,
			issueSpan.StreamName,
			issueSpan.TemplateRefId)
		{
		}
	}
}
