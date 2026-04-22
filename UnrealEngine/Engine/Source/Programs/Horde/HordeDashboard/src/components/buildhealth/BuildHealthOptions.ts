// Copyright Epic Games, Inc. All Rights Reserved.

import { action, makeObservable, observable, runInAction } from "mobx";
import { INCLUDE_PREFLIGHT_PARAM, PROJECTS_URL_PARAM_NAME, STEPS_URL_PARAM_NAME, STREAMS_URL_PARAM_NAME, TEMPLATES_URL_PARAM_NAME, TIME_SPAN_URL_PARAM } from "../buildhealth/BuildHealthDataTypes";
import { BuildHealthDataHandler } from "./BuildHealthDataHandler";
import { decodeStepKey, decodeTemplateKey, encodeStepKey, encodeTemplateKey } from "./BuildHealthUtilities";
import { Location } from 'react-router-dom';
import LZString from "lz-string"

// #region -- Options Utilities & Data Types --

/**
 * Data structure used to pass state from location query, to Build Health Options.
 */
type BuildHealthQueryParams = {
    projectIds: string[];
    streamIds: string[];
    templateIds: string[];
    stepNames: string[];
    timeSpanKey: string;
    includePreflight: boolean;
};

/**
 * Data type used to specify time span from now to consider for jobs.
 */
export type TimeSpan = {
    text: string;
    key: string;
    minutes: number;
}

/**
* List of currently supported time spans.
*/
export const JobHistoryTimeSpans: TimeSpan[] = [
    {
        text: "Past 15 Minutes", key: "time_15_minutes", minutes: 15
    },
    {
        text: "Past 1 Hour", key: "time_1_hour", minutes: 60
    },
    {
        text: "Past 4 Hours", key: "time_4_hours", minutes: 60 * 4
    },
    {
        text: "Past 1 Day", key: "time_1_day", minutes: 60 * 24
    },
    {
        text: "Past 2 Days", key: "time_2_days", minutes: 60 * 24 * 2
    },
    {
        text: "Past 1 Week", key: "time_1_week", minutes: 60 * 24 * 7
    },
    {
        text: "Past 2 Weeks", key: "time_2_week2", minutes: 60 * 24 * 7 * 2
    },
    {
        text: "Past 1 Month", key: "time_1_month", minutes: 60 * 24 * 31
    }
]

export const DEFAULT_JOB_HISTORY_TIME_SPAN: TimeSpan = JobHistoryTimeSpans[1];

/**
 * Parses the Build Health options from the query params.
 * @param location Location object.
 * @returns Parsed Build Health Query Params.
 */
export function parseBuildHealthQueryParams(location: Location): BuildHealthQueryParams {
    const searchParams = new URLSearchParams(location.search);
    const getList = (key: string) => {
        const raw = searchParams.get(key);
        if (!raw) return [];

        const decompressed = LZString.decompressFromEncodedURIComponent(raw);
        if (!decompressed) return [];

        return decompressed.split(",").filter(Boolean);
    };

    const projectIds = getList(PROJECTS_URL_PARAM_NAME);
    const streamIds = getList(STREAMS_URL_PARAM_NAME);
    const templateIds = getList(TEMPLATES_URL_PARAM_NAME);
    const stepNames = getList(STEPS_URL_PARAM_NAME);

    let timeSpanKey: string = (() => {
        const raw = searchParams.get(TIME_SPAN_URL_PARAM);
        return raw ? LZString.decompressFromEncodedURIComponent(raw) ?? "" : "";
    })();

    if (!JobHistoryTimeSpans.some(ts => ts.key === timeSpanKey)) {
        timeSpanKey = DEFAULT_JOB_HISTORY_TIME_SPAN.key;
    }

    // Read from URLSearchParams
    const includePreflight = (searchParams.get(INCLUDE_PREFLIGHT_PARAM) ? LZString.decompressFromEncodedURIComponent(searchParams.get(INCLUDE_PREFLIGHT_PARAM)!) : "false") === "true";

    return { projectIds, streamIds, templateIds: templateIds, stepNames, timeSpanKey, includePreflight };
}

