// Copyright Epic Games, Inc. All Rights Reserved.

import { ChangeSummaryData, GetStepResponse, IssueData } from "horde/backend/Api";
import { computeDuration, DURATION_NOT_SET, formatDurationFromMs, toDate } from "./StepOutcomeUtilities";

export const WarningStr: string = "Warnings";
export const SuccessStr: string = "Success";
export const SkippedStr: string = "Skipped";
export const FailureStr: string = "Failure";
export const WaitingStr: string = "Waiting";
export const RunningStr: string = "Running";

/**
 * Encodes a step outcome table entry to a stream qualified step name.
 * Note: The template is not included in the hierarchy qualification since two separate templates within the same stream will collocate the same step row.
 * @param stepOutcomeTableEntry The stepOutcomeTableEntry to encode.
 * @returns The hierarchical stream qualified step name.
 */
export function encodeStepName(stepOutcomeTableEntry: StepOutcomeTableEntry) {
    return `${stepOutcomeTableEntry.streamId.toLocaleLowerCase()}${KEY_SEPARATOR}${stepOutcomeTableEntry.stepResponse.name.toLocaleLowerCase()}`;
}

/**
 * Encodes a step outcome table entry to a stream qualified step name.
 * Note: The template is not included in the hierarchy qualification since two separate templates within the same stream will collocate the same step row.
 * @param stepNameHeader The stepNameHeader to encode.
 * @returns The hierarchical stream qualified step name.
 */
export function encodeStepNameFromStepNameHeader(stepNameHeader: StepNameHeader) {
    return `${stepNameHeader.streamId.toLocaleLowerCase()}${KEY_SEPARATOR}${stepNameHeader.stepName.toLocaleLowerCase()}`;
}

// #region -- Constants --

const KEY_SEPARATOR: string = "::";

// #endregion -- Constants --

// #region -- Data Types --

/**
 * ChangeId type that has the string & numerical representation of a commit id.
 */
export type ChangeId = {
    name: string
    order: number
}

/**
 * Convenience type to fold in change id into the existing ChangeSummaryData.
 */
export type UpdatedChangeSummaryData = ChangeSummaryData & {
    id: ChangeId
}

/*
 * Convenience type to fold in extra metadata relevant for the StepResponse.
 */
export type StepReponseData = GetStepResponse & {
    stepDurationMs: number;
}

// #region -- Table Entry Types --

/**
 * Discriminated union which represents a tabel entry. This can be a @see StepOutcomeTableEntry (the core data in the table) or a @see SummaryTableEntry (the summarization of an entire row).
 */
export type TableEntry = StepOutcomeTableEntry | SummaryTableEntry | undefined;

/**
 * Type predicate to test whether this is a @see StepOutcomeTableEntry .
 * @param entry The entry to test.
 * @returns True if it is a @see StepOutcomeTableEntry - false otherwise.
 */
export function isStepOutcome(entry: TableEntry): entry is StepOutcomeTableEntry {
    return entry instanceof StepOutcomeTableEntry;
}

/**
 * Type predicate to test whether this is a @see SummaryTableEntry .
 * @param entry The entry to test.
 * @returns True if it is a @see SummaryTableEntry - false otherwise.
 */
export function isSummary(entry: TableEntry): entry is SummaryTableEntry {
    return entry instanceof SummaryTableEntry;
}

/**
 * Data structure that represents a step summary for a collection of @see StepOutcomeTableEntry data.
 */
export class SummaryTableEntry {
    steps: number = 0;
    stepsCompleted: number = 0;
    stepsPass: number = 0;
    stepsFail: number = 0;
    stepsWarning: number = 0;
    stepsPending: number = 0;
}

/**
 * Model for a table entry.
 */
export class StepOutcomeTableEntry {
    // #region -- Public Members --

    change?: number;
    stepResponse: StepReponseData;
    jobId: string;
    jobName: string;
    streamId: string;
    jobCreateTime: string;
    issuesData: IssueData[] = [];

    // #endregion -- Public Members --

    // #region -- Constructor --

    /**
     * Constructor.
     * @param stepResponse The step response object for the table entry.
     * @param jobId The owning job's Id.
     * @param jobName The owning job's name.
     * @param streamId The owning streams's Id.
     * @param jobCreateTime The owning job's create time.
     * @param change The change associated with the stepResponse.
     */
    constructor(stepResponse: GetStepResponse, jobId: string, jobName: string, streamId: string, jobCreateTime: string, change?: number) {
        if (change !== undefined) {
            this.change = change;
        }

        this.stepResponse = { ...stepResponse, stepDurationMs: computeDuration(stepResponse.startTime, stepResponse.finishTime) };
        this.jobId = jobId;
        this.jobName = jobName;
        this.streamId = streamId;
        this.jobCreateTime = jobCreateTime
    }

