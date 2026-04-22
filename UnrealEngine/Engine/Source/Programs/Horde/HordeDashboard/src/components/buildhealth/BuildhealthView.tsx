// Copyright Epic Games, Inc. All Rights Reserved.

import { Checkbox, ComboBox, DirectionalHint, Dropdown, DropdownMenuItemType, IDropdownOption, IDropdownStyles, Label, Slider, Stack, TooltipHost } from "@fluentui/react";
import { GetProjectResponse, GetStreamResponse } from "horde/backend/Api";
import { observer } from "mobx-react-lite";
import { useCallback, useEffect, useMemo, useState } from "react";
import { useLocation, useNavigate } from "react-router";
import { useWindowSize } from "../../base/utilities/hooks";
import { getHordeStyling } from "../../styles/Styles";
import { Breadcrumbs } from "../Breadcrumbs";
import { TopNav } from "../TopNav";
import { StepOutcomeView } from "../buildhealth/stepoutcome/StepOutcome";
import { BuildHealthDataHandler, DataHandlerRefreshRequest, expandRefreshRequest } from "./BuildHealthDataHandler";
import { KEY_SEPARATOR, PARAMETER_KEY_PREFIX, StepRefData, TemplateRefData } from "./BuildHealthDataTypes";
import { BuildHealthOptionsController, BuildHealthOptionsState, JobHistoryTimeSpans, loadBuildHealthOptionsFromParams, parseBuildHealthQueryParams, TimeSpan } from "./BuildHealthOptions";
import { encodeStepKey, encodeTemplateKey } from "./BuildHealthUtilities";
import { stepOutcomeTableClasses } from "./stepoutcome/StepOutcomeSharedUIComponents";

// #region -- Dropdown Components --

// #region -- Dropdown Data Types --

type BuildHealthOptionData = {
    id: string,
    text: string,
    group?: string
}

type BuildHealthOptionList = {
    items: BuildHealthOptionData[]
    label: string,
    toolTip: string
}

// #endregion -- Dropdown Data Types --

// #region -- Base Dropdown Components --

/**
 * React Component that is a single select drop down.
 * @returns React Component.
 */
const BasicListParameter: React.FC<{ handler: BuildHealthDataHandler, buildHealthOptions: BuildHealthOptionsController, param: BuildHealthOptionList, disabled?: boolean, selectedKey?: string, request: DataHandlerRefreshRequest, onChange: (option: IDropdownOption<BuildHealthOptionData>) => void }> = function ConstructBasicListParamater({ param, buildHealthOptions, selectedKey, request, onChange }) {
    const key = `${PARAMETER_KEY_PREFIX}${KEY_SEPARATOR}${param.label}`;
    const changeVersion = buildHealthOptions.optionsChangeVersion;
    const doptions: IDropdownOption<BuildHealthOptionData>[] = [];

    param.items.forEach((item) => {
        doptions.push({
            key: item.id,
            text: item.text,
        });
    });

    return (<Dropdown id={changeVersion.toString() + key}
        key={key}
        label={param.label}
        placeholder={"Select option"}
        options={doptions}
        selectedKey={selectedKey}
        dropdownWidth={200}
        styles={{ dropdown: { width: 300 } }}
        calloutProps={{
            directionalHint: DirectionalHint.rightCenter
        }}
        onChange={(_ev, option) => {
            let castedOption = option as IDropdownOption<BuildHealthOptionData>;
            if (!castedOption) {
                return;
            }
            onChange(castedOption as IDropdownOption<BuildHealthOptionData>);
        }}
        onDismiss={() => {
            handler.requestHierarchicalRefresh(request);
            buildHealthOptions.synchronizeDerivedKeys();
        }}
    />);
};

/**
 * React Component that is a multi select drop down.
 * @returns React Component.
 */
