// Copyright Epic Games, Inc. All Rights Reserved.

import { KEY_SEPARATOR, StepRefData, TemplateRefData } from "./BuildHealthDataTypes";

/**
 * Produces an encoded template key for unique identification.
 * @param template The template to produce the key for.
 * @returns The key representing the template.
 */
export function encodeTemplateKey(template: TemplateRefData): string {
    return `${template.streamId}${KEY_SEPARATOR}${template.id}`;
}

/**
 * Decodes an encoded template key, providing it's constituent parts.
 * @param templateKey The template key to decode.
 * @returns The constituent parts.
 */
export function decodeTemplateKey(templateKey: string): { streamId: string; templateId: string } {
    const [streamId, templateId] = templateKey.split(KEY_SEPARATOR);
    return { streamId, templateId };
}

/**
 * Produces an encoded step key for unique identification
 * @param streamId The streamId to encode.
 * @param templateId The templateId to encode.
 * @param stepName The stepName to encode.
 * @returns The key representing the step.
 * @remark We don't want toLocalLowerCase the templateId until UE-315953 is completed in order to bump the url encoding query.
 */
export function encodeStepNameFromStrings(streamId: string, templateId: string, stepName: string) {
    return `${streamId.toLocaleLowerCase()}${KEY_SEPARATOR}${templateId}${KEY_SEPARATOR}${stepName.toLocaleLowerCase()}`;
}

/**
 * Produces an encoded step key for unique identification.
 * @param step The step to produce the key for.
 * @returns The key representing the step.
 */
export function encodeStepKey(step: StepRefData) {
    return `${step.streamId}${KEY_SEPARATOR}${step.templateId}${KEY_SEPARATOR}${step.name}`;
}

/**
 * Decodes an encoded step key, providing it's constituent parts.
 * @param templateKey The step key to decode.
 * @returns The constituent parts.
 */
export function decodeStepKey(stepKey: string): { streamId: string, templateId: string, stepName: string } {
    const [streamId, templateId, stepName] = stepKey.split(KEY_SEPARATOR);
    return { streamId, templateId, stepName };
}