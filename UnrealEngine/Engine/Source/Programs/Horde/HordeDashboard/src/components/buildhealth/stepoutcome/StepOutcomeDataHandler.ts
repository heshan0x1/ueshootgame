// Copyright Epic Games, Inc. All Rights Reserved.

import { GetJobResponse, IssueQuery, JobStreamQuery } from "horde/backend/Api";
import { PollBase } from "horde/backend/PollBase";
import { StepOutcomeTable, StepOutcomeTableEntry, UpdatedChangeSummaryData } from "./StepOutcomeDataTypes";
import backend from "horde/backend";
import { decodeStepKey, decodeTemplateKey, encodeStepNameFromStrings } from "../BuildHealthUtilities";
import { action, computed, makeObservable, observable, runInAction } from "mobx";

// #region -- API Data Types & Utilities --

/**
 * Utility function to create a formatted string of the StepOutcomeFilters object for messaging.
 * @param filters The filters object to use in creating the string.
 * @returns A formated string representation of the StepOutcomeFilters.
 */
export function prettyPrintStepOutcomeFilters(filters: StepOutcomeFilters): string {
    const parts: string[] = [];

    parts.push(`Streams: ${filters.streams.join(", ") || "(none)"}`);

    const start = filters.jobHistorySpan.start.toISOString().split("T")[0];
    const end = filters.jobHistorySpan.end ? filters.jobHistorySpan.end.toISOString().split("T")[0] : "(ongoing)";

    parts.push(`Job History: ${start} → ${end}`);

    if (filters.jobs?.length) {
        parts.push(`Jobs: ${filters.jobs.join(", ")}`);
    }

    if (filters.steps?.length) {
        parts.push(`Steps: ${filters.steps.join(", ")}`);
    }

    if (filters.maxJobCount !== undefined) {
        parts.push(`Max Job Count: ${filters.maxJobCount}`);
    }

    return parts.join("\n");
}

/**
 * Utility function to create a formatted string of the StepOutcomeFilters object for table summary.
 * @param filters The filters object to use in creating the strings.
 * @returns A three string representation of the filter as a summary: header, sub header, secondary sub header.
 */
export function prettyPrintStepOutcomeFiltersSummary(filters: StepOutcomeFilters): [string, string, string] {
    // Header: Stream(s)
    const header = `Streams: ${filters.streams.join(", ") || "(none)"}`;

    // Subheader: Job(s) - Steps
    const jobs = filters.jobs?.length
        ? (filters.jobs.length > 3 ? "(many)" : filters.jobs.join(", "))
        : "(all jobs)";

    const steps = filters.steps?.length
        ? (filters.steps.length > 3 ? "(many)" : filters.steps.join(", "))
        : "(all steps)";

    const subHeader = `Jobs: ${jobs} - Steps: ${steps}`;

    // Sub-subheader: Last N Days/Hours
    const start = filters.jobHistorySpan.start;
    const end = filters.jobHistorySpan.end || new Date();

    const diffMs = end.getTime() - start.getTime();
    const diffDays = Math.floor(diffMs / (1000 * 60 * 60 * 24));

    let subSubHeader: string;
    if (diffDays >= 1) {
        subSubHeader = `Last ${diffDays} day${diffDays > 1 ? "s" : ""}`;
    } else {
        const diffHours = Math.ceil(diffMs / (1000 * 60 * 60));
        subSubHeader = `Last ${diffHours} hour${diffHours > 1 ? "s" : ""}`;
    }

    return [header, subHeader, subSubHeader];
}

/**
 * Data structure that controls the filtering & queries of the Data Handler.
 * @todo UE-309726 - This data structure can be simplified to reduce duplication, and reduce the need for the internal container to decode.
 */
export interface StepOutcomeFilters {
    streams: string[];
    jobHistorySpan: { start: Date; end?: Date };
    jobs?: string[];
    steps?: string[];
    maxJobCount?: number;
    includePreflights: boolean;
    includeDateAnchors: boolean;
}

// #endregion -- API Data Types & Utilities --

/**
 * Data handler for all step outcome data used for step outcomes.
 * @todo Data sharing & caching would ideally occur between the BuildHealth View & Step Outcome.
 */
export class StepOutcomeDataHandler extends PollBase {
    private readonly STREAM_JOBS_FILTER = "id,streamId,name,change,templateId,state,createTime,updateTime,batches,preflightCommitId,preflightDescription,cancellationReason";
    private readonly STREAM_CHANGES_FILTER = "id,dateUtc";
    private readonly MAX_JOB_COUNT_DEFAULT = 150;

    // #region -- Private Members -- 

    @observable
    private internalLastRefreshDate?: Date;

    @observable
    private intialRefresh = true;
    @observable
    private activeRefresh: boolean = false;