const MultiListParameter: React.FC<{ handler: BuildHealthDataHandler, buildHealthOptions: BuildHealthOptionsController, param: BuildHealthOptionList, disabled?: boolean, selectedKeys: string[], request: DataHandlerRefreshRequest, onChange: (option: IDropdownOption<BuildHealthOptionData>) => void }> = function ConstructMultilistParamter({ param, buildHealthOptions, selectedKeys, request, onChange }) {
    const changeVersion = buildHealthOptions.optionsChangeVersion;
    const { modeColors } = getHordeStyling();
    const key = `${PARAMETER_KEY_PREFIX}${KEY_SEPARATOR}${param.label}`;
    const gset: Set<string> = new Set();

    param.items.forEach(item => {
        if (!item.group) {
            item.group = "__nogroup";
        }
        if (item.group) {
            gset.add(item.group);
        }
    });

    // #region -- Data Memos --

    const doptions = useMemo(() => {
        const gset: Set<string> = new Set();

        param.items.forEach(item => gset.add(item.group ?? "__nogroup"));

        const groups = Array.from(gset).sort((a, b) => a.localeCompare(b));

        const options: IDropdownOption<BuildHealthOptionData>[] = [];

        groups.forEach(group => {
            if (group !== "__nogroup") {
                options.push({
                    key: `group_${group}`,
                    text: group,
                    itemType: DropdownMenuItemType.Header,
                });
            }
            param.items.forEach(item => {
                if ((item.group ?? "__nogroup") === group) {
                    options.push({
                        key: item.id,
                        text: item.text,
                        data: item,
                    });
                }
            });
        });

        return options;
    }, [param.items]);

    const memoizedSelectedKeys = useMemo(() => selectedKeys, [selectedKeys]);

    // #endregion -- Data Memos --

    // #region -- Style Memos --

    const dropdownStyles: Partial<IDropdownStyles> = useMemo(() => ({
        dropdown: { width: 300 },
        callout: {
            selectors: {
                [".ms-Callout-main"]: {
                    padding: "4px 4px 12px 12px",
                    overflow: "auto" as "auto",
                },
            },
        },
        dropdownItemHeader: { fontSize: 12, color: modeColors.text },
        dropdownOptionText: { fontSize: 12 },
        dropdownItem: {
            minHeight: 18,
            lineHeight: 18,
            selectors: {
                [".ms-Checkbox-checkbox"]: {
                    width: 14,
                    height: 14,
                    fontSize: 11,
                },
            },
        },
        dropdownItemSelected: {
            minHeight: 18,
            lineHeight: 18,
            backgroundColor: "inherit",
            selectors: {
                [".ms-Checkbox-checkbox"]: {
                    width: 14,
                    height: 14,
                    fontSize: 11,
                },
            },
        },
    }), [modeColors.text]);

    // #endregion -- Style Memos --

    // #region -- Dropdown Callbacks --

    const renderItem = useCallback((item, defaultRender) => {
        let castedOption = item as IDropdownOption<BuildHealthOptionData>;
        if (!castedOption || !defaultRender) return null;

        if (!castedOption.data) {
            return defaultRender(item);
        }
        return (
            <TooltipHost key={`tooltip_${castedOption.text}`} content={
                <div>
                    <Stack>
                        <Label>{castedOption.data?.group}</Label>
                    </Stack>
                    <Stack>
                        {castedOption.text}
                    </Stack>
                </div>
            }
                directionalHint={DirectionalHint.rightCenter}
                calloutProps={{
                    styles: {
                        root: { maxWidth: 600 }
                    }
                }}
            >
                {defaultRender(item)}
            </TooltipHost>
        );
    }, []);

    const handleChange = useCallback((_ev, option) => {
        const castedOption = option as IDropdownOption<BuildHealthOptionData>;
        if (!castedOption) return;
        onChange(castedOption);
    }, [onChange]);

    const handleDismiss = useCallback(() => {
        handler.requestHierarchicalRefresh(request);
        buildHealthOptions.synchronizeDerivedKeys();
    }, [handler, request, buildHealthOptions]);

    // #endregion -- Dropdown Callbacks --

    return (
        <Dropdown
            id={changeVersion.toString() + key}
            key={key}
            label={param.label}
            placeholder="Select options"
            options={doptions}
            selectedKeys={memoizedSelectedKeys}
            dropdownWidth={400}
            calloutProps={{ directionalHint: DirectionalHint.rightCenter }}
            styles={dropdownStyles}
            onRenderItem={renderItem}
            onChange={handleChange}
            onDismiss={handleDismiss}
            multiSelect
        />);
};

