// Copyright Epic Games, Inc. All Rights Reserved.

import * as d3 from "d3";
import { TestDataHandler, TestNameRef, MetadataRef, PhaseSessionResult, TestMetaStatus } from './testData';
import dashboard, { StatusColor } from 'horde/backend/Dashboard';
import { getHumanTime, getShortNiceTime } from 'horde/base/utilities/timeUtils';
import { getHordeStyling } from 'horde/styles/Styles';
import { TestOutcome } from "./api";
import { getPhaseSessionStatusColor, getStatusColors } from "./testAutomationCommon";

// Handle bad "@types/d3" types, fix if addressed upstream
const _d3 = d3 as any;

type SelectionType = d3.Selection<SVGGElement, unknown, null, undefined>;
type DivSelectionType = d3.Selection<HTMLDivElement, unknown, null, undefined>;

export class PhaseHistoryGraph {

    constructor(test: TestNameRef, phase: string, stream: string, handler: TestDataHandler, onClickCallback?: (session: PhaseSessionResult) => void) {
        this.test = test;
        this.phase = phase;

        this.handler = handler;
        this.onClickCallback = onClickCallback;

        this.status = handler.getStatusStream(stream)!.tests.get(test)!;

        this.margin = { top: 0, right: 32, bottom: 0, left: 32 };
    }

    initData(meta: MetadataRef) {
        this.meta = meta;
        this.sessions = this.status.sessions.get(meta)?.getPhaseSessions(this.phase) ?? [];
    }

    render(container: HTMLDivElement, meta: MetadataRef, selectedPhaseSessionId?: string, withoutHeader?: boolean) {

        const { modeColors } = getHordeStyling();

        this.clear(container);

        this.initData(meta);

        this.phaseSessionId = selectedPhaseSessionId;

        const handler = this.handler;
        const sessions = this.sessions;
        const width = 1000

        const scolors = getStatusColors();
        const colors = new Map<TestOutcome, string>([
            [TestOutcome.Success, scolors.get(StatusColor.Success)!],
            [TestOutcome.Failure, scolors.get(StatusColor.Failure)!],
            [TestOutcome.Unspecified, scolors.get(StatusColor.Unspecified)!],
            [TestOutcome.Skipped, scolors.get(StatusColor.Skipped)!]
        ]);

        const X = _d3.map(sessions, (r) => handler.commitIdDates.get(r.commitId)!.getTime() / 1000);
        const Y = _d3.map(sessions, (r) => 0);

        const xDomain = d3.extent(handler.commitIdDates.values() as any, (d:any) => d.getTime() / 1000);
        const yDomain = new _d3.InternSet(Y as any);

        const I = d3.range(X.length);

        const yPadding = 1;
        const height = withoutHeader? 20 : Math.ceil((yDomain.size + yPadding) * 13) + this.margin.top + this.margin.bottom;

        const xRange = [this.margin.left, width - this.margin.right];
        const yRange = [this.margin.top, height - this.margin.bottom];

        const xScale = _d3.scaleTime(xDomain, xRange);
        const yScale = _d3.scalePoint(yDomain, yRange).round(true).padding(yPadding);

        const svg = d3.select(container)
            .append("svg")
            .attr("width", width)
            .attr("height", height + 24)
            .attr("viewBox", [0, 0, width, height + 24] as any)

        // top axis
        const xAxis = (g: SelectionType) => {

            const dateMin = new Date(xDomain[0]! * 1000);
            const dateMax = new Date(xDomain[1]! * 1000);

            let ticks: number[] = [];
            for (const date of d3.timeDays(dateMin, dateMax, 1).reverse()) {
                ticks.push(date.getTime() / 1000);
            }

            if (ticks.length > 14) {
                let nticks = [...ticks];
                // remove first and last, will be readded 
                const first = nticks.shift()!;
                const last = nticks.pop()!;

                const n = Math.floor(nticks.length / 12);

                const rticks: number[] = [];
                for (let i = 0; i < nticks.length; i = i + n) {
                rticks.push(nticks[i]);
                }

                rticks.unshift(first);
                rticks.push(last);
                ticks = rticks;
            }

            g.attr("transform", `translate(0,16)`)
                .style("font-family", "Horde Open Sans Regular")
                .style("font-size", "9px")
                .call(d3.axisTop(xScale)
                    .tickValues(ticks)
                    .tickFormat(d => withoutHeader? "" : getHumanTime(new Date((d as number) * 1000)))
                    .tickSizeOuter(0))
                .call(g => g.select(".domain").remove())
                .call(g => g.selectAll(".tick line")
                    .attr("stroke-opacity", dashboard.darktheme ? 0.35 : 0.25).clone()
                    .attr("stroke", dashboard.darktheme ? "#6D6C6B" : "#4D4C4B")
                    .attr("y2", height - this.margin.bottom)
                )
        }

        svg.append("g").call(xAxis)

        const g = svg.append("g")
            .selectAll()
            .data(_d3.group(I, i => Y[i]))
            .join("g")
            .attr("class", "meta")
            .attr("id", ([y]: any) => `meta${y as any}`)
            .attr("transform", ([y]: any) => `translate(0,${(yScale(y) as any) + 16})`);

        g.append("line")
            .attr("stroke", dashboard.darktheme ? "#6D6C6B" : "#4D4C4B")
            .attr("stroke-width", 1)
            .attr("stroke-linecap", 4)
            .attr("stroke-opacity", dashboard.darktheme ? 0.35 : 0.25)
            .attr("x1", ([, I]: any) => this.margin.left)
            .attr("x2", ([, I]: any) => width - this.margin.right);

        g.selectAll("circle")
            .data(([, I]: any) => I)
            .enter()
            .append("g")
            .attr("id", i => `circle${sessions[i as any].id}`)
            .attr("transform", i => `translate(${xScale(X[i as any])},0)`)
            .append("g")
            .attr("class", "circleScale")
            .attr("id", i => `circleScale${sessions[i as any].id}`)
            .style("cursor", "pointer")
            .each((d, i, nodes) => SessionCircle(nodes[i], sessions[d as any], 5, !!this.phaseSessionId && sessions[d as any].id === this.phaseSessionId));

        const tooltip = this.tooltip = d3.select(container)
            .append("div")
            .attr("id", "tooltip")
            .style("display", "none")
            .style("background-color", modeColors.background)
            .style("border", "solid")
            .style("border-width", "1px")
            .style("border-radius", "3px")
            .style("border-color", dashboard.darktheme ? "#413F3D" : "#2D3F5F")
            .style("padding", "6px")
            .style("position", "absolute")
            .style("pointer-events", "none");

        const closestData = (x: number, y: number): PhaseSessionResult | undefined => {

            if (x < this.margin.left - 16) {
                return undefined;
            }

            y -= 16;

            const closest = sessions.reduce((best, session, i) => {
                const absy = Math.abs(yScale(Y[0]) - y);
                const sessionTime = handler.commitIdDates.get(session.commitId)!.getTime() / 1000;
                const absx = Math.abs(xScale(sessionTime) - x);

                const lengthSqr = absy * absy + absx * absx;

                if (lengthSqr < best.value) {
                    return { index: i, value: lengthSqr };
                }
                else {
                    return best;
                }
            }, { index: 0, value: Number.MAX_SAFE_INTEGER });

            if (closest) {
                return sessions[closest.index];
            }

            return undefined;

        }

        const handleMouseMove = (event: any) => {

            const mouseX = _d3.pointer(event)[0];
            const mouseY = _d3.pointer(event)[1];

            const closest = closestData(mouseX, mouseY);
            if (!closest) {
                handleMouseLeave(undefined);
                return;
            }

            if (this.tooltipSessionId !== closest.id) {
                this.tooltipSessionId = closest.id;
                const metaIndex = 0;

                svg.selectAll(".circleScale").attr("transform", `scale(1)`);
                svg.select(`#circleScale${closest.id}`).attr("transform", `scale(2)`);
                svg.select(`#circle${closest.id}`).raise();
                const date = getShortNiceTime(closest.start, true, true);
                let desc = "";
                desc += `${closest.outcome} <br/>`;
                desc += `on Commit ${closest.commitId} <br/>`;
                desc += `${date} <br/>`;

                const timeStamp = handler.commitIdDates.get(closest.commitId)!.getTime() / 1000;
                const tx = xScale(timeStamp);
                const ty = yScale(metaIndex as any)!

                this.updateTooltip(true, tx, ty, desc);
            }
        }

        const handleMouseLeave = (event: any) => {
            svg.selectAll(".circleScale").attr("transform", `scale(1)`);
            tooltip.style("display", "none");
            this.tooltipSessionId = undefined;
        }

        const handleMouseClick = (event: any) => {

            const mouseX = _d3.pointer(event)[0];
            const mouseY = _d3.pointer(event)[1];

            const closest = closestData(mouseX, mouseY);
            if (closest && this.onClickCallback) {
                this.onClickCallback(closest);
            }
        }

        svg.on("mousemove", (event) => handleMouseMove(event))
        svg.on("mouseleave", (event) => handleMouseLeave(event))
        svg.on("click", (event) => handleMouseClick(event))

    }