/**
 * Processes provided Build Health Query Params for the Build Health View.
 * @param params The params to process.
 * @param options The Build Health Option controller to use to update options from the params.
 * @param handler  THe Build Health Data Handler to use to obtain data based on the params.
 */
export async function loadBuildHealthOptionsFromParams(
    params: BuildHealthQueryParams,
    options: BuildHealthOptionsController,
    handler: BuildHealthDataHandler) {

    runInAction(() => {
        options.setIncludePreflight(params.includePreflight);
    })

    if (params.timeSpanKey) {
        runInAction(() => {
            options.setJobHistoryTimeSpan(JobHistoryTimeSpans.find(ts => ts.key === params.timeSpanKey)!);
        });
    }

    // Get baseline project data first
    await handler.requestHierarchicalRefresh();

    runInAction(() => {
        options.clearProjects();
        params.projectIds.forEach(pid => {
            const project = handler.projectsData.find(p => p.id === pid);
            if (project) {
                options.toggleProject(pid, project.name, true);
            }
            else {
                console.warn(`Project parameter provided in URL query (${pid}) that could not be matched to underlying data - discarding.`);
            }
        });
    });

    await handler.getStreamData({ streams: true });

    runInAction(() => {
        options.clearStreams();
        params.streamIds.forEach(sid => {
            const stream = handler.streamsData.find(s => s.id === sid);
            if (stream) {
                options.toggleStream(sid, stream.name, true);
            }
            else {
                console.warn(`Stream parameter provided in URL query (${sid}) that could not be matched to underlying data - discarding.`);
            }
        });
    });

    await handler.getTemplateData({ jobs: true });

    runInAction(() => {
        options.clearTemplates();
        const decodedTemplates = params.templateIds.map(templateKey => decodeTemplateKey(templateKey));

        decodedTemplates.forEach(tid => {
            const template = handler.templatesData.find(t => t.id === tid.templateId && t.streamId == tid.streamId);
            if (template) {
                options.toggleTemplate(encodeTemplateKey(template), template.name, true);
            }
            else {
                console.warn(`Template parameter provided in URL query (${tid}) that could not be matched to underlying data - discarding.`);
            }
        });
    });

    await handler.getStepData({ steps: true });

    runInAction(() => {
        options.clearSteps();
        const decodedStepNames = params.stepNames.map(stepNameKey => decodeStepKey(stepNameKey));
        decodedStepNames.forEach(stepData => {
            const step = handler.stepData.find(s => s.name.toLowerCase() === stepData.stepName.toLowerCase() && s.streamId == stepData.streamId && s.templateId == stepData.templateId);
            if (step) {
                options.toggleStep(encodeStepKey(step), step.name, true);
            }
            else {
                console.warn(`Step name parameter provided in URL query (${name}) that could not be matched to underlying data - discarding.`);
            }
        });

        options.synchronizeDerivedKeys();
    });
}

// #endregion -- Options Utilities & Data Types --

/**
 * Build Health View Options.
 */
export class BuildHealthOptionsState {

    // #region -- Published Options --

    @observable stepOutcomeEnabledStreamKeys: string[] = [];
    @observable stepOutcomeEnabledJobKeys: string[] = [];
    @observable stepOutcomeEnabledStepKeys: string[] = [];
    @observable startDate!: Date;
    @observable endDate?: Date;

    // #endregion -- Published Options --

    // #region -- User Selected Options --

    @observable enabledProjects: Record<string, string> = {};
    @observable enabledStreams: Record<string, string> = {};
    @observable enabledTemplates: Record<string, string> = {};
    @observable enabledSteps: Record<string, string> = {};
    @observable jobHistoryTimeSpan: TimeSpan = DEFAULT_JOB_HISTORY_TIME_SPAN;
    @observable includePreflight: boolean = false;
    @observable includeDateAnchors: boolean = true;

    // #endregion -- User Selected Options --

    // #region -- Constructor --

    constructor() {
        makeObservable(this);
        this.endDate = new Date();

        const d = new Date();
        d.setDate(d.getDate() - 1);
        this.startDate = d;
    }

    // #endregion -- Constructor --
}

/**
 * Controller responsible for modifying the underlying BuildHealthOptionsState.
 */
