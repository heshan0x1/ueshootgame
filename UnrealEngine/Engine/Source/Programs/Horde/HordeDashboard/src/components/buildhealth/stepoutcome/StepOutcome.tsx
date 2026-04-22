// Copyright Epic Games, Inc. All Rights Reserved.

import { Dialog, DialogType, Label, mergeStyleSets, PrimaryButton, ScrollablePane, Spinner, SpinnerSize, Stack } from "@fluentui/react";
import { Text } from '@fluentui/react/lib/Text';
import { observer } from "mobx-react-lite";
import React, { useEffect, useState } from 'react';
import { StepOutcomeD3Table } from "./StepOutcomeD3Component";
import { prettyPrintStepOutcomeFilters, prettyPrintStepOutcomeFiltersSummary, StepOutcomeDataHandler, StepOutcomeFilters } from "./StepOutcomeDataHandler";
import { StepOutcomeTableEntry } from "./StepOutcomeDataTypes";
import { sharedBorderStyle, stepOutcomeTableClasses } from "./StepOutcomeSharedUIComponents";
import { toDate } from "./StepOutcomeUtilities";

// #region -- View Components --

type StepOutcomeTableProps = {
    filters: StepOutcomeFilters;
    handler: StepOutcomeDataHandler;
    onCellSelected: (item: StepOutcomeTableEntry) => void;
};

interface StepOutcomeViewProps {
    filter: StepOutcomeFilters;
    refreshTime?: number;
}

// #region -- Styles  --

const stepOutcomeModalClasses = mergeStyleSets(sharedBorderStyle);

// #endregion -- Styles  --

// #region -- Modal Popup View --

type StepOutcomeFocusModalProps = {
    item: StepOutcomeTableEntry;
    onDismiss?: (_ev?: React.MouseEvent<HTMLButtonElement>) => any;
};

/**
 * React Component representing the Step Outcome focus.
 */
const StepOutcomeFocusModal: React.FC<StepOutcomeFocusModalProps> = ({ item, onDismiss }) => {
    const startDate = toDate(item.stepResponse.startTime);

    const [issueRequestComplete, setIssueRequestComplete] = useState(false);

    useEffect(() => {
        if (!issueRequestComplete) {
            handler.populateStepOutcomeTableEntryIssueData(item, setIssueRequestComplete);
        }
    }, []);

    return (
        <Dialog
            modalProps={{
                isBlocking: false,
                topOffsetFixed: true,
                styles: {
                    root: {
                        selectors: {
                            ".ms-Dialog-title": {
                                paddingTop: '24px',
                                paddingLeft: '32px'
                            }
                        }
                    }
                }
            }}
            onDismiss={onDismiss}
            hidden={false}
            minWidth={1200}
            dialogContentProps={{
                type: DialogType.close,
                onDismiss: onDismiss,
                title: item.stepResponse.name,
            }}>
            <Stack>
                <Stack horizontal tokens={{ childrenGap: 16 }} styles={{ root: { width: '100%', flexGrow: 0 } }}>

                    {/* Left Column - Step Details*/}
                    <Stack styles={{ root: { width: '35%' } }} tokens={{ childrenGap: 4 }}>
                        {[
                            ['Date', startDate ? startDate.toDateString() : "N/A"],
                            ['Stream', item.streamId],
                            ['Code Change', item.change],
                            ['Job', item.jobName],
                            ['Step', item.stepResponse.name],
                            ['Duration', item.getDurationString()],
                            ['State', item.stepResponse.state],
                            ['Outcome', item.stepResponse.outcome],
                            ['Job Link', <a href={`/job/${item.jobId}`} target="_blank" rel="noopener noreferrer">View Job</a>],
                            ['Step Link', <a href={`/job/${item.jobId}?step=${item.stepResponse.id}`} target="_blank" rel="noopener noreferrer">View Step</a>]
                        ].map(([label, value], index) => (
                            <Stack key={`modal_step_item_${index}`} horizontal tokens={{ childrenGap: 12 }}>
                                <Label styles={{ root: { minWidth: 100, padding: 0 } }}>{label}:</Label>
                                <Text>{value}</Text>
                            </Stack>
                        ))}
                    </Stack>

                    {/* Right Column - Issues*/}
                    <div style={{ width: '65%', maxHeight: 400, overflow: 'auto', position: 'relative' }}>
                        <ScrollablePane styles={{ root: { height: '100%' } }}>
                            <Stack tokens={{ childrenGap: 8 }}>
                                <Stack horizontal tokens={{ childrenGap: 4 }} className={stepOutcomeModalClasses.bottomBorder}>
                                    <Label styles={{ root: { padding: 0 } }}>Issue Count:</Label>
                                    <Text>{item.issuesData.length}</Text>
                                    <Text>[{item.issuesData.filter((issue) => !issue.resolvedAt).length} unresolved, {item.issuesData.filter((issue) => issue.resolvedAt).length} resolved]</Text>
                                </Stack>
                                {item.issuesData.length > 0 ?
                                    <Stack tokens={{ childrenGap: 10 }} styles={{ root: { paddingLeft: 16 } }}>
                                        {item.issuesData.map((issue, index) => (
                                            <Stack key={`modal_issue_item_${index}`} tokens={{ childrenGap: 4 }} className={stepOutcomeModalClasses.bottomBorder}>
                                                {[
                                                    ['Issue', issue.id],
                                                    ['Issue Link', <a href={`/job/${item.jobId}?step=${item.stepResponse.id}&issue=${issue.id}`} target="_blank" rel="noopener noreferrer">View Issue</a>],
                                                    ['Issue Summary', issue.summary],
                                                    ['Status', issue.resolvedAt ? "Resolved" : "Unresolved"],
                                                    ['Resolved Streams', `[${issue.resolvedStreams.join(', ')}]`],
                                                    ['Unresolved Streams', `[${issue.unresolvedStreams.join(', ')}]`]
                                                ].map(([label, value], j) => (
                                                    <Stack key={j} horizontal tokens={{ childrenGap: 12 }}>
                                                        <Label styles={{ root: { minWidth: 150, padding: 0 } }}>{label}:</Label>
                                                        <Text>{value}</Text>
                                                    </Stack>
                                                ))}
                                            </Stack>
                                        ))}
                                    </Stack> : null
                                }
                            </Stack>
                        </ScrollablePane>
                    </div>
                </Stack>
            </Stack>
        </Dialog>
    );
};