    updateTooltip(show: boolean, x?: number, y?: number, html?: string) {
        if (!this.tooltip) {
            return;
        }

        x = x ?? 0;
        y = y ?? 0;

        this.tooltip
            .style("display", "block")
            .html(html ?? "")
            .style("position", `absolute`)
            .style("width", `max-content`)
            .style("top", `${y}px`)
            .style("left", `${x}px`)
            .style("transform", "translate(10%, 52%)")
            .style("font-family", "Horde Open Sans Semibold")
            .style("font-size", "10px")
            .style("line-height", "16px")
            .style("shapeRendering", "crispEdges")
            .style("stroke", "none")
            .style("z-index", 10000)

    }

    clear(container: HTMLDivElement) {
        d3.select(container).selectAll("*").remove();
    }

    // refs
    test: TestNameRef;
    meta: MetadataRef;
    phase: string;
    phaseSessionId?: string;

    onClickCallback?: (session: PhaseSessionResult) => void;

    handler: TestDataHandler;
    margin: { top: number, right: number, bottom: number, left: number }

    hasRendered = false;

    status: TestMetaStatus;
    sessions: PhaseSessionResult[] = [];

    tooltip?: DivSelectionType;
    tooltipSessionId?: string;
}

const SessionCircle = (container: SVGGElement, session: PhaseSessionResult, radius: number, selected?: boolean) => {
    const color = getPhaseSessionStatusColor(session);
    const g = d3.select(container);
    g.append("circle")
        .attr("fill", color as any)
        .attr("r", radius);

    if (selected) {
        g.append("circle")
            .attr("stroke", dashboard.darktheme ? "#c1c4c9" : "#3d4654 ")
            .attr("fill", "transparent")
            .attr("stroke-width", 2)
            .attr("r", radius + 3);
    }

    return g;
}
