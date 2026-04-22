// Copyright Epic Games, Inc. All Rights Reserved.

import { mergeStyles, mergeStyleSets, Text, ComboBox, IComboBoxOption, SelectableOptionMenuItemType, IContextualMenuProps, IContextualMenuItem, IconButton, IContextualMenuListProps, Stack, Checkbox, FontIcon, Modal, DefaultButton, ScrollablePane, SelectionZone, DetailsList, Selection, DetailsListLayoutMode, SelectionMode, IColumn, PrimaryButton } from "@fluentui/react";
import { MetadataRef, PhaseSessionResult, TestDataHandler, TestNameRef, TestPhaseStatus, TestSessionDetails, TestSessionResult } from "./testData";
import dashboard, { StatusColor } from "horde/backend/Dashboard";
import { useCallback, useEffect, useMemo, useState } from "react";
import { TestOutcome, TestPhaseOutcome } from "./api";
import { PhaseHistoryGraph } from "./phaseHistoryGraph";
import { projectStore } from "horde/backend/ProjectStore";
import { getHordeStyling } from "horde/styles/Styles";

export type PhaseSessionCallback = (session: PhaseSessionResult) => void;

export const styles = {
    labelSmall: {
        fontSize: 12,
        fontWeight: "bold"
    },
    textSmall: {
        fontSize: 12
    },
    textLarge: {
        fontSize: 24
    },
    defaultButton: {
        backgroundColor: "#035ca1"
    },
    defaultFilter: {
        backgroundColor: "#0078d4"
    },
    highlightFilter: {
        backgroundColor: "#2a95e8ff"
    },
    defaultCheckbox: {
        root: {
            height: 15
        },
        text: {
            fontSize: 11,
            marginLeft: 0,
            lineHeight: 15
        },
        checkbox: {
            height: 15,
            width: 15
        }
    },
    icon: {
        cursor: "default",
        height: 15,
        paddingTop: 1
    },
    stripes: {
        backgroundImage: 'repeating-linear-gradient(-45deg, rgba(255, 255, 255, .2) 25%, transparent 25%, transparent 50%, rgba(255, 255, 255, .2) 50%, rgba(255, 255, 255, .2) 75%, transparent 75%, transparent)',
    },
}

let _styles: any;

const getStyles = () => {

   const border = `1px solid ${dashboard.darktheme ? "#2D2B29" : "#EDEBE9"}`

   const styles = _styles ?? mergeStyleSets({
      list: {
         selectors: {
            'a': {
               height: "unset !important",
            },
            '.ms-List-cell': {

               borderTop: border,
               borderRight: border,
               borderLeft: border
            },
            '.ms-List-cell:nth-last-child(-n + 1)': {
               borderBottom: border
            },
            ".ms-DetailsRow #artifactview": {
               opacity: 0
            },
            ".ms-DetailsRow:hover #artifactview": {
               opacity: 1
            },
         }
      }
   });

   _styles = styles;

   return styles;
}

let _colors: Map<StatusColor, string> | undefined = undefined;

export const getStatusColors = (): Map<StatusColor, string> => {

    const dashboardStatusColors = dashboard.getStatusColors();

    const colors = _colors ?? new Map<StatusColor, string>([
        [StatusColor.Success, dashboardStatusColors.get(StatusColor.Success)!],
        [StatusColor.Warnings, dashboardStatusColors.get(StatusColor.Warnings)!],
        [StatusColor.Failure, dashboardStatusColors.get(StatusColor.Failure)!],
        [StatusColor.Skipped,  dashboard.darktheme ? "#c5c4c3ff" : "#a1a1a1ff"],
        [StatusColor.Unspecified, "#515151ff"]
    ]);

    return colors;
}