    // #endregion -- Constructor --

    // #region -- Public API --

    /**
     * Gets the duration of the underyling @see StepReponseData in string representation.
     * @returns The string representation of the step duration.
     */
    getDurationString(): string {
        let durationStr: string = this.stepResponse.state === "Skipped" ? "N/A" : "Ongoing";

        if (this.stepResponse.stepDurationMs != DURATION_NOT_SET) {
            durationStr = formatDurationFromMs(this.stepResponse.stepDurationMs);
        }

        return durationStr;
    }
    // #endregion -- Public API --
}

// #endregion -- Table Entry Types --

// #region -- Table Header Types --

/**
 * Discriminated union which represents a tabel entry. This can be a @see ChangeHeader (the fundamental change header in the table) or a @see SummaryHeader (the summarization of an entire row).
 */
export type ColumnHeader = ChangeHeader | SummaryHeader | DateHeader;

/**
 * Type predicate to test whether this is a @see SummaryHeader .
 * @param header the header to apply the predicate to.
 * @returns True if the header was a @see SummaryHeader ; false otherwise.
 */
export function isSummaryHeader(header: ColumnHeader): header is SummaryHeader {
    return header.type === "summary";
}

/**
 * Type predicate to test whether this is a @see DateHeader .
 * @param header the header to apply the predicate to.
 * @returns True if the header was a @see DateHeader ; false otherwise.
 */
export function isDateHeader(header: ColumnHeader): header is DateHeader {
    return header.type === "date";
}

/**
 * Type predicate to test whether this is a @see ChangeHeader .
 * @param header the header to apply the predicate to.
 * @returns True if the header was a @see ChangeHeader ; false otherwise.
 */
export function isChangeHeader(header: ColumnHeader): header is ChangeHeader {
    return header.type === "change";
}

/**
 * Data struct that represents a summary header.
 */
export type SummaryHeader = {
    type: "summary";
    name: string;
}

/**
 * Data struct that represents a change header. This header contains associated data related to rendering & ordering the header.
 */
export type ChangeHeader = {
    type: "change";
    change: number;
    date?: string;
}

/**
 * Data struct that represents a date header. This header contains associated data related to rendering & ordering the header.
 */
export type DateHeader = {
    type: "date";
    date: string;
}

/**
 * Data struct that represents a step name header. This header contains associated data related to rendering & ordering the header.
 */
export type StepNameHeader = {
    stepName: string;
    streamId: string;
}

// #region -- Table Header Types --

/**
 * Model for the StepOutcomeTable.
 */
export class StepOutcomeTable {
    // #region -- Public Members --

    dataCount: number = 0;

    /**
     * Sets whether @see getDateAnchoredChanges is supported for the given data.
     * @todo - UE-315881 will remove the need for this constraint.
     */
    supportsDateAnchoredChanges: boolean = true;

    /**
     * Change column headers.
     */
    changeColLookup: Map<number, number> = new Map<number, number>();
    changeColHeaders: ChangeHeader[] = [];
    changeOrderAscend: boolean = false;

    /**
     * Step name row headers.
     */
    stepRowLookup: Map<string, number> = new Map<string, number>();
    stepRowNumberLookup: Map<number, string> = new Map<number, string>();
    stepNameRowHeaders: StepNameHeader[] = [];

    /**
     * SummaryLookup maps a normalized step name to a summary entry.
     */
    summaryLookup: Map<string, SummaryTableEntry> = new Map<string, SummaryTableEntry>();

    tableStreamMetadata: Map<string, number> = new Map<string, number>();

    /**
     * Table data structure
     */
    tableEntries: (StepOutcomeTableEntry | null)[][] = [];

    // #endregion --  Public Members --

    // #region -- Private API --

    private ensureRow(source: (StepOutcomeTableEntry | null)[], targetSize: number) {
        while (targetSize > source.length) {
            source.push(null);
        }
    }

    private annotateStreamRow(streamId: string) {
        let priorCount: number = this.tableStreamMetadata.get(streamId) ?? 0;
        if (priorCount) {
            this.tableStreamMetadata.set(streamId, priorCount);
        }
        priorCount = priorCount + 1;
        this.tableStreamMetadata.set(streamId, priorCount);
    }

    // #endregion -- Private API --

    // #region -- Public API --

    /**
     * Resets the table data. This will remove all data from the container.
     */
    reset(): void {
        this.dataCount = 0;
        this.tableEntries = [];
        this.stepNameRowHeaders = [];
        this.stepRowLookup.clear();
        this.stepRowNumberLookup.clear();
        this.summaryLookup.clear();
        this.changeColHeaders = [];
        this.changeColLookup.clear();
        this.tableStreamMetadata.clear();
    }