// #endregion -- Base Dropdown Components --

// #region -- Option Specific Dropdown Components --

const ProjectDropdownSingle: React.FC<{ handler: BuildHealthDataHandler, buildHealthOptions: BuildHealthOptionsController }> = observer(function ConstructProjectDropdownSingle({ handler, buildHealthOptions }) {
    const projectDropdownParams: BuildHealthOptionList = {
        label: "Select Project",
        items: [
        ],
        toolTip: "Select an option from the dropdown."
    };
    projectDropdownParams.items = handler.projectsData.map((project: GetProjectResponse) => {
        let listParam: BuildHealthOptionData = {
            id: `${project.id}`,
            text: project.name,
        };

        return listParam;
    }).sort((a, b) => a.text.localeCompare(b.text));

    let selectedProjectKey = Object.keys(buildHealthOptions.state.enabledProjects)[0] ?? undefined;

    // Note: This may look odd, but there is really no reason to force a full hierarchy of project refresh; we just want to pulse streams
    let request: DataHandlerRefreshRequest = expandRefreshRequest({ streams: true });

    return (
        <Stack>
            <BasicListParameter handler={handler} buildHealthOptions={buildHealthOptions} param={projectDropdownParams} selectedKey={selectedProjectKey} request={request} onChange={(option: IDropdownOption<BuildHealthOptionData>) => {
                const key = option.key as string;
                buildHealthOptions.toggleSingleProject(key, option.text);
            }} />
        </Stack>
    );
});

const ProjectDropdown: React.FC<{ handler: BuildHealthDataHandler, buildHealthOptions: BuildHealthOptionsController }> = observer(function ConstructProjectDropdown({ handler, buildHealthOptions }) {
    const projectDropdownParams: BuildHealthOptionList = {
        label: "Select Project",
        items: [
        ],
        toolTip: "Select an option from the dropdown."
    };

    projectDropdownParams.items = handler.projectsData.map((project: GetProjectResponse) => {
        let listParam: BuildHealthOptionData = {
            id: `${PARAMETER_KEY_PREFIX}${KEY_SEPARATOR}${projectDropdownParams.label}${KEY_SEPARATOR}${project.id}`,
            text: project.name,
        };

        return listParam;
    }).sort((a, b) => a.text.localeCompare(b.text));

    let selectedProjectKeys: string[] = Object.keys(buildHealthOptions.state.enabledProjects);
    let request: DataHandlerRefreshRequest = expandRefreshRequest({ streams: true });

    return (
        <Stack>
            <MultiListParameter handler={handler} buildHealthOptions={buildHealthOptions} param={projectDropdownParams} selectedKeys={selectedProjectKeys} request={request} onChange={(option: IDropdownOption<BuildHealthOptionData>) => {
                const key = option.key as string;
                const isSelected = !!option.selected;
                buildHealthOptions.toggleProject(key, option.text, isSelected);
            }} />
        </Stack>
    );
});

const StreamDropdownSingle: React.FC<{ handler: BuildHealthDataHandler, buildHealthOptions: BuildHealthOptionsController }> = observer(function ConstructProjectDropdownSingle({ handler, buildHealthOptions }) {
    const streamDropdownParams: BuildHealthOptionList = {
        label: "Select Stream",
        items: [
        ],
        toolTip: "Select an option from the dropdown."
    };

    streamDropdownParams.items = handler.streamsData.map((stream: GetStreamResponse) => {
        let listParam: BuildHealthOptionData = {
            id: stream.id,
            text: stream.name,
            group: stream.projectId,
        };
        return listParam;
    }).sort((a, b) => a.text.localeCompare(b.text));

    let selectedStreamKey: string = Object.keys(buildHealthOptions.state.enabledStreams)[0]!;
    let request: DataHandlerRefreshRequest = expandRefreshRequest({ streams: true });
    {
        return streamDropdownParams.items.length > 0 && (
            <Stack >
                <BasicListParameter handler={handler} buildHealthOptions={buildHealthOptions} param={streamDropdownParams} selectedKey={selectedStreamKey} request={request} onChange={(option: IDropdownOption<BuildHealthOptionData>) => {
                    const key = option.key as string;
                    buildHealthOptions.toggleSingleStream(key, option.text);
                }} />
            </Stack>
        );
    }
});