export const MultiOptionChooser: React.FC<{options: IComboBoxOption[], initialSelection: string[], updateKeys: (selectedKeys: string[]) => void, placeholder?: string, style?: any, disabled?: boolean }> = ({ options, initialSelection, updateKeys, placeholder, style, disabled }) => {
    const [selection, setSelection] = useState<string[]>(initialSelection);
    const optionsWithSelectAll = [...options];

    useEffect(() => {
        // keep input selection in sync
        if (selection.toString() !== initialSelection.toString()) {
            setSelection(initialSelection);
        }
    }, [initialSelection]);

    if (selection.length === options.length) {
        selection.push('selectAll');
    }

    optionsWithSelectAll.unshift({ key: 'selectAll', text: 'Select All', itemType: SelectableOptionMenuItemType.SelectAll });

    return <ComboBox styles={{ root: style }} placeholder={placeholder ?? "None"} selectedKey={selection} multiSelect options={optionsWithSelectAll} disabled={disabled}
        onChange={(_, option) => {
            if (option) {
                if (option.itemType === SelectableOptionMenuItemType.SelectAll) {
                    setSelection(option.selected? optionsWithSelectAll.map((o) => o.key as string) : []);
                    return;
                }
                const key = option.key as string;
                const index = selection.indexOf(key);
                if (index === -1 && option.selected) {
                    selection.push(key);
                } else if (index >= 0 && !option.selected) {
                    selection.splice(index, 1);
                }
                setSelection([...selection]);
            }
        }}
        onMenuDismissed={() => {
            updateKeys(selection.filter((k) => k !== 'selectAll'));
        }} />
};

export type StatusBarStack = {
    value: number,
    title?: string,
    titleValue?: string,
    color?: string,
    onClick?: () => void,
    stripes?: boolean,
}

export const StatusBar = (stack: StatusBarStack[], width: number, height: number, basecolor?: string, style?: any): JSX.Element => {

    stack = stack.filter(s => s.value > 0);

    const mainTitle = stack.map((item) => {
        return !item.titleValue ? `${Math.ceil(item.value)}% ${item.title}` : `${item.titleValue} ${item.title}`
    }).join(' ');

    return (
        <div className={mergeStyles({ backgroundColor: basecolor, width: width, height: height, verticalAlign: 'middle', display: "flex" }, style)} title={mainTitle}>
            {stack.map((item) => <span key={item.title!}
                onClick={item.onClick}
                style={{
                    width: `${item.value}%`, height: '100%',
                    backgroundColor: item.color,
                    display: 'block',
                    cursor: item.onClick ? 'pointer' : 'inherit',
                    backgroundSize: `${height * 2}px ${height * 2}px`,
                    backgroundImage: item.stripes ? styles.stripes.backgroundImage : undefined
                }} />)}
        </div>
    );
}

export const SessionStatusBar = (session: TestSessionResult | TestSessionDetails, width: number, height: number): JSX.Element | undefined => {
    const sessionTotalCount = session.phasesSucceededCount + session.phasesFailedCount + session.phasesUnspecifiedCount;
    const metaFailedFactor = Math.ceil(session.phasesFailedCount / (sessionTotalCount || 1) * 50) / 50;
    const metaUnspecifiedFactor = Math.ceil(session.phasesUnspecifiedCount / (sessionTotalCount || 1) * 50) / 50;

    const bDisplayBar = (metaFailedFactor + metaUnspecifiedFactor) > 0 && metaFailedFactor < 1 && metaUnspecifiedFactor < 1;
    if (!bDisplayBar) return undefined

    const statusColors = getStatusColors();

    const stack: StatusBarStack[] = [
        {
            value: metaUnspecifiedFactor * 100,
            title: "Unspecified",
            titleValue: `${ Math.ceil(session.phasesUnspecifiedCount / (sessionTotalCount || 1) * 100)}%`,
            color: statusColors.get(StatusColor.Unspecified)!,
            stripes: true
        },
        {
            value: metaFailedFactor * 100,
            title: "Failure",
            titleValue: `${ Math.ceil(session.phasesFailedCount / (sessionTotalCount || 1) * 100)}%`,
            color: statusColors.get(StatusColor.Failure)!,
            stripes: true
        }
    ];

    return StatusBar(stack, width, height, statusColors.get(StatusColor.Success)!, { margin: '3px !important' });
}

export const StatusPie = (stack: StatusBarStack[], radius: number, style?: any): JSX.Element => {

    const width = radius * 2;
    const height = width;

    stack = stack.filter(s => s.value > 0);
    let offset = 0;
    const stackOffsets = stack.map(s => {
        const stepOffset = offset;
        offset += s.value;
        return stepOffset;
    });

    const mainTitle = stack.map((item) => {
        return item.titleValue === undefined ? `${Math.ceil(item.value)}% ${item.title}` : `${item.titleValue} ${item.title}`
    }).join(' ');

    return (
        <div className={mergeStyles({ width: width, height: height, verticalAlign: 'middle', display: "flex" }, style)} title={mainTitle}>
            <svg width={width} height={height} viewBox={`0 0 64 64`}>
                {stack.map((item, index) => <circle key={item.title!} r="25%" cx="50%" cy="50%" strokeWidth="50%" transform={`rotate(-90) translate(-64)`} fill="transparent"
                                        stroke={item.color}
                                        strokeDashoffset={`-${stackOffsets[index]}`}
                                        strokeDasharray={`${item.value} 100`}
                                        onClick={item.onClick} />)
                }
            </svg>
        </div>
    );
}