    /**
     * Adds a new entry to the table data.
     * @param tableEntry The table entry to add to the data table.
     * @param changeSummaryData The change summary data corresponding to the @see tableEntry.
     * @returns If tableEntry parameter is invalid, returns.
     */
    addEntry(tableEntry: StepOutcomeTableEntry, changeSummaryData: UpdatedChangeSummaryData | undefined): void {
        if (tableEntry.change === undefined) {
            return;
        }

        const normalizedStepName = encodeStepName(tableEntry);

        // Resolve or create row index
        let insertionRow = this.stepRowLookup.get(normalizedStepName);
        if (insertionRow === undefined) {
            this.annotateStreamRow(tableEntry.streamId);
            insertionRow = this.stepNameRowHeaders.length;
            this.stepRowLookup.set(normalizedStepName, insertionRow); // cache the normalized step name -> row number
            this.stepRowNumberLookup.set(insertionRow, normalizedStepName); // cache the row number -> normalized step name
            this.stepNameRowHeaders.push({ streamId: tableEntry.streamId, stepName: tableEntry.stepResponse.name });
        }

        let insertionCol = this.changeColLookup.get(tableEntry.change);
        if (insertionCol === undefined) {
            insertionCol = this.changeColHeaders.length;
            this.changeColLookup.set(tableEntry.change, insertionCol);
            this.changeColHeaders.push({ type: "change", change: tableEntry.change, date: toDate(changeSummaryData?.dateUtc)?.toISOString() });

            for (const row of this.tableEntries) {
                if (row) {
                    this.ensureRow(row, this.changeColHeaders.length);
                }
            }
        }

        // Ensure the row exists and is padded
        if (this.tableEntries[insertionRow] === undefined) {
            this.tableEntries[insertionRow] = Array(this.changeColHeaders.length).fill(null);
        } else {
            this.ensureRow(this.tableEntries[insertionRow], this.changeColHeaders.length);
        }

        const existingEntry = this.tableEntries[insertionRow][insertionCol];
        if (existingEntry !== null) {
            console.warn(`Duplicate entry for step new (${tableEntry.streamId}::${tableEntry.jobName}::${tableEntry.jobId}::${tableEntry.stepResponse.name} (pre-existing: ${existingEntry.streamId}::${existingEntry.jobName}::${existingEntry.jobId}::${existingEntry.stepResponse.name})) and change ("${tableEntry.change}" (${existingEntry.change})). Ambiguous selector needed.`, {
                existingEntry,
                newEntry: tableEntry
            });
            return;
        }

        this.dataCount++;
        this.tableEntries[insertionRow][insertionCol] = tableEntry;
    }

    /**
     * Summarizes all of the data currently stored within the step outcome table.
     * A @see SummaryTableEntry is the step completion (across failure & success states) for all rows in the table.
     */
    summarize() {
        for (let rowIndex = 0; rowIndex < this.tableEntries.length; rowIndex++) {
            const row = this.tableEntries[rowIndex];
            if (!this.stepRowNumberLookup.has(rowIndex)) {
                continue;
            }

            let summaryTableEntry: SummaryTableEntry = new SummaryTableEntry();
            let normalizedTableEntryName: string = this.stepRowNumberLookup.get(rowIndex)!;
            this.summaryLookup.set(normalizedTableEntryName, summaryTableEntry);

            for (let colIndex = 0; colIndex < row.length; colIndex++) {
                const cell = row[colIndex];
                if (cell == null) {
                    continue;
                }
                summaryTableEntry.steps++;

                if (cell.stepResponse.state === WaitingStr || cell.stepResponse.state === RunningStr) {
                    summaryTableEntry.stepsPending++;
                    continue;
                }

                summaryTableEntry.stepsCompleted++;

                if (cell.stepResponse.outcome == WarningStr) {
                    summaryTableEntry.stepsWarning++;
                }
                else if (cell.stepResponse.outcome == FailureStr) {
                    summaryTableEntry.stepsFail++;
                }
                else if (cell.stepResponse.outcome == SuccessStr) {
                    summaryTableEntry.stepsPass++;
                }
            }
        }
    }

    /**
     * Retrieves a step row summary for a provided row.
     * @param row The normalized row name, or number.
     * @returns The step summary for that row if it exists, undefined otherwise.
     */
    getStepRowSummary(row: string | number): TableEntry {
        let rowString: string | undefined = undefined;
        if (typeof row === 'number') {
            rowString = this.stepRowNumberLookup.get(row);
        } else {
            rowString = row;
        }

        let summaryTableEntry: SummaryTableEntry | undefined = this.summaryLookup.has(rowString!) ? this.summaryLookup.get(rowString!) : undefined;

        return summaryTableEntry;
    }