const StreamDropdown: React.FC<{ handler: BuildHealthDataHandler, buildHealthOptions: BuildHealthOptionsController }> = observer(function ConstructProjectDropdown({ handler, buildHealthOptions }) {
    const streamDropdownParams: BuildHealthOptionList = {
        label: "Select Stream(s)",
        items: [
        ],
        toolTip: "Select an option from the dropdown."
    };

    streamDropdownParams.items = handler.streamsData.map((stream: GetStreamResponse) => {
        let listParam: BuildHealthOptionData = {
            id: stream.id,
            text: stream.name,
            group: stream.projectId,
        };
        return listParam;
    }).sort((a, b) => a.text.localeCompare(b.text));

    let selectedStreamKeys: string[] = Object.keys(buildHealthOptions.state.enabledStreams);
    let request: DataHandlerRefreshRequest = expandRefreshRequest({ streams: true });
    {
        return streamDropdownParams.items.length > 0 && (
            <Stack>
                <MultiListParameter handler={handler} buildHealthOptions={buildHealthOptions} param={streamDropdownParams} selectedKeys={selectedStreamKeys} request={request} onChange={(option: IDropdownOption<BuildHealthOptionData>) => {
                    const key = option.key as string;
                    const isSelected = !!option.selected;

                    buildHealthOptions.toggleStream(key, option.text, isSelected);
                }} />
            </Stack>
        );
    }
});

const TemplateDropdown: React.FC<{ handler: BuildHealthDataHandler, buildHealthOptions: BuildHealthOptionsController }> = observer(function ConstructProjectDropdown({ handler, buildHealthOptions }) {
    const templateDropdownParams: BuildHealthOptionList = {
        label: "Select Template(s)",
        items: [
        ],
        toolTip: "Select an option from the dropdown."
    };

    templateDropdownParams.items = handler.templatesData.map((template: TemplateRefData) => {
        let listParam: BuildHealthOptionData = {
            id: encodeTemplateKey(template),
            text: template.name,
            group: template.streamId,
        };
        return listParam;
    }).sort((a, b) => a.text.localeCompare(b.text));

    let selectedTemplateKeys: string[] = Object.keys(buildHealthOptions.state.enabledTemplates);
    let request: DataHandlerRefreshRequest = expandRefreshRequest({ jobs: true });
    {
        return templateDropdownParams.items.length > 0 && (
            <Stack id={`job_dropdown`}>
                <MultiListParameter handler={handler} buildHealthOptions={buildHealthOptions} param={templateDropdownParams} selectedKeys={selectedTemplateKeys} request={request} onChange={(option: IDropdownOption<BuildHealthOptionData>) => {
                    const key = option.key as string;
                    const isSelected = !!option.selected;

                    buildHealthOptions.toggleTemplate(key, option.text, isSelected);
                }} />
            </Stack>
        );
    }
});

const StepDropdown: React.FC<{ handler: BuildHealthDataHandler, buildHealthOptions: BuildHealthOptionsController }> = observer(function ConstructProjectDropdown({ handler, buildHealthOptions }) {
    const stepDropdownParams: BuildHealthOptionList = {
        label: "Select Step(s)",
        items: [
        ],
        toolTip: "Select an option from the dropdown."
    };

    stepDropdownParams.items = handler.stepData.map((step: StepRefData) => {
        let listParam: BuildHealthOptionData = {
            id: encodeStepKey(step),
            text: step.name,
            group: `${step.streamId} - ${step.templateId}`
        };
        return listParam;
    }).sort((a, b) => a.text.localeCompare(b.text));

    let selectedStepKeys: string[] = Object.keys(buildHealthOptions.state.enabledSteps);
    let request: DataHandlerRefreshRequest = { steps: true };
    {
        return stepDropdownParams.items.length > 0 && (
            <Stack id={`step_dropdown`}>
                <MultiListParameter handler={handler} buildHealthOptions={buildHealthOptions} param={stepDropdownParams} selectedKeys={selectedStepKeys} request={request} onChange={(option: IDropdownOption<BuildHealthOptionData>) => {
                    const key = option.key as string;
                    const isSelected = !!option.selected;

                    buildHealthOptions.toggleStep(key, option.text, isSelected);
                }} />
            </Stack>
        );
    }
});

