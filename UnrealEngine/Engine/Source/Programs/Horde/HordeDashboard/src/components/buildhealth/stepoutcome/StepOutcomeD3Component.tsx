// Copyright Epic Games, Inc. All Rights Reserved.

import { mergeStyleSets } from "@fluentui/react";
import * as d3 from "d3";
import { getHordeStyling, preloadFonts } from "horde/styles/Styles";
import { useEffect, useRef } from "react";
import { StepOutcomeDataHandler } from "./StepOutcomeDataHandler";
import { ChangeHeader, ColumnHeader, encodeStepNameFromStepNameHeader, isChangeHeader, isDateHeader, isStepOutcome, isSummary, isSummaryHeader, StepNameHeader, StepOutcomeTable, TableEntry } from "./StepOutcomeDataTypes";
import { getCellStyle, getColorRecords, IncrementalRefreshIndicatorPanel, sharedBorderStyle } from "./StepOutcomeSharedUIComponents";
import { getHordeTheme } from "horde/styles/theme";

// #region -- Styles --

const baseTooltipLine = {
    paddingLeft: 8,
    margin: 0.2,
};

const tooltipClasses = mergeStyleSets({
    toolTipSection: sharedBorderStyle.bottomBorder,
    tooltipLine: baseTooltipLine,
    tooltipLineWithBorder: {
        ...baseTooltipLine,
        ...sharedBorderStyle.bottomBorder,
    }
});

// #endregion -- Styles --

/**
 * Function to create a D3 format table for the step outcome table.
 * @param stepOutcomeTable The data to base the table off of.
 * @param handler The data handler used to query step outcome data.
 * @param onCellSelected Cell selected callback.
 * @returns React Component.
 */
