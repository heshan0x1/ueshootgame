// Copyright Epic Games, Inc. All Rights Reserved.

import { GetStepResponse, GetTemplateRefResponse } from "horde/backend/Api";

export const PROJECTS_URL_PARAM_NAME: string = "projects";
export const STREAMS_URL_PARAM_NAME: string = "streams";
export const TEMPLATES_URL_PARAM_NAME: string = "templates";
export const STEPS_URL_PARAM_NAME: string = "stepNames";
export const PARAMETER_KEY_PREFIX: string = "parameter_key";
export const TIME_SPAN_URL_PARAM: string = "lastTimeRange";
export const INCLUDE_PREFLIGHT_PARAM: string = "includePreflight";
export const KEY_SEPARATOR: string = "::";

/**
 * Convenience type to fold in extra metadata relevant for the Template. 
 */
export type TemplateRefData = GetTemplateRefResponse & {
    streamId?: string;
    fullname?: string;
}

/**
 * Convenience type to fold in extra metadata relevant for the Step. 
 */
export type StepRefData = GetStepResponse & {
    templateId?: string;
    streamId?: string;
}