export class BuildHealthOptionsController {
    // #region -- Private Members --

    readonly state: BuildHealthOptionsState;
    private lastSynchronize = -1;

    // #endregion -- Private Members --

    // #region -- Public Members --

    optionsChangeVersion = 0;

    // #endregion -- Public Members --

    // #region -- Constructor --

    constructor(state: BuildHealthOptionsState) {
        this.state = state;
        makeObservable(this);
    }

    private setOptionsChanged() {
        this.optionsChangeVersion++;
    }

    // #endregion -- Constructor --

    // #region -- Public API --

    /**
     * Toggles the state of the provided project. Will clear all other set projects.
     * @param id The id of the project to toggle.
     * @param name The name of the project.
     * @param enabled Whether it's enabled or not.
     */
    @action
    toggleSingleProject(id: string, name: string) {
        this.state.enabledProjects = { [id]: name };
        this.setOptionsChanged();
    }

    /**
     * Toggles the state of the provided project.
     * @param id The id of the project to toggle.
     * @param name The name of the project.
     * @param enabled Whether it's enabled or not.
     */
    @action
    toggleProject(id: string, name: string, enabled: boolean) {
        if (enabled) {
            this.state.enabledProjects[id] = name;
        } else {
            delete this.state.enabledProjects[id];
            this.clearStreams(true);
        }
        this.setOptionsChanged();
    }

    /**
     * Toggles the state of the provided stream. Will clear all other set streams.
     * @param id The id of the stream to toggle.
     * @param name The name of the stream.
     * @param enabled Whether it's enabled or not.
     */
    @action
    toggleSingleStream(id: string, name: string) {
        this.clearStreams(true);
        this.state.enabledStreams[id] = name;
        this.setOptionsChanged();
    }

    /**
     * Toggles the state of the provided stream.
     * @param id The id of the stream to toggle.
     * @param name The name of the stream.
     * @param enabled Whether it's enabled or not.
     */
    @action
    toggleStream(id: string, name: string, enabled: boolean) {
        if (enabled) {
            this.state.enabledStreams[id] = name;
        } else {
            delete this.state.enabledStreams[id];
            this.clearTemplates(true, id);
        }
        this.setOptionsChanged();
    }

    /**
     * Toggles the state of the provided template.
     * @param id The id of the template to toggle.
     * @param name The name of the template.
     * @param enabled Whether it's enabled or not.
     */
    @action
    toggleTemplate(id: string, name: string, enabled: boolean) {
        if (enabled) {
            this.state.enabledTemplates[id] = name;
        } else {
            delete this.state.enabledTemplates[id];
            let { streamId, templateId } = decodeTemplateKey(id);
            this.clearSteps({ streamId, templateId });
        }
        this.setOptionsChanged();
    }

    /**
     * Toggles the state of the provided step.
     * @param id The id of the step to toggle.
     * @param name The name of the step.
     * @param enabled Whether it's enabled or not.
     */
    @action
    toggleStep(id: string, name: string, enabled: boolean) {
        if (enabled) {
            this.state.enabledSteps[id] = name;
        } else {
            delete this.state.enabledSteps[id];
        }
        this.setOptionsChanged();
    }

    /**
     * Sets the current job history time span to utilize.
     * @param time The time span to utilize.
     */
    @action
    setJobHistoryTimeSpan(time: TimeSpan) {
        this.state.jobHistoryTimeSpan = time;
        this.setOptionsChanged();
    }

    /**
     * Sets whether to filter out preflight jobs, or not.
     * @param includePreflight True to include preflight job instances, false otherwise.
     */
    @action
    setIncludePreflight(includePreflight: boolean) {
        this.state.includePreflight = includePreflight;
        this.setOptionsChanged();
    }

    /**
     * Sets whether the date anchors should be used when visualizing the table.
     * @param includeDateAnchors 
     */
    @action
    setHideDateAnchors(includeDateAnchors: boolean) {
        this.state.includeDateAnchors = includeDateAnchors;
        this.setOptionsChanged();
    }