export const SessionStatusPie = (session: TestSessionResult, radius: number): JSX.Element | undefined => {
    const sessionTotalCount = session.phasesSucceededCount + session.phasesFailedCount + session.phasesUnspecifiedCount;
    const metaFailedFactor = Math.ceil(session.phasesFailedCount / (sessionTotalCount || 1) * 100) / 100;
    const metaUnspecifiedFactor = Math.ceil(session.phasesUnspecifiedCount / (sessionTotalCount || 1) * 100) / 100;

    const bDisplayBar = (metaFailedFactor + metaUnspecifiedFactor) > 0 && (metaFailedFactor + metaUnspecifiedFactor) < 1;
    if (!bDisplayBar) return;

    const statusColors = getStatusColors();

    const stack: StatusBarStack[] = [
        {
            value: metaUnspecifiedFactor * 100,
            title: "Unspecified",
            color: statusColors.get(StatusColor.Unspecified)!
        },
        {
            value: metaFailedFactor * 100,
            title: "Failure",
            color: statusColors.get(StatusColor.Failure)!
        },
        {
            value: (1 - (metaFailedFactor + metaUnspecifiedFactor)) * 100,
            title: "Passed",
            color: statusColors.get(StatusColor.Success)!
        }
    ];

    return StatusPie(stack, radius);
}

export const PhasesStatusPie = (phases: TestPhaseStatus[], radius: number): JSX.Element | undefined => {
    const totalCount = phases.length || 1;
    let phasesFailedCount = 0;
    let phasesUnspecifiedCount = 0;
    let phasesSkippedCount = 0
    phases.forEach(p => {
        switch (p.outcome) {
            case TestPhaseOutcome.Skipped:
                ++phasesSkippedCount;
                break;
            case TestPhaseOutcome.Failed:
                ++phasesFailedCount;
                break;
            case TestPhaseOutcome.Interrupted:
            case TestPhaseOutcome.Unknown:
            case TestPhaseOutcome.NotRun:
                ++phasesUnspecifiedCount;
                break;
        }
    });

    const failedFactor = Math.ceil(phasesFailedCount / totalCount * 100) / 100;
    const unspecifiedFactor = Math.ceil(phasesUnspecifiedCount / totalCount * 100) / 100;
    const skippedFactor = Math.ceil(phasesSkippedCount / totalCount * 100) / 100;

    const statusColors = getStatusColors();

    const stack: StatusBarStack[] = [
        {
            value: unspecifiedFactor * 100,
            title: "Unspecified",
            color: statusColors.get(StatusColor.Unspecified)!
        },
        {
            value: skippedFactor * 100,
            title: "Skipped",
            color: statusColors.get(StatusColor.Skipped)!
        },
        {
            value: failedFactor * 100,
            title: "Failure",
            color: statusColors.get(StatusColor.Failure)!
        },
        {
            value: (1 - (failedFactor + unspecifiedFactor + skippedFactor)) * 100,
            title: "Passed",
            color: statusColors.get(StatusColor.Success)!
        }
    ];

    return StatusPie(stack, radius);
}

export const SessionValues = (session: TestSessionResult, style?: any): JSX.Element => {
    const statusColors = getStatusColors();
    return <Text style={style}>
                {!!session.phasesUnspecifiedCount && <span style={{color: statusColors.get(StatusColor.Unspecified)}}> Unspecified: {session.phasesUnspecifiedCount}</span>}
                {!!session.phasesFailedCount && <span style={{color: statusColors.get(StatusColor.Failure)}}> Failed: {session.phasesFailedCount}</span>}
                {!!session.phasesSucceededCount && <span style={{color: statusColors.get(StatusColor.Success)}}> Success: {session.phasesSucceededCount}</span>}
            </Text>
}