    private activeFilter: StepOutcomeFilters;
    private jobData: GetJobResponse[] = [];
    private changeData: Map<number, UpdatedChangeSummaryData> = new Map<number, UpdatedChangeSummaryData>();
    private stepOutcomeTableData: StepOutcomeTable = new StepOutcomeTable();

    //#endregion -- Private Members

    // #region -- Constructor --

    /**
     * Cosntructs a StepOutcomeDataHandler.
     * @param pollTime The initial poll refresh time.
     */
    constructor(pollTime: number) {
        super(pollTime);
        makeObservable(this);
    }

    //#endregion -- Constructor --

    // #region -- Interface --

    @action
    async poll(): Promise<void> {
        if (this.activeRefresh ||
            this.activeFilter === undefined ||
            !this.activeFilter.streams?.length ||
            !this.activeFilter.jobs?.length
        ) {
            this.stepOutcomeTableData.reset();
            this.setUpdated();
            return;
        }
        runInAction(() => {
            this.activeRefresh = true;
        });

        this.stepOutcomeTableData.reset();

        // @todo UE-309726 - The poll should not need to understand internal data structure; the filter object should be already decoded.
        const stepMap: Map<string, Map<string, Set<string>>> = new Map();

        (this.activeFilter.steps ?? []).forEach(step => {
            const { streamId, templateId, stepName } = decodeStepKey(step);

            if (!stepMap.has(streamId)) {
                stepMap.set(streamId, new Map());
            }

            const templateMap = stepMap.get(streamId)!;

            if (!templateMap.has(templateId)) {
                templateMap.set(templateId, new Set());
            }

            templateMap.get(templateId)!.add(step.toLocaleLowerCase());
        });

        const streamList = this.activeFilter.streams ?? [];
        const modifiedAfter = this.activeFilter.jobHistorySpan.start.toISOString();
        const jobCount = this.activeFilter.maxJobCount ?? this.MAX_JOB_COUNT_DEFAULT;

        // Group jobs by streamId
        const jobsByStream: Record<string, string[]> = {};

        (this.activeFilter.jobs ?? []).forEach(jobKey => {
            // @todo UE-309726 - The poll should not need to understand internal data structure; the filter object should be already decoded.
            const { streamId, templateId } = decodeTemplateKey(jobKey);
            if (!jobsByStream[streamId]) {
                jobsByStream[streamId] = [];
            }
            jobsByStream[streamId].push(templateId);
        });

        const queries: { streamId: string; query: JobStreamQuery }[] = [];
        const changeQueries: { streamId: string; query: any }[] = [];

        for (const streamId of streamList) {
            const jobsForStream = jobsByStream[streamId] ?? [];
            for (const templateId of jobsForStream) {
                const query: JobStreamQuery = {
                    template: [templateId],
                    count: jobCount,
                    filter: this.STREAM_JOBS_FILTER,
                    modifiedAfter: modifiedAfter
                };
                queries.push({ streamId, query });
                changeQueries.push({ streamId: streamId, query: { filter: this.STREAM_CHANGES_FILTER } });
            }
        }

        // Parallelize backend calls for stream jobs
        const results = await Promise.all(
            queries.map(({ streamId, query }) => backend.getStreamJobs(streamId, query))
        );

        // Annotate the changes that we need to request in getChangeSummaries.
        let jobHistoryCount: Map<string, number[]> = new Map<string, number[]>();

        results.forEach((jobs, i) => {
            const streamId = queries[i].streamId;

            const newChanges = jobs
                .map(job => job.change!)
                .filter(change => !this.changeData.has(change));

            if (newChanges.length > 0) {
                if (!jobHistoryCount.has(streamId)) {
                    jobHistoryCount.set(streamId, []);
                }

                jobHistoryCount.get(streamId)!.push(...newChanges);
            }
        });

        // Flatten all jobData
        this.jobData = results.flat();

        if (jobHistoryCount.size > 0) {
            // Parallelize backend calls for change summaries
            const changeResults = await Promise.all(
                changeQueries.map(({ streamId, query }) => backend.getChangeSummaries(streamId, undefined, undefined, jobHistoryCount.get(streamId), query.filter))
            );

            let flattenedResults = changeResults.flat();
            flattenedResults.map((value: UpdatedChangeSummaryData) => {
                if (!this.changeData.has(value.id.order)) {
                    this.changeData.set(value.id.order, value);
                }
            });

            console.info(`Retrieved: ${flattenedResults.length} change summary results for StepOutcome query.`);
        }

        for (const streamResult of results) {
            // Process all job responses
            for (const jobResponse of streamResult) {
                // Filter preflights as appropriate
                if (!this.activeFilter.includePreflights && (jobResponse.preflightChange || jobResponse.preflightDescription)) {
                    continue;
                }

                const batchResponses = jobResponse.batches ?? [];
                for (const batch of batchResponses) {
                    for (const step of batch.steps) {
                        let fullyQualifiedStepName = encodeStepNameFromStrings(jobResponse.streamId, jobResponse.templateId!, step.name).toLocaleLowerCase();
                        let streamStepSet = stepMap.get(jobResponse.streamId);
                        let templateStepSet = streamStepSet ? streamStepSet.get(jobResponse.templateId!) : undefined;
                        if (
                            !templateStepSet ||
                            templateStepSet.size === 0 ||
                            templateStepSet.has(fullyQualifiedStepName)
                        ) {

                            this.stepOutcomeTableData.addEntry(
                                new StepOutcomeTableEntry(step, jobResponse.id, jobResponse.name, jobResponse.streamId, jobResponse.createTime.toString(), jobResponse.change), this.changeData.get(jobResponse.change!)
                            );
                        }
                    }
                }

                // Summarize each row
            }
        }
        
        this.StepOutcomeTableData.summarize();

        console.info(`Retrieved: ${this.jobData.length} job instance results for StepOutcome query.`);

        this.stepOutcomeTableData.orderTableDataByChange(true);

        this.StepOutcomeTableData.supportsDateAnchoredChanges = this.activeFilter.includeDateAnchors;

        this.setUpdated();

        runInAction(() => {
            this.activeRefresh = false;
            this.intialRefresh = false;
            this.internalLastRefreshDate = new Date();
        });

        return Promise.resolve();
    }