// #endregion -- Option Specific Dropdown Components --

// #endregion -- Dropdown Components --

// #region -- Primary View Components --

/**
 * React Component used to manage the URL query params, and refresh it based on build health options. This is separate from the main component to scope rerenders to solely this component.
 * @returns React Component.
 */
const BuildHealthUrlSync: React.FC<{ buildHealthController: BuildHealthOptionsController }> = function ConstrucBuildHealthSidebar({ buildHealthController }) {
    const location = useLocation();
    const navigate = useNavigate();

    useEffect(() => {
        const initializeFromUrl = async () => {
            const params = parseBuildHealthQueryParams(location);
            await loadBuildHealthOptionsFromParams(params, buildHealthController, handler);
        };

        initializeFromUrl();
    }, []);

    useEffect(() => {
        const desiredSearch = buildHealthController.toNavigationQuery().toString();
        if (desiredSearch && location.search !== `?${desiredSearch}`) {
            navigate(`${location.pathname}?${desiredSearch}`, { replace: true });
        }
    }, [navigate, buildHealthController.optionsChangeVersion, location.pathname, location.search]);

    return null;
};

/**
 * React Component that represents the Build Health sidebar. All filters are present here.
 * @returns React Component.
 */
const BuildHealthSidebar: React.FC<{ handler: BuildHealthDataHandler, buildHealthOptions: BuildHealthOptionsController }> = function ConstrucBuildHealthSidebar({ handler, buildHealthOptions }) {
    const { hordeClasses } = getHordeStyling();
    const [timeSpanKey, setTimeSpanKey] = useState(() => buildHealthOptions.state.jobHistoryTimeSpan.key);
    const [includePreflight, setIncludePreflight] = useState(() => buildHealthOptions.state.includePreflight)
    const [hideDateAnchors, setHideDateAnchors] = useState(() => buildHealthOptions.state.includeDateAnchors)

    // #region -- Property Callbacks --

    const updateJobHistoryTimeSpan = (_ev, option) => {
        const select = option as TimeSpan;
        setTimeSpanKey(select.key);
        buildHealthOptions.setJobHistoryTimeSpan(select);
    };

    const updateIncludePreflight = (isChecked) => {
        setIncludePreflight(isChecked);
        buildHealthOptions.setIncludePreflight(isChecked);
    }

    const updateIncludeDateAnchors = (isChecked) => {
        setHideDateAnchors(isChecked);
        buildHealthOptions.setHideDateAnchors(isChecked);
    }

    // #endregion -- Property Callbacks --

    // #region -- Property Use Effects --

    useEffect(() => {
        setTimeSpanKey(buildHealthOptions.state.jobHistoryTimeSpan.key);
    }, [buildHealthOptions.state.jobHistoryTimeSpan.key]);

    useEffect(() => {
        setIncludePreflight(buildHealthOptions.state.includePreflight);
    }, [buildHealthOptions.state.includePreflight]);

    useEffect(() => {
        setHideDateAnchors(buildHealthOptions.state.includeDateAnchors);
    }, [buildHealthOptions.state.includeDateAnchors]);

    // #endregion -- Property Use Effects --

    let timeComboWidth = 180;

    return (
        <Stack style={{ minWidth: 300, maxWidth: 450, paddingRight: 18 }}>
            <Stack className={hordeClasses.modal}>
                <Stack>
                    <Stack style={{ paddingTop: 0, paddingBottom: 4 }}>
                        <Label>Step Outcome Filter</Label>
                    </Stack>
                    <ProjectDropdownSingle handler={handler} buildHealthOptions={buildHealthOptions} />
                    <StreamDropdown handler={handler} buildHealthOptions={buildHealthOptions} />
                    <TemplateDropdown handler={handler} buildHealthOptions={buildHealthOptions} />
                    <StepDropdown handler={handler} buildHealthOptions={buildHealthOptions} />
                    <Stack>
                        <TooltipHost content="Adjust the historical job history time span.">
                            <Label>Jobs Since:</Label>
                        </TooltipHost>
                        <ComboBox
                            styles={{ root: { width: timeComboWidth } }}
                            options={JobHistoryTimeSpans}
                            selectedKey={timeSpanKey}
                            onChange={(ev, option, index, value) => {
                                updateJobHistoryTimeSpan(ev, option);
                                buildHealthOptions.synchronizeDerivedKeys();
                            }}
                        />
                        {/* @todo - UE-315881 will remove the need for this constraint. */}
                        <TooltipHost content={
                            <div style={{ maxWidth: 300 }}>
                                <div>
                                    Include preflights in the job history results, or not.
                                </div>
                            </div>
                        }>
                            <Checkbox
                                label="Include Preflights"
                                checked={includePreflight}
                                onChange={(_, isChecked) => updateIncludePreflight(!!isChecked)}
                            />
                        </TooltipHost>
                        <TooltipHost content={
                            <div style={{ maxWidth: 300 }}>
                                <div>
                                    Include change submit date anchors in the header to separate changes.
                                </div>
                            </div>
                        }>
                            <Checkbox
                                label="Use Date Anchors"
                                checked={hideDateAnchors}
                                onChange={(_, isChecked) => updateIncludeDateAnchors(!!isChecked)}
                            />
                        </TooltipHost>
                    </Stack>
                </Stack>
            </Stack>
        </Stack>
    );
};

