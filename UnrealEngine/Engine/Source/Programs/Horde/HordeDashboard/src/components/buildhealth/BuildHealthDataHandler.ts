// Copyright Epic Games, Inc. All Rights Reserved.

import backend from "horde/backend";
import { JobData, JobStreamQuery, ProjectData, StreamData } from "horde/backend/Api";
import { makeObservable, observable, runInAction } from "mobx";
import { BuildHealthOptionsController, BuildHealthOptionsState } from "./BuildHealthOptions";
import { StepRefData, TemplateRefData } from "./BuildHealthDataTypes";
import { decodeTemplateKey } from "./BuildHealthUtilities";

// #region -- API Data Types & Utilities --

/**
 * Request object that controls what items to refresh upon Data Handler refresh.
 */
export type DataHandlerRefreshRequest = {
    projects?: boolean;
    streams?: boolean;
    jobs?: boolean;
    steps?: boolean;
}

export const DEFAULT_REFRESH_REQUEST: DataHandlerRefreshRequest = { projects: true, streams: true, jobs: true, steps: true };

/**
 * Utility function that will expand a Data Handler Refresh Request to include all the dependents in it's refresh.
 * @param request The request to expand.
 * @returns The expanded request.
 */
export function expandRefreshRequest(request: DataHandlerRefreshRequest): DataHandlerRefreshRequest {
    const expanded: DataHandlerRefreshRequest = { ...request };
    if (request.projects) {
        expanded.streams = true;
    }

    if (request.streams) {
        expanded.jobs = true;
    }

    if (request.jobs) {
        expanded.steps = true;
    }

    return expanded;
}

// #endregion -- API Data Types & Utilities --

/**
 * Data handler for all build health data used for dropdowns.
 * @todo Data sharing & caching would ideally occur between the BuildHealth View & Step Outcome.
 */
export class BuildHealthDataHandler {
    private readonly STREAMS_FILTER = "id,state,streamId,name,batches";
    private readonly STEP_DATA_SAMPLE_SIZE = 5;

    // #region -- Private Members --

    private lastRefresh = -1;
    private buildHealthOptions: BuildHealthOptionsState;
    private buildHealthOptionsController: BuildHealthOptionsController;
    private cachedStreamToTemplatesData: Map<string, TemplateRefData[]> = new Map<string, TemplateRefData[]>();

    // #endregion -- Private Members --

    // #region -- Public Members --

    @observable.shallow
    projectsData: ProjectData[] = [];

    @observable.shallow
    streamsData: StreamData[] = [];

    @observable.shallow
    templatesData: TemplateRefData[] = [];

    @observable.shallow
    stepData: StepRefData[] = [];

    // #endregion -- Public Members --

    // #region -- Constructor --

    constructor(buildHealthOptionsState: BuildHealthOptionsState, buildHealthOptionsController: BuildHealthOptionsController) {
        makeObservable(this);

        this.buildHealthOptions = buildHealthOptionsState;
        this.buildHealthOptionsController = buildHealthOptionsController;
    }

    //#endregion -- Constructor --

    // #region -- Private API --

    private async refreshData(request: DataHandlerRefreshRequest) {
        return this.getProjectsData(request);
    }

    // #endregion -- Private API --

    // #region -- Public API --

    /**
     * Requests a hierarchical data refresh. Refreshw ill occur if the options have changed since last refresh.
     * @param request The data refresh request message to use in order to control refresh depth.
     * @param force Whether to force the teh refresh or not. If false, will only refresh if the options have changed since last refresh.
     */
    async requestHierarchicalRefresh(request: DataHandlerRefreshRequest = DEFAULT_REFRESH_REQUEST, force?: boolean) {
        if (this.lastRefresh < this.buildHealthOptionsController.optionsChangeVersion || force) {
            this.lastRefresh = this.buildHealthOptionsController.optionsChangeVersion;
            await this.refreshData(request);
        }
    }

    /**
     * Obtains the project data given the current buildHealthOptions.
     * @param request The data refresh request message to use in order to control refresh depth.
     */
    async getProjectsData(request: DataHandlerRefreshRequest) {
        if (this.projectsData.length === 0 || request.projects) {
            const projectDataResult = (await backend.getProjects());

            runInAction(() => {
                this.projectsData = projectDataResult;
            });
        }

        await this.getStreamData(request);
    }

    /**
     * Obtains the stream data given the current buildHealthOptions.
     * @param request The data refresh request message to use in order to control refresh depth.
     */
    async getStreamData(request: DataHandlerRefreshRequest) {
        if (request.streams && Object.keys(this.buildHealthOptions.enabledProjects).length > 0) {
            let streamsData: StreamData[] = [];
            this.cachedStreamToTemplatesData.clear();

            this.projectsData.forEach((project: ProjectData) => {
                const isProjectEnabled = this.buildHealthOptions.enabledProjects.hasOwnProperty(project.id);
                if (!isProjectEnabled) {
                    return;
                }

                project.streams?.forEach((stream: StreamData) => {
                    streamsData.push(stream);
                    this.cachedStreamToTemplatesData.set(stream.id, stream.templates);
                });
            });

            runInAction(() => {
                this.streamsData = streamsData;
            });
        }

        await this.getTemplateData(request);
    }

    /**
     * Obtains the template data given the current buildHealthOptions.
     * @param request The data refresh request message to use in order to control refresh depth.
     */
    async getTemplateData(request: DataHandlerRefreshRequest) {
        if (request.jobs && Object.keys(this.buildHealthOptions.enabledStreams).length > 0) {
            let templatesData: TemplateRefData[] = [];
            for (const key of Object.keys(this.buildHealthOptions.enabledStreams)) {
                if (this.cachedStreamToTemplatesData.has(key)) {
                    const templates = this.cachedStreamToTemplatesData.get(key);
                    if (templates) {
                        templatesData.push(
                            ...templates.map(t => ({ ...t, streamId: key }))
                        );
                    }
                }
            }

            runInAction(() => {
                this.templatesData = templatesData;
            });
        }

        await this.getStepData(request);
    }

    /**
     * Obtains the step data given the current buildHealthOptions.
     * @param request The data refresh request message to use in order to control refresh depth.
     */
    async getStepData(request: DataHandlerRefreshRequest) {
        const entries = Object.entries(this.buildHealthOptions.enabledTemplates);
        if (request.steps && entries.length > 0) {
            const results = await Promise.all(entries.map(async ([key]) => {
                const templateData = decodeTemplateKey(key);

                const query: JobStreamQuery = {
                    template: [templateData.templateId],
                    count: this.STEP_DATA_SAMPLE_SIZE,
                    filter: this.STREAMS_FILTER
                };

                const jobDatas: JobData[] = await backend.getStreamJobs(templateData.streamId, query);
                const recentJobs = jobDatas.filter(job => job.batches);
                const stepMap = new Map<string, StepRefData>();

                for (const job of recentJobs) {
                    for (const batch of job.batches!) {
                        for (const step of batch.steps) {
                            const stepKey = step.name;
                            if (!stepMap.has(stepKey)) {
                                stepMap.set(stepKey, { ...step, streamId: templateData.streamId, templateId: decodeTemplateKey(key).templateId });
                            }
                        }
                    }
                }

                const distinctSteps = [...stepMap.values()];
                return distinctSteps;
            }));

            const allSteps = results.flat();

            runInAction(() => {
                this.stepData = [...allSteps.values()];
            });
        }
    }

    // #endregion -- Public API --
}