    /**
     * Will populate the provided stepOutcomeTableEntry's issue data field,
     * @param stepOutcomeTableEntry The step outcome table entry to attempt to populate with issue data.
     */
    async populateStepOutcomeTableEntryIssueData(stepOutcomeTableEntry: StepOutcomeTableEntry, onCompleteCallback?: (boolean) => void): Promise<void> {
        let unresolvedIssueQuery: IssueQuery = { jobId: stepOutcomeTableEntry.jobId, stepId: stepOutcomeTableEntry.stepResponse.id, resolved: false }
        const unresolvedIssueResults = await backend.getIssues(unresolvedIssueQuery);
        stepOutcomeTableEntry.issuesData = unresolvedIssueResults;

        let resolvedIssueQuery: IssueQuery = { jobId: stepOutcomeTableEntry.jobId, stepId: stepOutcomeTableEntry.stepResponse.id, resolved: true }
        const resolvedIssueResults = await backend.getIssues(resolvedIssueQuery);
        stepOutcomeTableEntry.issuesData.push(...resolvedIssueResults);

        onCompleteCallback?.(true);
        return Promise.resolve();
    }

    // #endregion -- Interface --

    // #region -- Public API  --

    /**
     * Gets the last refresh date.
     * @return The last refresh date, if a refresh has occurred. 
     */
    @computed
    get lastRefreshDate(): Date | undefined {
        return this.internalLastRefreshDate;
    }

    /**
     * Gets whether the handler has yet to initiate the initial refresh.
     * @returns True if the handler has not issued a initial refresh, false otherwise.
     */
    @computed
    get isInitialRefresh(): boolean {
        return this.intialRefresh;
    }

    /**
     * Gets whether an full data refresh is occurring.
     * @returns True if an full data refresh is occurring, false otherwise.
     */
    @computed
    get isInFullDataRefresh(): boolean {
        return this.activeRefresh && this.intialRefresh;
    }

    /**
     * Gets whether an incremental data refresh is occurring.
     * @returns True if an incremental data refresh is occurring, false otherwise.
     */
    @computed
    get isInIncrementalDataRefresh(): boolean {
        return this.activeRefresh && !this.intialRefresh;
    }

    /**
     * Gets the step outcome table data.
     * @returns The instance Step Outcome Data Table.
     */
    get StepOutcomeTableData(): StepOutcomeTable {
        return this.stepOutcomeTableData;
    }

    /**
     * Sets a new filter for the handler, which will control the underlying dataset.
     * @param filter The new filter to use.
     */
    @action
    setFilter(filter: StepOutcomeFilters) {
        this.activeFilter = filter;
        this.intialRefresh = true;
        this.poll();
    }

    /**
     * Gets the current refresh cadence of the data handler.
     * @returns The handler refresh cadence in ms.
     */
    get refreshTime(): number {
        return this.pollTime;
    }

    /**
     *  Sets a new refresh time for the handler, in ms.
     * @param newRefreshTime The new refresh rate.
     */
    setRefreshTime(newRefreshTime: number) {
        this.pollTime = newRefreshTime;
    }

    // #endregion -- Public API  --
}