/**
 * React Component representing the entire Build Health view.
 * @returns React Component.
 */
export const BuildHealthView: React.FC = observer(function ConstructBuildHealthView() {
    const windowSize = useWindowSize();
    const { hordeClasses, modeColors } = getHordeStyling();

    const filter = useMemo(() => ({
        streams: buildHealthState.stepOutcomeEnabledStreamKeys,
        jobs: buildHealthState.stepOutcomeEnabledJobKeys,
        steps: buildHealthState.stepOutcomeEnabledStepKeys,
        jobHistorySpan: { start: buildHealthState.startDate, end: buildHealthState.endDate },
        includePreflights: buildHealthState.includePreflight,
        includeDateAnchors: buildHealthState.includeDateAnchors
    }), [
        buildHealthState.stepOutcomeEnabledStreamKeys,
        buildHealthState.stepOutcomeEnabledJobKeys,
        buildHealthState.stepOutcomeEnabledStepKeys,
        { start: buildHealthState.startDate, end: buildHealthState.endDate },
        buildHealthState.includePreflight,
        buildHealthState.includeDateAnchors
    ]);

    return (
        <Stack className={hordeClasses.horde}>
            <BuildHealthUrlSync buildHealthController={buildHealthController} />
            <TopNav />
            <Breadcrumbs items={[{ text: 'Build Health' }]} />
            <Stack horizontal>
                <div key={`windowsize_automationview_${windowSize.width}_${windowSize.height}`} style={{ width: 0, flexShrink: 0, backgroundColor: modeColors.background }} />
                <Stack horizontalAlign="center" grow styles={{ root: { width: "100%", padding: 12, backgroundColor: modeColors.background } }}>
                    <Stack styles={{ root: { width: "100%" } }}>
                        <Stack horizontal styles={{ root: { minHeight: '85vh' } }} >
                            <BuildHealthSidebar handler={handler} buildHealthOptions={buildHealthController} />
                            <Stack id="parent-buildhealth-stepoutcome" className={stepOutcomeTableClasses.root} grow style={{ overflowX: "auto", overflowY: "visible", minWidth: "1200px", position: "relative", border: `${modeColors.content} solid 2px` }}>
                                <StepOutcomeView filter={filter} />
                            </Stack>
                        </Stack>
                    </Stack>
                </Stack>
            </Stack>
        </Stack>
    );
});

// #endregion -- Primary View Components --

// #region -- Script --

const buildHealthState = new BuildHealthOptionsState();
const buildHealthController = new BuildHealthOptionsController(buildHealthState);
const handler = new BuildHealthDataHandler(buildHealthState, buildHealthController);

// #endregion -- Script --