    /**
     * Gets all of the Table Entries in a row.
     * @param * Generator of table entries.
     */
    * getStepOutputTableEntries(row: string | number): Generator<TableEntry> {
        yield* this.getStepOutputRow(row);
        yield this.getStepRowSummary(row);
    }

    /**
     * Gets the Step Outcome Table Entries for a given step name.
     * @param rowName The step name to obtain.
     * @returns * Generator of TableEntries.
     */
    getStepOutputRow(rowName: string): Generator<StepOutcomeTableEntry>;
    getStepOutputRow(rowIndex: number): Generator<StepOutcomeTableEntry>;
    getStepOutputRow(row: string | number): Generator<StepOutcomeTableEntry>;
    *getStepOutputRow(row: string | number): Generator<StepOutcomeTableEntry | null | undefined> {
        let rowIdx: number | undefined;

        if (typeof row === 'string') {
            rowIdx = this.stepRowLookup.get(row);
        } else {
            rowIdx = row;
        }

        if (rowIdx === undefined || rowIdx < 0 || rowIdx >= this.tableEntries.length) {
            return;
        }

        const rowEntries = this.tableEntries[rowIdx];

        for (const entry of this.getDateAnchoredChanges()) {
            if (isDateHeader(entry)) {
                yield undefined;
            }
            else {
                const colIdx = this.changeColLookup.get(entry.change);
                yield colIdx !== undefined ? rowEntries[colIdx] : undefined;

            }
        }
    }

    /**
     * Gets the Table Entries for the given change.
     * @param changeNumber The change number.
     * @returns * Generator of all Table Entries for the specific change.
     */
    *getChangeColumn(changeNumber: number): Generator<StepOutcomeTableEntry> {

        let colIdx: (number | undefined) = this.changeColLookup.get(changeNumber);
        if (colIdx === undefined) {
            return;
        }

        for (const entry of this.stepNameRowHeaders) {
            let rowIdx: (number | undefined) = this.stepRowLookup.get(encodeStepNameFromStepNameHeader(entry));
            if (rowIdx === undefined) {
                continue;
            }

            let tableEntry: (StepOutcomeTableEntry | null) = this.tableEntries[rowIdx][colIdx];
            if (tableEntry !== null) {
                yield tableEntry;
            }
        }
    }

    /**
     * Sets the table data's change (column) order.
     * @param ascending True for ascending order; false for descending order.
     */
    orderTableDataByChange(ascending: boolean): void {
        this.changeColHeaders.sort((a, b) => ascending ? a.change - b.change : b.change - a.change);
        this.changeOrderAscend = ascending;
    }

    /**
     * Sets the table data's step (row) order.
     * @param ascending True for ascending order; false for descending order.
     */
    orderTableDataByStepName(ascending: boolean): void {
        this.stepNameRowHeaders.sort((a, b) => {
            if (a < b) return ascending ? -1 : 1;
            if (a > b) return ascending ? 1 : -1;
            return 0;
        });
    }

    /**
     * Gets all of the column headers for the table.
     * @returns * Generator of all the column headers.
     */
    *getColumnHeaders(): Generator<ColumnHeader> {
        yield* this.getDateAnchoredChanges();
        yield {
            type: "summary",
            name: "Summary"
        }
    }

    /**
     * Gets the the date anchored changes. 
     * @returns * Generator of all of the @see ChangeHeader items separated by @see DateHeader that represent dates. 
     */
    private *getDateAnchoredChanges(): Generator<ChangeHeader | DateHeader> {
        if (!this.supportsDateAnchoredChanges) {
            for (const header of this.changeColHeaders) {
                yield header;
            }

            return;
        }

        let prevDay: string | null = null;
        for (let i: number = 0; i < this.changeColHeaders.length; ++i) {

            // If the date associated with the changelist header has changed since the last, we are on a new day. Set that as the colimn.
            let changeHeader = this.changeColHeaders[i];
            if (changeHeader.date !== undefined) {
                const headerDate = new Date(changeHeader.date!);

                // Extract the day portion as YYYY-MM-DD
                const dayStr = `${headerDate.getUTCFullYear()}-${headerDate.getUTCMonth() + 1}-${headerDate.getUTCDate()}`;

                if (prevDay !== dayStr) {
                    prevDay = dayStr;
                    let dateAnchorChangelistHeader: DateHeader = {
                        type: "date",
                        date: dayStr
                    }
                    yield dateAnchorChangelistHeader;
                }
                yield changeHeader;
            }
        }
    }

    // #endregion -- Public API --
}

// #endregion -- Data Types  --