export const getPhaseSessionStatusColor = (phase: PhaseSessionResult | TestPhaseStatus) => {
    const statusColors = getStatusColors();
    let color = statusColors.get(StatusColor.Unspecified)!
    switch(phase.outcome) {
        case TestPhaseOutcome.Skipped:
            color = statusColors.get(StatusColor.Skipped)!
            break;
        case TestPhaseOutcome.Failed:
            color = statusColors.get(StatusColor.Failure)!
            break;
        case TestPhaseOutcome.Success:
            color = statusColors.get(phase.hasWarning? StatusColor.Warnings : StatusColor.Success)!
            break;
    }

    return color;
}

export const getTestSessionStatusColor = (test: TestSessionResult | TestSessionDetails) => {
    const statusColors = getStatusColors();
    let color = statusColors.get(StatusColor.Success);
    if (test.outcome === TestOutcome.Failure) {
        color = statusColors.get(StatusColor.Failure);
    } else if (test.outcome === TestOutcome.Unspecified) {
        color = statusColors.get(StatusColor.Unspecified);
    }

    return color;
}

export const PhaseHistoryGraphWidget: React.FC<{test: TestNameRef, phaseKey: string, meta: MetadataRef, streamId: string, sessionId?: string, handler: TestDataHandler, onClick: PhaseSessionCallback, withoutHeader?: boolean}> = ({ test, phaseKey, meta, streamId, sessionId, handler, onClick, withoutHeader }) => {
    const [container, setContainer] = useState<HTMLDivElement | null>(null);
    const [state, setState] = useState<{ graph?: PhaseHistoryGraph }>({});

    useEffect(() => {
        const graph = new PhaseHistoryGraph(test, phaseKey, streamId, handler, onClick);
        setState({ graph: graph });
    }, [test, phaseKey, streamId, onClick]);

    useEffect(() => {
        // refresh on meta or sessionId update
        try {
            container && state.graph?.render(container, meta, sessionId, withoutHeader);
        } catch (err) {
            console.error(err);
        }
    }, [container, state, meta, sessionId])

    if (!state.graph) {
        return null;
    }

    const graph_container_id = `test_history_graph_container_${test!.id}_${meta.id}_${phaseKey}_${streamId}`;

    return <div id={graph_container_id} style={{ userSelect: "none", position: "relative" }} ref={(ref: HTMLDivElement) => setContainer(ref)}/>
}

export const getStreamOptions = (streams: string[], onClick: (stream: string) => void, selected?: string): IContextualMenuProps => {
    const projectItems: Map<string, Map<string, IContextualMenuItem[]>> = new Map();

    projectStore.projects.sort(
        (a, b) => a.order - b.order
    ).forEach(p => {
        p.categories?.forEach(category => {
            category.streams.forEach(a => {
                if (!category.showOnNavMenu || !streams.includes(a)) return;

                const projectName = p.name ?? 'Unknown';
                if (!projectItems.has(projectName)) {
                    projectItems.set(projectName, new Map());
                }

                const categoryItems = projectItems.get(projectName)!;
                if (!categoryItems.has(category.name)) {
                    categoryItems.set(category.name, []);
                }

                categoryItems.get(category.name)?.push({
                    key: `streams_${projectStore.streamById(a)?.fullname}`,
                    text: projectStore.streamById(a)?.name ?? a,
                    onClick: () => onClick(a),
                    disabled: a === selected
                });
            });
        });
    });

    const options: IContextualMenuItem[] = projectItems.entries().map(
        ([name, categories]) => {
            // if more than one catergory and more than 8 streams overall, do a full break down of the catergories
            const hasCategoriesAndManyItems = categories.size > 1 && categories.values().map(c => c.length).reduce((acc, l) => acc + l, 0) > 8;
            return {
                key: `project_${name}`,
                text: name,
                subMenuProps: {
                    shouldFocusOnMount: true,
                    subMenuHoverDelay: 0,
                    items: (
                        hasCategoriesAndManyItems ?
                            categories.entries().map(
                                ([cat, items]) => {
                                    return {
                                        key: `category_${cat}`,
                                        text: cat,
                                        subMenuProps: {
                                            shouldFocusOnMount: true,
                                            subMenuHoverDelay: 0,
                                            items: items
                                        }
                                    }
                            }).toArray()
                            : categories.values().toArray().flat())
                }
            }
    }).toArray();

    return {
            shouldFocusOnMount: true,
            subMenuHoverDelay: 0,
            items: options,
        };
}