function GenerateD3Table(stepOutcomeTable: StepOutcomeTable, handler: StepOutcomeDataHandler, onCellClick: any,): JSX.Element {
    // #region -- Tooltip Helpers --

    const TOOLTIP_SPACER: number = 4;

    let attachDynamicPositionToolTip = (event: MouseEvent, tooltip: d3.Selection<HTMLDivElement, unknown, HTMLElement, any>, tooltipContent: HTMLDivElement) => {
        const _d3 = d3 as any;

        tooltip.html("");
        const toolTipNode = tooltip.node();
        toolTipNode?.appendChild(tooltipContent);

        const tooltipWidth = toolTipNode?.offsetWidth ?? 0;
        const tooltipHeight = toolTipNode?.offsetHeight ?? 0;

        // Get absolute  mouse position.
        const x = event.pageX;
        const y = event.pageY;

        let leftPos = x + TOOLTIP_SPACER;
        let topPos = y - tooltipHeight / 2;

        // If tooltip goes off right edge, flip to the left.
        if (x + tooltipWidth + TOOLTIP_SPACER > window.innerWidth) {
            leftPos = x - tooltipWidth - TOOLTIP_SPACER;
        }

        // Prevent it from going off the top/bottom.
        if (topPos < 0) topPos = 0;
        if (topPos + tooltipHeight > window.innerHeight) {
            topPos = window.innerHeight - tooltipHeight;
        }

        tooltip
            .style("opacity", 1)
            .style("left", `${leftPos}px`)
            .style("top", `${topPos}px`);
    };

    let attachTargettedToolTip = (element: HTMLElement, tooltip: d3.Selection<HTMLDivElement, unknown, HTMLElement, any>, tooltipContent: HTMLDivElement) => {
        tooltip.html("");
        const toolTipNode = tooltip.node();
        toolTipNode?.appendChild(tooltipContent);

        const tooltipWidth = toolTipNode?.offsetWidth ?? 0;
        const tooltipHeight = toolTipNode?.offsetHeight ?? 0;

        const cell = element as HTMLElement;
        const rect = cell.getBoundingClientRect();

        let leftPos = rect.right + TOOLTIP_SPACER; // default: to the right.

        // if tooltip goes outside window, position to the left of cell.
        if (rect.right + tooltipWidth + TOOLTIP_SPACER > window.innerWidth) {
            leftPos = rect.left - tooltipWidth - TOOLTIP_SPACER;
        }

        tooltip
            .style("opacity", 1)
            .style("left", `${leftPos}px`)
            .style("top", `${rect.bottom - tooltipHeight}px`);
    };

    // #endregion -- Tooltip Helpers --

    const containerRef = useRef<HTMLDivElement>(null);
    const { modeColors, _hordeClasses, _detailClasses } = getHordeStyling();
    const theme = getHordeTheme();
    const colorRecords: Record<string, string> = getColorRecords();
    const summaryColorScale = d3.scaleLinear<string>()
        .domain([0, 0.5, 1])
        .range([colorRecords["Failure"], colorRecords["Warnings"], colorRecords["Success"]]);

    const scaffold = useRef<{
        wrapper: d3.Selection<HTMLDivElement, unknown, null, undefined>;
        table: d3.Selection<HTMLTableElement, unknown, null, undefined>;
        thead: d3.Selection<HTMLTableSectionElement, unknown, null, undefined>;
        theaderrow: d3.Selection<HTMLTableRowElement, unknown, null, undefined>;
        tbody: d3.Selection<HTMLTableSectionElement, unknown, null, undefined>;
        tooltip: d3.Selection<HTMLDivElement, unknown, HTMLElement, any>;
    } | null>(null);

    // Single execution useEffect to create static part of table.
    useEffect(() => {
        if (!containerRef.current || scaffold.current) return;

        // Create our base container.
        const wrapper = d3.select(containerRef.current)
            .append("div")
            .attr("id", "step-outcome-table-container");

        // Create the table element.
        const table = wrapper.append("table")
            .style("border-collapse", "separate")
            .style("border-spacing", "0px")
            .style("width", "fit-content")
            .style("table-layout", "fixed")
            .style("font-family", preloadFonts[0])
            .style("font-size", "12px")
            .style("color", modeColors.text);

        // Add header && tbody.
        const thead = table.append("thead");
        const tbody = table.append("tbody");

        // Add tool tip div; will be used throughout.
        d3.selectAll(".d3-tooltip").remove();

        const tooltip = d3.select("body")
            .append("div")
            .attr("class", "d3-tooltip")
            .style("position", "absolute")
            .style("pointer-events", "none")
            .style("padding", "6px 12px")
            .style("background", "rgba(0,0,0,0.95)")
            .style("color", "#fff")
            .style("z-index", "5")
            .style("min-width", "250px")
            .style("font-size", "12px")
            .style("border-radius", "2px")
            .style("line-height", "1.4")
            .style("opacity", 0);

        const theaderrow = thead.append("tr").style("height", "100px");

        // First column header; (0,0) - we just want to hide this; sits on top of group; guarantee the width of groups TH.
        theaderrow.append("th")
            .style("position", "sticky")
            .style("z-index", "6")
            .style("top", "0")
            .style("left", "0")
            .style("background-color", modeColors.background)
            .style("width", "30px")
            .style("min-width", "30px")
            .style("max-width", "30px");

        scaffold.current = { wrapper, table, thead, theaderrow, tbody, tooltip };

        return () => {
            d3.selectAll(".d3-tooltip").remove();
        };
    }, []);

    // Dependency array use effect for stepOutcomeTable data.
    useEffect(() => {
        if (!containerRef.current || !scaffold.current) return;

        // Second Column Header; (0, 1) - used to display refresh time - this sits on top of step name; guarantee the width of step name TH.
        scaffold.current.theaderrow
            .selectAll("th.refresh-header")
            .data([handler.lastRefreshDate])
            .join("th")
            .attr("class", "refresh-header")
            .style("position", "sticky")
            .style("z-index", "6")
            .style("top", "0")
            .style("left", "30px")
            .style("background-color", modeColors.background)
            .style("width", "320px")
            .style("min-width", "320px")
            .style("max-width", "320px")
            .style("cursor", "pointer")
            .text(d =>
                d
                    ? `Last Refresh: ${d.toLocaleTimeString([], {
                        hour: '2-digit',
                        minute: '2-digit',
                        second: '2-digit',
                        timeZoneName: 'short'
                    })}`
                    : ""
            )
            .on("mouseover", function (_event, _d) {
                d3.select(this).style("background-color", theme.semanticColors.bodyBackgroundHovered);
                const totalSeconds = Math.floor(handler.refreshTime / 1000);
                const minutes = Math.floor(totalSeconds / 60);
                const seconds = totalSeconds % 60;
                const formatted = `${minutes}m ${seconds}s`;

                const tooltipContent = document.createElement("div");
                tooltipContent.style.paddingLeft = "4px";

                const refreshRateDiv = document.createElement("div");
                refreshRateDiv.innerHTML = `<strong>Refresh Rate:</strong><p class="${tooltipClasses.tooltipLine}">${formatted}</p><p>Click to refresh.</p>`;
                tooltipContent.appendChild(refreshRateDiv);

                attachTargettedToolTip(this as HTMLElement, scaffold.current!.tooltip, tooltipContent);
            })
            .on("mouseout", function () {
                scaffold.current!.tooltip.style("opacity", 0);
                d3.select(this).style("background-color", modeColors.content);
            })
            .on("click", (_event, _d) => {
                if (handler.isInFullDataRefresh || handler.isInIncrementalDataRefresh) {
                    return;
                }

                handler.poll();
            });

        // Add all columns to the header row; these will be the changelists; these control downstream widths
        scaffold.current.theaderrow.selectAll("th.col")
            .data([...stepOutcomeTable.getColumnHeaders()] as ColumnHeader[])
            .join("th")
            .attr("class", "col")
            .style("position", "sticky")
            .style("top", "0")
            .style("z-index", "3")
            .style("background-color", modeColors.content)
            .style("width", d => isSummaryHeader(d) ? "70px" : "30px")
            .style("font-size", d => (isDateHeader(d) || isSummaryHeader(d) ? "large" : "small"))
            .style("box-sizing", "border-box")
            .style("writing-mode", "vertical-rl")
            .style("transform", "rotate(180deg)")
            .style("padding-bottom", "5px")
            .style("text-align", "right")
            .style("border", `1px solid ${modeColors.background}`)
            .text(entry => isSummaryHeader(entry) ? entry.name : (isChangeHeader(entry) ? entry.change : (entry.date ?? "")))
            .on("mouseover", function (_event, d) {
                const entry = d as unknown as ChangeHeader;

                if (isDateHeader(entry) || isSummaryHeader(entry)) {
                    return;
                }

                d3.select(this).style("background-color", theme.semanticColors.bodyBackgroundHovered);
                if (!entry) {
                    {
                        return;
                    }
                }

                if (isChangeHeader(entry)) {
                    const tooltipContent = document.createElement("div");
                    tooltipContent.style.paddingLeft = "4px";

                    const changeHeader = { change: entry.change ?? "-", date: entry.date ?? "-" };

                    const changeDiv = document.createElement("div");
                    changeDiv.innerHTML = `<strong>Change:</strong><p class="${tooltipClasses.tooltipLine}">${changeHeader.change}</p>`;
                    tooltipContent.appendChild(changeDiv);

                    const dateUTCDiv = document.createElement("div");
                    dateUTCDiv.innerHTML = `<strong>Date (UTC):</strong><p class="${tooltipClasses.tooltipLine}">${new Date(changeHeader.date!).toISOString()}</p>`;
                    tooltipContent.appendChild(dateUTCDiv);

                    const dateLocalDiv = document.createElement("div");
                    dateLocalDiv.innerHTML = `<strong>Date (Local):</strong><p class="${tooltipClasses.tooltipLine}">${new Date(changeHeader.date!).toString()}</p>`;
                    tooltipContent.appendChild(dateLocalDiv);

                    attachTargettedToolTip(this as HTMLElement, scaffold.current!.tooltip, tooltipContent);
                }
            })
            .on("mouseout", function () {
                scaffold.current!.tooltip.style("opacity", 0);
                d3.select(this).style("background-color", modeColors.content);
            });

        const rows = scaffold.current.tbody.selectAll("tr")
            .data(stepOutcomeTable.stepNameRowHeaders as StepNameHeader[])
            .join("tr")
            .style("height", "30px")
            .style("padding-top", "4px")
            .style("padding-bottom", "4px");

        // Add-Update all of the stream names as groups
        const streamTracker = new Map<string, boolean>();

        // Add all of our stream groupings
        rows.each(function (rowData: StepNameHeader) {
            const tr = d3.select(this);
            const stream = rowData.streamId;

            // Only add a TH for the first row of each stream
            const streamData = !streamTracker.has(stream)
                ? [{
                    streamId: stream,
                    rowspan: stepOutcomeTable.tableStreamMetadata.get(stream) ?? 1
                }]
                : [];

            tr.selectAll("th.stream")
                .data(streamData)
                .join(
                    enter => enter.append("th")
                        .attr("class", "stream")
                        .attr("rowspan", d => d.rowspan)
                        .style("position", "sticky")
                        .style("left", "0")
                        .style("z-index", "2")
                        .style("writing-mode", "vertical-rl")
                        .style("transform", "rotate(180deg)")
                        .style("background-color", modeColors.content)
                        .style("white-space", "nowrap")
                        .style("overflow", "hidden")
                        .style("text-overflow", "ellipsis")
                        .style("font-size", "16px")
                        .style("text-align", "right")
                        .style("padding-bottom", "10px")
                        .style("box-sizing", "border-box")
                        .style("border-left", "1px solid")
                        .style("border-right", "1px solid")
                        .style("border-bottom", "1px solid")
                        .style("border-top", "1px solid")
                        .text(d => d.streamId),
                    update => update.text(d => d.streamId),
                    exit => exit.remove()
                );

            // Attach tool-tip handlers.
            tr.selectAll("th.stream").each(function (stepNameHeader: StepNameHeader) {
                const th = d3.select(this);
                th.on("mouseover", (event: MouseEvent) => {
                    d3.select(this).style("background-color", theme.semanticColors.bodyBackgroundHovered);

                    const tooltipContent = document.createElement("div");
                    tooltipContent.style.paddingLeft = "4px";

                    if (stepNameHeader) {
                        const streamDiv = document.createElement("div");
                        streamDiv.innerHTML = `<strong>Stream:</strong><p class="${tooltipClasses.tooltipLine}">${stepNameHeader.streamId}</p>`;
                        tooltipContent.appendChild(streamDiv);
                    }
                    attachDynamicPositionToolTip(event, scaffold.current!.tooltip, tooltipContent);
                });

                th.on("mouseout", () => {
                    scaffold.current!.tooltip.style("opacity", 0);
                    d3.select(this).style("background-color", modeColors.content);
                });
            });

            streamTracker.set(stream, true);
        });

        // Accessability row highlights.
        rows.on("mouseover", function () {
            d3.select(this).style("background-color", theme.semanticColors.bodyBackgroundHovered);
        })
            .on("mouseout", function () {
                d3.select(this).style("background-color", null);
            });

        // Add the first "step name" column item.
        rows.each(function (rowData: StepNameHeader) {
            const tr = d3.select(this);
            const cells = tr.selectAll("td.step-name")
                .data([rowData])
                .join(
                    enter => enter.append("td").attr("class", "step-name").text(d => d.stepName),
                    update => update.text(d => d.stepName),
                    exit => exit.remove()
                );

            cells.style("position", "sticky")
                .style("left", "32px")
                .style("z-index", "1")
                .style("background-color", modeColors.content)
                .style("white-space", "nowrap")
                .style("overflow", "hidden")
                .style("padding-left", "8px")
                .style("text-overflow", "ellipsis")
                .style("box-sizing", "border-box")
                .text(d => `${d.stepName}`)
                .on("mouseover", function (_event, stepNameHeader) {
                    d3.select(this).style("background-color", theme.semanticColors.bodyBackgroundHovered);

                    const castedStepNameHeader = stepNameHeader as unknown as StepNameHeader;
                    const tooltipContent = document.createElement("div");
                    tooltipContent.style.paddingLeft = "4px";

                    if (castedStepNameHeader) {
                        const stepDiv = document.createElement("div");
                        stepDiv.innerHTML = `<strong>Step:</strong><p class="${tooltipClasses.tooltipLine}">${castedStepNameHeader.stepName}</p>`;
                        tooltipContent.appendChild(stepDiv);

                        const streamDiv = document.createElement("div");
                        streamDiv.innerHTML = `<strong>Stream:</strong><p class="${tooltipClasses.tooltipLine}">${castedStepNameHeader.streamId}</p>`;
                        tooltipContent.appendChild(streamDiv);
                    }

                    attachTargettedToolTip(this as HTMLElement, scaffold.current!.tooltip, tooltipContent);
                })
                .on("mouseout", function () {
                    scaffold.current!.tooltip.style("opacity", 0);
                    d3.select(this).style("background-color", modeColors.content);
                });
        });

        rows.selectAll("td.step-name").style("border", `1px solid ${modeColors.background}`);

        const styleAndBindStepCell = (
            selection: d3.Selection<HTMLDivElement, TableEntry, any, any>
        ) => {
            selection
                .style("vertical-align", "middle")
                .style("width", "90%")
                .style("height", "100%")
                .style("display", "inline-block")
                .style("text-align", "center")
                .each(function (entry) {
                    const div = d3.select(this);

                    if (!entry) {
                        div.text("").style("background-color", "transparent").style("cursor", "default");
                        return;
                    }

                    if (isStepOutcome(entry)) { // StepOutcomeTableEntry
                        div.text(""),
                            div.style("cursor", "pointer")
                                .style("background-color", getCellStyle(entry).bgColor)
                                .on("click", () => onCellClick?.(entry))
                                .on("mouseover", function () {
                                    const tooltipContent = document.createElement("div");
                                    tooltipContent.style.paddingLeft = "4px";

                                    const changeDiv = document.createElement("div");
                                    changeDiv.innerHTML = `<strong>Change:</strong><p class="${tooltipClasses.tooltipLineWithBorder}">${entry.change ?? "-"}</p>`;
                                    tooltipContent.appendChild(changeDiv);

                                    const durationDiv = document.createElement("div");
                                    durationDiv.innerHTML = `<strong>Duration:</strong><p class="${tooltipClasses.tooltipLine}">${entry.getDurationString?.() ?? "-"}</p>`;
                                    tooltipContent.appendChild(durationDiv);

                                    const stateDiv = document.createElement("div");
                                    stateDiv.innerHTML = `<strong>State:</strong><p class="${tooltipClasses.tooltipLineWithBorder}">${entry.stepResponse?.state ?? "-"}</p>`;
                                    tooltipContent.appendChild(stateDiv);

                                    const streamDiv = document.createElement("div");
                                    streamDiv.innerHTML = `<strong>Stream:</strong><p class="${tooltipClasses.tooltipLine}">${entry.streamId}</p>`;
                                    tooltipContent.appendChild(streamDiv);

                                    const jobDiv = document.createElement("div");
                                    jobDiv.innerHTML = `<strong>Job:</strong><p class="${tooltipClasses.tooltipLine}">${entry.jobName ?? "-"}</p>`;
                                    tooltipContent.appendChild(jobDiv);

                                    const stepDiv = document.createElement("div");
                                    stepDiv.innerHTML = `<strong>Step:</strong><p class="${tooltipClasses.tooltipLine}">${entry.stepResponse?.name ?? "-"}</p>`;
                                    tooltipContent.appendChild(stepDiv);

                                    attachTargettedToolTip(this as HTMLElement, scaffold.current!.tooltip, tooltipContent);
                                })
                                .on("mouseout", () => {
                                    scaffold.current!.tooltip.style("opacity", 0);
                                });
                    } else if (isSummary(entry)) { // SummaryTableEntry
                        const ratio = entry.steps > 0 ? entry.stepsPass / entry.stepsCompleted : 0;
                        let ratioStr: string = `${(ratio * 100).toFixed(1)}%`;

                        div.style("cursor", "default")
                            .style("background-color", summaryColorScale(ratio))
                            .style("width", "100%")
                            .style("display", "flex")
                            .style("align-items", "center")
                            .style("justify-content", "center")
                            .style("font-weight", "900")
                            .style("color", "black")
                            .text(entry.steps ? ratioStr : "-")
                            .on("click", null)
                            .on("mouseover", function () {
                                const tooltipContent = document.createElement("div");
                                tooltipContent.style.paddingLeft = "4px";

                                // Show ratio in proper format
                                const ratio = document.createElement("div");
                                ratio.style.display = "flex";
                                ratio.style.justifyContent = "space-between";
                                ratio.style.alignItems = "flex-start";
                                ratio.className = tooltipClasses.toolTipSection;

                                const leftDiv = document.createElement("div");
                                leftDiv.style.display = "flex";
                                leftDiv.style.flexDirection = "column";

                                const label = document.createElement("strong");
                                label.textContent = "Success Ratio:";
                                leftDiv.appendChild(label);

                                const desc = document.createElement("span");
                                desc.style.fontSize = "smaller";
                                desc.textContent = "(Success / Completed Steps)";
                                leftDiv.appendChild(desc);

                                // Right side: ratio value
                                const rightDiv = document.createElement("span");
                                rightDiv.textContent = ratioStr;

                                // Append both sides to the main container
                                ratio.appendChild(leftDiv);
                                ratio.appendChild(rightDiv);

                                tooltipContent.appendChild(ratio);

                                // Show totals
                                const runningSteps = document.createElement("div");
                                runningSteps.style.display = "flex";
                                runningSteps.style.justifyContent = "space-between";
                                runningSteps.innerHTML = `<strong>Pending Steps:</strong><span>${entry.stepsPending ?? "-"}</span>`;
                                tooltipContent.appendChild(runningSteps);

                                const succeededSteps = document.createElement("div");
                                succeededSteps.style.display = "flex";
                                succeededSteps.style.justifyContent = "space-between";
                                succeededSteps.innerHTML = `<strong>Succeeded Steps:</strong><span>${entry.stepsPass ?? "-"}</span>`;
                                tooltipContent.appendChild(succeededSteps);

                                const warningSteps = document.createElement("div");
                                warningSteps.style.display = "flex";
                                warningSteps.style.justifyContent = "space-between";
                                warningSteps.innerHTML = `<strong>Warning Steps:</strong><span>${entry.stepsWarning ?? "-"}</span>`;
                                tooltipContent.appendChild(warningSteps);

                                const failedSteps = document.createElement("div");
                                failedSteps.style.display = "flex";
                                failedSteps.style.justifyContent = "space-between";
                                failedSteps.innerHTML = `<strong>Failed Steps:</strong><span>${entry.stepsFail ?? "-"}</span>`;
                                tooltipContent.appendChild(failedSteps);

                                const completedStepsDiv = document.createElement("div");
                                completedStepsDiv.style.display = "flex";
                                completedStepsDiv.style.justifyContent = "space-between";
                                completedStepsDiv.innerHTML = `<strong>Completed Steps:</strong><span>${entry.stepsCompleted ?? "-"}</span>`;
                                tooltipContent.appendChild(completedStepsDiv);

                                attachTargettedToolTip(this as HTMLElement, scaffold.current!.tooltip, tooltipContent);
                            })
                            .on("mouseout", () => {
                                scaffold.current!.tooltip.style("opacity", 0);
                            });
                    }
                });
        };

        // Render step outcome data
        rows.each(function (rowData: StepNameHeader) {
            const tr = d3.select(this);

            const cellsRaw = stepOutcomeTable.getStepOutputTableEntries(encodeStepNameFromStepNameHeader(rowData));
            const cells = cellsRaw ? [...cellsRaw] : [];

            const dataCells = tr.selectAll("td.step-outcome-data")
                .data(cells)
                .join("td")
                .attr("class", "step-outcome-data");

            dataCells.style("text-align", "center").style("border-right", `1px solid ${modeColors.content}`);

            dataCells.selectAll("div")
                .data(d => [d])
                .join("div")
                .call(div => styleAndBindStepCell(div as d3.Selection<HTMLDivElement, TableEntry, any, any>))
                .style("height", "25px");
        });

    }, [stepOutcomeTable.tableEntries, getCellStyle]);

    const gridStyle: React.CSSProperties = {
        display: 'grid',
        gridTemplateRows: 'auto',
        gridTemplateColumns: '1fr auto',
    };

    // Binding to the reference of the StepOutcomeTableData is by design. Once we have performed the initial load, we want to snap to the right side.
    // After the user regains control and can horizontally scroll, we do *not* want to snap to right anymore.
    useEffect(() => {
        if (containerRef.current) {
            const container = containerRef.current;

            // We need to make sure the d3 table is fully drawn just so we get the padding included in the width.
            requestAnimationFrame(() => {
                container.scrollLeft = container.scrollWidth;
            });
        }
    }, [handler.StepOutcomeTableData]);

    const updateMaxHeight = () => {
        if (containerRef.current) {
            const containerTop = containerRef.current.getBoundingClientRect().top;
            const availableHeight = window.innerHeight - containerTop - 10;
            containerRef.current.style.maxHeight = `${availableHeight}px`;
        }
    };

    // Maximum height of container, reactive to zoom events.
    useEffect(() => {
        updateMaxHeight();

        window.addEventListener("resize", updateMaxHeight);

        return () => {
            window.removeEventListener("resize", updateMaxHeight);
        };
    }, []);

    return (
        <div style={gridStyle}>
            {/* Left side: table */}
            <div id="d3-container"
                ref={containerRef}
                style={{
                    overflow: 'auto'
                }}
            />
            {/* Right side: incremental spinner */}
            <IncrementalRefreshIndicatorPanel handler={handler} />
        </div>
    );
};

/**
 * React Component that represents the provided stepOutcomeTable. This is a D3 table.
 * @param stepOutcomeTable The data to base the table off of.
 * @param handler The data handler used to query step outcome data.
 * @param onCellSelected Cell selected callback.
 * @returns React Component.
 */
export const StepOutcomeD3Table: React.FC<{ stepOutcomeTable: StepOutcomeTable; handler: StepOutcomeDataHandler, onCellSelected: any; }> = ({ stepOutcomeTable, handler, onCellSelected }) => {
    return GenerateD3Table(stepOutcomeTable, handler, onCellSelected);
};