// #endregion -- Modal Popup View --

/**
 * React Component that acts as a observable binder for data handler updates, and allows for easy swapping of other table types.
 */
const StepOutcomeTableComponent: React.FC<StepOutcomeTableProps> = observer(({ handler, filters, onCellSelected }) => {
    handler.updated;
    const [dataCount, setDataCount] = useState(() => handler.StepOutcomeTableData.dataCount);

    useEffect(() => {
        setDataCount(handler.StepOutcomeTableData.dataCount);
    }), [handler.StepOutcomeTableData.dataCount];

    const [header, subHeader, subSubHeader] = prettyPrintStepOutcomeFiltersSummary(filters);

    return (
        <Stack>
            {handler.isInFullDataRefresh ? (
                <Spinner style={{ paddingTop: 16 }} size={SpinnerSize.large} />
            ) :
                <Stack>
                    {dataCount > 0 ? (
                        <Stack>
                            <Stack horizontalAlign="center" styles={{ root: { paddingTop: 8, paddingBottom: 8 } }}>
                                <Text styles={{ root: { fontSize: "large", fontWeight: 'bold' } }}>{header}</Text>
                                <Text styles={{ root: { fontSize: "small", fontWeight: 'bold' } }} >{subHeader} - {subSubHeader}</Text>
                            </Stack>
                            <StepOutcomeD3Table
                                stepOutcomeTable={handler.StepOutcomeTableData}
                                handler={handler}
                                onCellSelected={onCellSelected}
                            />
                        </Stack>
                    ) : (
                        <Text style={{ display: "block", paddingTop: 16, textAlign: "center" }}>
                            {!handler.isInitialRefresh && !handler.isInFullDataRefresh && !handler.isInIncrementalDataRefresh ? (
                                <>
                                    No results were found with the currently selected filters:
                                    <br />
                                    <pre style={{ textAlign: "left", display: "inline-block", marginTop: "8px" }}>
                                        {prettyPrintStepOutcomeFilters(filters)}
                                    </pre>
                                </>
                            ) : (
                                <>Please select filters and run a search to see results.</>
                            )}
                        </Text>
                    )}
                </Stack>
            }
        </Stack>
    );
});

/**
 * React Component that represents the overall Step Outcome Table view.
 * @param StepOutcomeViewProps The filter to use for the step outcome, and the refresh cadence of the data.
 * @returns React Component.
 */
export const StepOutcomeView: React.FC<StepOutcomeViewProps> = ({ filter, refreshTime }) => {
    useEffect(() => {
        handler.start();

        return () => {
            handler.stop();
        };

    }, []);

    useEffect(() => {
        handler.setFilter(filter);
    }, [filter]);

    useEffect(() => {
        if (refreshTime) {
            handler.setRefreshTime(refreshTime);
        }
    }, [refreshTime]);

    const [selectedItem, setSelectedItem] = useState<StepOutcomeTableEntry | null>(null);

    return (
        <Stack>
            <Stack key="step-outcome-table">
                {selectedItem && (<StepOutcomeFocusModal item={selectedItem} onDismiss={() => setSelectedItem(null)} />)}
                <StepOutcomeTableComponent handler={handler} filters={filter} onCellSelected={setSelectedItem} />
            </Stack>
        </Stack>
    );
};

// #endregion -- View Components --

// #region -- Script --

const DEFAULT_POLL_TIME = 120000;
const handler = new StepOutcomeDataHandler(DEFAULT_POLL_TIME);

// #endregion -- Script --