export type ICheckListOption = IContextualMenuItem & {
    checked: boolean,
    color?: string,
}

export const getPhaseOutcomeOptions = (): ICheckListOption[] => {
    const statusColors = getStatusColors();
    return [
        {
            key: TestOutcome.Unspecified,
            text: 'Unspecified',
            checked: true,
            color: statusColors.get(StatusColor.Unspecified)
        },
        {
            key: TestOutcome.Failure,
            text: 'Failed',
            checked: true,
            color: statusColors.get(StatusColor.Failure)
        },
        {
            key: TestOutcome.Success,
            text: 'Passed',
            checked: true,
            color: statusColors.get(StatusColor.Success)
        },
        {
            key: TestOutcome.Skipped,
            text: 'Skipped',
            checked: true,
            color: statusColors.get(StatusColor.Skipped)
        },
    ]
}

class PhaseFiltersContext {
    constructor() {
        this._outcomeOptions = getPhaseOutcomeOptions();
        this._names = new Map();
    }

    get outcomeOptions() {
        return this._outcomeOptions
    }

    filterTestPhases(testId: string, phases: TestPhaseStatus[]) {
        const outcomes = this.getFilteredOutcomes();
        const names = this.getFilteredNames(testId);
        return phases.filter(phase => phase.isMatch(names, outcomes));
    }

    getFilteredOutcomes() {
        const outcome: Set<TestPhaseOutcome> = new Set();
        this._outcomeOptions.filter(option => option.checked).forEach(option => {
            switch(option.key) {
                case TestOutcome.Unspecified:
                    outcome.add(TestPhaseOutcome.NotRun);
                    outcome.add(TestPhaseOutcome.Unknown);
                    outcome.add(TestPhaseOutcome.Interrupted);
                    break;
                case TestOutcome.Skipped:
                    outcome.add(TestPhaseOutcome.Skipped);
                    break;
                case TestOutcome.Failure:
                    outcome.add(TestPhaseOutcome.Failed);
                    outcome.add(TestPhaseOutcome.Interrupted);
                    break;
                case TestOutcome.Success:
                    outcome.add(TestPhaseOutcome.Success);
                    break;
            }
        });
        return outcome;
    }

    getFilteredNames(testId: string) {
        return this._names.get(testId) ?? [];
    }

    setFilteredNames(testId: string, names: string[]) {
        this._names.set(testId, names.map(n => n.toLowerCase()));
    }

    private _outcomeOptions: ICheckListOption[];
    private _names: Map<string, string[]>;
}
export const phaseFiltersContext = new PhaseFiltersContext();

export const CheckListOption: React.FC<{options: ICheckListOption[], onChange: (items: ICheckListOption[]) => void}> = ({options, onChange}) => {
    const [items, setItems] = useState<ICheckListOption[]>([]);
    const [update, setUpdate] = useState<number>(0);

    useEffect(() => {
        const initItems: ICheckListOption[] = [{
            key: 'SelectAll',
            text: 'Select All',
            checked: !options.some(i => !i.checked)
        }];
        
        initItems.push(...options);

        setItems(initItems);
        setUpdate(1);
    }, [options]);

    const isFiltering : boolean = useMemo(() => items.slice(1).some(i => !i.checked), [items, update]);

    if (!items.length && !update) return;

    const fitlerOptions: IContextualMenuProps = {
        items: items,
        onRenderMenuList: (props: IContextualMenuListProps) =>
                <Stack tokens={{childrenGap: 6}} style={{margin: '3px 2px'}}>
                    {props.items.map(item => <Checkbox
                                                key={item.key}
                                                label={item.text}
                                                checked={item.checked}
                                                indeterminate={item.key === 'SelectAll' && !item.checked && props.items.slice(1).some(i => i.checked)}
                                                onChange={
                                                    (ev, checked) => {
                                                        item.checked = checked;
                                                        if (item.key === 'SelectAll') {
                                                            props.items.forEach(i => i.checked = checked);
                                                        } else {
                                                            props.items[0].checked = !props.items.slice(1).some(i => !i.checked);
                                                        }
                                                        setUpdate(update + 1);
                                                        onChange(items.slice(1));
                                                    }
                                                }
                                                onRenderLabel={(props) => {
                                                    return <Stack horizontal verticalAlign="center">
                                                                <Stack style={{ paddingRight: 5 }}>
                                                                    <FontIcon style={{ fontSize: 12, color: item.color }} iconName={!!item.color? "Square" : "MultiSelect"} />
                                                                </Stack>
                                                                <Text>{props?.label}</Text>
                                                            </Stack>
                                                }}
                                            />)}
                </Stack>
    }

    const iconName : string = isFiltering? "ReportWarning" : "PageListFilter";
    const iconColor : string = isFiltering? styles.highlightFilter.backgroundColor : "";

    return  <IconButton
                iconProps={{iconName: iconName}}
                menuProps={fitlerOptions}
                style={{
                    fontSize:16, height: 28,
                    border: '1px solid', borderColor: dashboard.darktheme ? "#4D4C4B" : "#6D6C6B", borderRadius: 4,
                    backgroundColor: dashboard.darktheme ? "rgba(255, 255, 255, 0.1)" : "",
                    color: dashboard.darktheme ? 'white' : 'grey'
                }}
                styles={{icon: {color: iconColor}}}
            />
}