    /**
      * Clear all the projects tracked by the options.
      * @param cascade Whether to cascade this down to dependent options.
      */
    clearProjects(cascade?: boolean) {
        Object.keys(this.state.enabledProjects).forEach(key => {
            delete this.state.enabledProjects[key];
        });

        if (cascade) {
            this.clearStreams(cascade);
        }
    }

    /**
      * Clear all the streams tracked by the options.
      * @param cascade Whether to cascade this down to dependent options.
      */
    clearStreams(cascade?: boolean) {
        Object.keys(this.state.enabledStreams).forEach(key => {
            delete this.state.enabledStreams[key];

            if (cascade) {
                this.clearTemplates(cascade, key);
            }
        });

    }

    /**
      * Clear all the templates tracked by the options.
      * @param cascade Whether to cascade this down to dependent options.
      * @param owningStreamId Whether to clear an item if it belongs to a stream. If not provided, will remove any template.
      */
    clearTemplates(cascade?: boolean, owningStreamId?: string) {
        Object.keys(this.state.enabledTemplates).forEach(key => {
            let { streamId, templateId } = decodeTemplateKey(key);
            if (!owningStreamId || (streamId === owningStreamId)) {
                delete this.state.enabledTemplates[key];

                if (cascade) {
                    this.clearSteps(owningStreamId ? { streamId: owningStreamId!, templateId: templateId } : undefined);
                }
            }
        });

    }

    /**
     * Clear all the steps tracked by the options.
     * @param owningHierarchy Whether to clear an item if it belongs to a stream & template. If not provided, will remove any step.
     */
    clearSteps(owningHierarchy?: { streamId: string; templateId: string }) {
        Object.keys(this.state.enabledSteps).forEach(key => {
            let { streamId, templateId, stepName } = decodeStepKey(key);
            if ((!owningHierarchy) || (streamId === owningHierarchy.streamId && templateId === owningHierarchy.templateId)) {
                delete this.state.enabledSteps[key];
            }
        });
    }

    /**
     * Sycnrhonizes the options that are bound to UI elements, with the data observed by consumers of the option set.
     * This is important to keep separate as it allows us to control when we signal/flush a finalized set of options to the consumers.
     */
    @action
    synchronizeDerivedKeys() {
        if (this.lastSynchronize < this.optionsChangeVersion) {
            this.state.stepOutcomeEnabledStreamKeys = Object.keys(this.state.enabledStreams);
            this.state.stepOutcomeEnabledJobKeys = Object.keys(this.state.enabledTemplates);
            this.state.stepOutcomeEnabledStepKeys = Object.keys(this.state.enabledSteps);
            this.state.startDate = new Date(new Date().valueOf() - (this.state.jobHistoryTimeSpan.minutes * 60000));
            this.state.endDate = new Date();

            this.lastSynchronize = this.optionsChangeVersion;
        }
    }

    /**
     * Obtains a URLSearchParams representation of the current options.
     * @returns The URLSearchParams representation of the current options.
     */
    toNavigationQuery(): URLSearchParams {
        const params = new URLSearchParams(location.search);

        const projects = Object.keys(this.state.enabledProjects).join(",");
        const streams = this.state.stepOutcomeEnabledStreamKeys.join(",");
        const jobs = this.state.stepOutcomeEnabledJobKeys.join(",");
        const steps = this.state.stepOutcomeEnabledStepKeys.join(",");
        const lastTimeRange = this.state.jobHistoryTimeSpan.key.toString();
        const includePreflight = this.state.includePreflight;

        if (!projects.length) return params;

        params.set(PROJECTS_URL_PARAM_NAME, LZString.compressToEncodedURIComponent(projects));
        params.set(STREAMS_URL_PARAM_NAME, LZString.compressToEncodedURIComponent(streams));
        params.set(TEMPLATES_URL_PARAM_NAME, LZString.compressToEncodedURIComponent(jobs));
        params.set(STEPS_URL_PARAM_NAME, LZString.compressToEncodedURIComponent(steps));
        params.set(TIME_SPAN_URL_PARAM, LZString.compressToEncodedURIComponent(lastTimeRange));
        params.set(INCLUDE_PREFLIGHT_PARAM, LZString.compressToEncodedURIComponent(String(includePreflight)));

        return params;
    }

    // #endregion -- Public API --
}