// Copyright Epic Games, Inc. All Rights Reserved.

import { mergeStyleSets, Spinner, SpinnerSize } from "@fluentui/react";
import dashboard, { StatusColor } from "horde/backend/Dashboard";
import { observer } from "mobx-react-lite";
import { StepOutcomeDataHandler } from "./StepOutcomeDataHandler";
import { FailureStr, SkippedStr, StepOutcomeTableEntry, SuccessStr, WarningStr } from "./StepOutcomeDataTypes";

// #region -- Constants --

export const ROW_HEADER_NAME: string = "stepName" as const;

// #endregion -- Constants --

// #region -- Styles --

export const sharedBorderStyle = {
    bottomBorder: {
        borderBottom: `1px solid rgba(80, 80, 80, 1)`
    }
};

export const stepOutcomeTableClasses = mergeStyleSets({
    root: {
        maxWidth: "min(85vw, 2400px)",
    },
});

// #endregion -- Styles --

/**
 * Gets the color records based off of the string representations of @see StatusColor - which maps to the status string.
 * @returns A record that maps sates to their corresponding colours. 
 */
export function getColorRecords(): Record<string, string> {
    const scolors = dashboard.getStatusColors();
    const colors: Record<string, string> = {
        "Success": scolors.get(StatusColor.Success)!,
        "Failure": scolors.get(StatusColor.Failure)!,
        "Warnings": scolors.get(StatusColor.Warnings)!,
        "Skipped": scolors.get(StatusColor.Skipped)!,
        "Running": scolors.get(StatusColor.Running)!,
        "Waiting": scolors.get(StatusColor.Waiting)!,
        "Ready": scolors.get(StatusColor.Ready)!,
        "Unspecified": scolors.get(StatusColor.Skipped)!,
    };

    return colors;
}

/**
 * Gets the cell style given the provided StepOutcomeTableEntry.
 * @param entry The entry to base the style off of.
 * @returns The matching style given the state of the entry. 
 */
export function getCellStyle(entry: StepOutcomeTableEntry | null): { bgColor: string } {
    const colors: Record<string, string> = getColorRecords();

    let resultColor: string = 'transparent';

    if (!entry) {
        return { bgColor: resultColor };
    }

    const { stepResponse } = entry;
    const { outcome, state } = stepResponse;

    // There are some interesting mappings going on internally with respect to state & outcome. Outcome & State have overlapping "result view" space, and as such we need to 
    // narrowly select the edge cases (e.g. outcome=success && state=completed uniquely signifies a completed run with no errors, otherwise outcome=success it could mean ready, waiting, or running).
    // Then we can fall back to the state as the truth.
    if (outcome === FailureStr) {
        resultColor = state === SkippedStr ? colors[SkippedStr] : colors[FailureStr];
    }
    // A step can be emitting warnings whilst still running, so we narrowly select this colouring.
    else if (outcome === WarningStr) {
        resultColor = colors[WarningStr];
    }
    // A step can have a completed outcome, but merely be waiting to run, running, or ready.
    else if (outcome === SuccessStr && state === "Completed") {
        resultColor = colors[SuccessStr];
    }
    // The edge cases have been satisfied, yield to the state to define result view color.
    else {
        resultColor = colors[state];
    }

    return { bgColor: resultColor };
}

/**
 * React Component that provides refresh spinner on incremental refresh. 
 * Note: This is a separate component to prevent rerender of primary table @see GenerateFluentUITable upon refresh spinner.
 * @param handler The data handler used to query step outcome data.
 * @returns React Component.
 */
export const IncrementalRefreshIndicatorPanel: React.FC<{ handler: StepOutcomeDataHandler }> = observer(({ handler }) => (
    <div
        style={{
            gridRow: '1 / span 2',
            gridColumn: 3,
            overflowY: 'auto',
            width: 25,
        }}
    >
        {handler.isInIncrementalDataRefresh && <Spinner size={SpinnerSize.small} />}
    </div>
));