type ArtifactHref = {
    name: string;
    reference: string;
    href: string;
}

export const PhaseArtifactsModal: React.FC<{ phase: TestPhaseStatus, artifactPaths: string[], onClose?: () => void }> = ({ phase, artifactPaths, onClose }) => {
    const [selectVer, setSelectVer] = useState(0);
    const [items, setItems] = useState<ArtifactHref[]>([]);

    useEffect(() => {
        if (artifactPaths.length === 0) return;
        const hrefs: (Promise<string> | undefined)[] = [];
        for (const artifact of artifactPaths) {
            hrefs.push(phase.artifacts?.getLink(artifact));
        }
        // get common prefix end position
        let pos = 0;
        const sortedPaths = artifactPaths.concat().sort();
        const shortest = sortedPaths[0];
        const longest = sortedPaths[sortedPaths.length - 1];
        if (shortest === longest) {
            pos = shortest.lastIndexOf('/') + 1;
        } else {
            while (pos < shortest.length && shortest.charAt(pos) === longest.charAt(pos) ) pos++;
            pos = shortest.lastIndexOf('/', pos) + 1;
        }
        
        Promise.all(hrefs).then(results => {
            const items: ArtifactHref[] = [];
            artifactPaths.forEach((artifact, index) => {
                const result = results[index];
                if (!result) return;
                items.push({name: artifact.substring(pos), reference: artifact, href: result});
            });
            setItems(items);
        });
    }, [phase, artifactPaths]);

    const { hordeClasses } = getHordeStyling();
    const styles = getStyles();

    const selector = useMemo(() => new Selection({onSelectionChanged: () => setSelectVer(Math.random)}), [phase, artifactPaths]);

    const downloadText = useMemo(() => {
        let text = "Download";
        const count = selector.getSelectedCount() || artifactPaths.length;
        if (count === 1) {
            text += ` (1 file)`;
        } else if (count > 1) {
            text += ` (${count} files)`;
        }
        return text;
    }, [selectVer]);

    const downloadZip = useCallback(() => {
        if (artifactPaths.length === 0) return;

        let items = selector.getSelection() as ArtifactHref[];
        if (items.length === 0) items = selector.getItems() as ArtifactHref[];

        // download a single file
        if (items.length === 1) {
            phase.artifacts?.download(items[0].reference);
            return;
        }

        phase.artifacts?.downloadZip(items.map(item => item.reference));

    }, [phase, artifactPaths]);

    const columns: IColumn[] = useMemo(() => [
        { key: 'column1', name: 'Name', minWidth: 794 - 32, maxWidth: 794 - 32, isResizable: false, isPadded: false },
        { key: 'column2', name: 'View_Download', minWidth: 64 + 32, maxWidth: 64 + 32, isResizable: false, isPadded: false }
    ], []);

    const renderItem = useCallback((item: ArtifactHref, index?: number, column?: IColumn) => {
        if (!column)  return null;

        if (column.name === "View_Download") {

            const href = item.href;

            return <Stack data-selection-disabled verticalAlign="center" verticalFill horizontal horizontalAlign="end" style={{ paddingTop: 0, paddingBottom: 0 }}>
                        <IconButton id="artifactview" href={`${href}&inline=true`} target="_blank" style={{ paddingTop: 1, color: "#106EBE" }} iconProps={{ iconName: "Eye", styles: { root: { fontSize: "14px" } } }} />
                        <IconButton id="artifactview" href={href} target="_blank" style={{ paddingTop: 1, color: "#106EBE" }} iconProps={{ iconName: "CloudDownload", styles: { root: { fontSize: "14px" } } }} />
                    </Stack>
        }

        if (column.name === "Name") {

            const path = (item.name as string).split("/");
            const start_index = path.length > 5? path.length - 5 : 0;

            const pathElements = path.slice(start_index).map((t, index) => {
                const last = (index + start_index) === (path.length - 1);
                let color = last ? (dashboard.darktheme ? "#FFFFFF" : "#605E5C") : undefined;
                const font = last ? undefined : "Horde Open Sans Light";
                const sep = last ? undefined : "/"
                return <Text key={`p-${index}`} styles={{ root: { fontFamily: font } }} style={{ color: color }}>{t}{sep}</Text>
            });
            if (start_index !== 0) pathElements.splice(0, 0, <Text key={`p-${item.name}`} >... /</Text>);

            return <Stack verticalFill verticalAlign="center" style={{ cursor: "pointer" }}>
                        <Stack horizontal tokens={{ childrenGap: 8 }} title={item.name}>
                            <Stack>
                                <FontIcon style={{ paddingTop: 1, fontSize: 16 }} iconName="Document" />
                            </Stack>
                            <Stack horizontal>
                                {pathElements}
                            </Stack>
                        </Stack>
                    </Stack>
        }

        return null;
    }, []);

    return <Stack>
            <Modal isOpen={true} topOffsetFixed={true} styles={{ main: { padding: 8, width: 1180, height: 820, hasBeenOpened: false, top: "80px", position: "absolute" } }} onDismiss={onClose} className={hordeClasses.modal}>
                <Stack className="horde-no-darktheme" styles={{ root: { paddingTop: 10, paddingRight: 12 } }}>
                    <Stack style={{ paddingLeft: 24, paddingRight: 24 }}>
                        <Stack tokens={{ childrenGap: 12 }} style={{ height: 800 }}>
                            <Stack horizontal verticalAlign="start">
                                <Stack style={{ paddingTop: 3 }}>
                                    <Text variant="mediumPlus" styles={{ root: { fontFamily: "Horde Open Sans SemiBold" } }}>Artifacts</Text>
                                </Stack>
                                <Stack grow />
                                <Stack horizontalAlign="end">
                                    <IconButton
                                        iconProps={{ iconName: 'Cancel' }}
                                        onClick={onClose}
                                    />
                                </Stack>
                            </Stack>
                            <Stack styles={{ root: { paddingLeft: 4, paddingRight: 0, paddingTop: 8, paddingBottom: 4 } }}>
                                <Stack tokens={{ childrenGap: 12 }}>
                                    <Stack horizontal verticalAlign="center" tokens={{ childrenGap: 18 }} style={{ paddingBottom: 12 }}>
                                        <Text style={{fontSize: 13, fontWeight: 600, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', cursor: "text"}}>{phase.name}</Text>
                                        <Stack grow />
                                        <PrimaryButton text={downloadText} onClick={downloadZip}/>
                                    </Stack >
                                    <Stack style={{ height: 492 + 160, position: "relative" }}>
                                        <ScrollablePane style={{ height: 492 + 160 }}>
                                            <SelectionZone selection={selector}>
                                                <DetailsList
                                                    styles={{ root: { overflowX: "hidden" } }}
                                                    className={styles.list}
                                                    isHeaderVisible={false}
                                                    compact={true}
                                                    items={items}
                                                    columns={columns}
                                                    layoutMode={DetailsListLayoutMode.fixedColumns}
                                                    selectionMode={SelectionMode.multiple}
                                                    enableUpdateAnimations={false}
                                                    selection={selector}
                                                    selectionPreservedOnEmptyClick={true}
                                                    onShouldVirtualize={() => false}
                                                    onItemInvoked={(item: ArtifactHref) => {
                                                        window.open(`${item.href}&inline=true`, "_blank");
                                                    }}
                                                    onRenderItemColumn={renderItem}
                                                />
                                            </SelectionZone>
                                        </ScrollablePane>
                                    </Stack>
                                </Stack >
                            </Stack>
                        </Stack>
                    </Stack>
                </Stack>
            </Modal>
        </Stack>
}
