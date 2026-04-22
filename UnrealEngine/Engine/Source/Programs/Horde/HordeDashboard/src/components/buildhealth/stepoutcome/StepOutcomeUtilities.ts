// Copyright Epic Games, Inc. All Rights Reserved.

export const DURATION_NOT_SET: number = -1;

/**
 * Normalizes an ambiguous input (Date | string) to a Date object. 
 * @param input the input string union type.
 * @returns The @see Date standard representation for the time. 
 */
export function toDate(input?: Date | string): Date | undefined {
    if (!input) {
        return undefined;
    }

    if (input instanceof Date) {
        return input;
    }

    const parsed = new Date(input);
    return isNaN(parsed.getTime()) ? undefined : parsed;
}

/**
 * Computes the duration of a window in ms from a common time response format (Date | string).
 * @param startTime The start time of the window.
 * @param finishTime The finish time of the window.
 * @returns The duration of the window in ms, @see DURATION_NOT_SET if an invalid window is provided.
 */
export function computeDuration(startTime?: Date | string, finishTime?: Date | string): number {
    const startDate = toDate(startTime);
    const finishDate = toDate(finishTime);
    return startDate && finishDate
        ? finishDate.getTime() - startDate.getTime()
        : DURATION_NOT_SET;
}

/**
 * Generates a formatted duration provided a time span in milliseconds. 
 * @param ms The duration in milliseconds.
 * @returns The formated duration.
 */
export function formatDurationFromMs(ms: number): string {
    const diffSeconds = Math.floor(ms / 1000);
    const hours = Math.floor(diffSeconds / 3600);
    const minutes = Math.floor((diffSeconds % 3600) / 60);
    const seconds = diffSeconds % 60;

    return `${hours}h ${minutes}m ${seconds}s`;
}

/**
 * Generates a formatted duration provided a start and finish time.
 * @param startISO The start time, in string representation.
 * @param finishISO The finish time, in string representation.
 * @returns The formated duration.
 */
export function formatDuration(startISO: string, finishISO: string): string {
    const start = new Date(startISO);
    const finish = new Date(finishISO);
    const diffMs = finish.getTime() - start.getTime();

    return formatDurationFromMs(diffMs);
}