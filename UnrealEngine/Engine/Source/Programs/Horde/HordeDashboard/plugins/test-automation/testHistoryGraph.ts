// Copyright Epic Games, Inc. All Rights Reserved.

import * as d3 from "d3";
import { TestDataHandler, TestNameRef, MetadataRef, TestMetaStatus, TestSessionResult } from './testData';
import dashboard, { StatusColor } from 'horde/backend/Dashboard';
import { getHumanTime, getShortNiceTime, msecToElapsed } from 'horde/base/utilities/timeUtils';
import { getHordeStyling } from 'horde/styles/Styles';
import { getStatusColors, SessionValues } from "./testAutomationCommon";
import { renderToString } from "react-dom/server"
import { TestOutcome } from "./api";

// Handle bad "@types/d3" types, fix if addressed upstream
const _d3 = d3 as any;

type SelectionType = d3.Selection<SVGGElement, unknown, null, undefined>;
type DivSelectionType = d3.Selection<HTMLDivElement, unknown, null, undefined>;

const clampMin = (x: number) => x !== 0 && x <= 0.3? 0.3: x;

export class TestHistoryGraph {

    constructor(test: TestNameRef, stream: string, handler: TestDataHandler, commonMetaKeys?: string[], onClickCallback?: (session: TestSessionResult) => void) {
        this.test = test;

        this.handler = handler;
        this.onClickCallback = onClickCallback;
        this.commonMetaKeys = commonMetaKeys;

        this.status = handler.getStatusStream(stream)!.tests.get(test)!;

        this.margin = { top: 0, right: 32, bottom: 0, left: 160 };
        this.clipId = `test_history_clip_path_${test.key}_${stream}`;
    }

    initData(selectedMetaKeys?: string[]) {
        this.metaRefs = this.handler.filteredMetadata.filter((m) => this.status.includesMetadata(m) && (!selectedMetaKeys || selectedMetaKeys.includes(m.id)));
        const commonMetaKeys = this.commonMetaKeys = this.commonMetaKeys ?? MetadataRef.identifyCommonKeys(this.metaRefs);

        this.sessions = [];
        this.metaInfo = new Map();

        this.metaRefs.forEach((meta, index) => {
            const metaLastSession = this.status.getLastSession(meta);
            this.metaStatus.set(meta, !metaLastSession? TestOutcome.Unspecified : metaLastSession.outcome);

            const elements: string[] = meta.getValuesExcept(commonMetaKeys);
            this.metaNames.set(meta, elements.join(" / "));

            this.sessions.push(...this.status.sessions.get(meta)!.history);
            this.metaInfo.set(meta.id, {y: 0 , index: index});
        });
    }

    render(container: HTMLDivElement, forceRender?: boolean, selectedMetaKeys?: string[]) {

        if (this.hasRendered && !forceRender) {
            return;
        }

        const { modeColors } = getHordeStyling();

        this.clear(container);

        this.hasRendered = true;

        this.initData(selectedMetaKeys);

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
        const Y = _d3.map(sessions, (r) => this.metaInfo.get(r.metadataId)!.index);

        const xDomain = d3.extent(handler.commitIdDates.values() as any, (d:any) => d.getTime() / 1000);
        const yDomain = new _d3.InternSet(Y as any);

        const I = d3.range(X.length);

        const yPadding = 1;
        const height = Math.ceil((yDomain.size + yPadding) * 16) + this.margin.top + this.margin.bottom;

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
                .tickFormat(d => getHumanTime(new Date((d as number) * 1000)))
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
            .attr("stroke", (y: any) => {
                const status = this.metaStatus.get(this.metaRefs[y])!;
                if (status === TestOutcome.Failure) {
                    return colors[this.metaStatus.get(this.metaRefs[y])!]
                }
                return dashboard.darktheme ? "#6D6C6B" : "#4D4C4B";
            })
            .attr("stroke-width", 1)
            .attr("stroke-linecap", 4)
            .attr("stroke-opacity", dashboard.darktheme ? 0.35 : 0.25)
            .attr("x1", ([, I]: any) => this.margin.left)
            .attr("x2", ([, I]: any) => width - this.margin.right);


        g.append("text")
            .attr("text-anchor", "start")
            .style("alignment-baseline", "left")
            .style("font-family", "Horde Open Sans Regular")
            .style("font-size", 10)
            .attr("dy", "0.15em") // center stream name
            .attr("x", () => 0)
            .attr("fill", ([y]: any) => {
                const status = this.metaStatus.get(this.metaRefs[y])!;
                if (!dashboard.darktheme && status === TestOutcome.Failure) {
                    return colors[this.metaStatus.get(this.metaRefs[y])!]
                }
                return dashboard.darktheme ? "#E0E0E0" : "#2D3F5F";
            })
            .text(([y]: any) => this.metaNames.get(this.metaRefs[y])!);

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
            .each((d, i, nodes) => SessionConcentricCircle(nodes[i], sessions[d as any], 5));

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

        // populate metaInfo y value
        this.metaInfo.forEach(meta => meta.y = yScale(meta.index as any)!)

        const closestData = (x: number, y: number): TestSessionResult | undefined => {

            if (x < this.margin.left - 16) {
                return undefined;
            }

            y -= 16;

            const closest = sessions.reduce((best, session, i) => {
                const absy = Math.abs(this.metaInfo.get(session.metadataId)!.y - y);
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
                const metaIndex = this.metaInfo.get(closest.metadataId)!.index;

                svg.selectAll(".circleScale").attr("transform", `scale(1)`);
                svg.select(`#circleScale${closest.id}`).attr("transform", `scale(2.5)`);
                svg.select(`#circle${closest.id}`).raise();
                svg.select(`#meta${metaIndex}`).raise();

                const cmeta = this.metaRefs[metaIndex].getValuesExcept(this.commonMetaKeys!).join(" / ");
                const date = getShortNiceTime(closest.start, true, true);
                let desc = "";
                if (!!cmeta) desc += `# ${cmeta} <br/>`;
                desc += `Commit ${closest.commitId} <br/>`;
                desc += `Duration ${msecToElapsed(closest.duration * 1000, true, true)} <br/>`;
                desc += `${date} <br/>`;
                if (closest.phasesSucceededCount + closest.phasesFailedCount + closest.phasesUnspecifiedCount > 1)
                    desc += renderToString(SessionValues(closest, {fontSize: 9}));

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
            .style("transform", "translate(-108%, -97%)")
            .style("font-family", "Horde Open Sans Semibold")
            .style("font-size", "10px")
            .style("line-height", "16px")
            .style("shapeRendering", "crispEdges")
            .style("stroke", "none")

    }

    clear(container: HTMLDivElement) {
        d3.select(container).selectAll("*").remove();
    }

    // test ref
    test: TestNameRef;

    commonMetaKeys?: string[];
    onClickCallback?: (session: TestSessionResult) => void;

    handler: TestDataHandler;
    margin: { top: number, right: number, bottom: number, left: number }

    hasRendered = false;

    status: TestMetaStatus;
    metaRefs: MetadataRef[] = [];
    metaInfo: Map<string, {y: number, index: number}> = new Map();
    sessions: TestSessionResult[] = [];
    // metaRef => status string
    metaStatus: Map<MetadataRef, string> = new Map();
    // metaRef => meta string
    metaNames: Map<MetadataRef, string> = new Map();

    clipId: string;

    tooltip?: DivSelectionType;
    tooltipSessionId?: string;
}

type ItemStack = {value: number, name: string, color: string, title?: boolean};

const PieChart = (container: SVGGElement, data: ItemStack[], radius: number, innerRadius: number = 0, padAngle: number = 0) => {
    const pie = d3.pie<ItemStack>().padAngle(padAngle).value((d) => d.value).sort(null);
    const arc = d3.arc().innerRadius(innerRadius).outerRadius(radius);
    const color = d3.scaleOrdinal()
        .domain(data.map(d => d.name))
        .range(data.map(d => d.color));
    
    const g = d3.select(container)
        .selectAll("pie")
        .data(pie(data))
        .join("path")
            .attr("class", "pie")
            .attr("fill", d => color(d.data.name) as any)
            .attr("d", arc as any)
        .append("title")
            .text(d => d.data.title? `${Math.ceil(d.data.value)}% ${d.data.name}` : "");

    return g;
}

const SessionPieChart = (container: SVGGElement, session: TestSessionResult, radius: number, innerRadius:number = 0, padAngle: number = 0) => {

    const sessionTotalCount = session.phasesSucceededCount + session.phasesFailedCount + session.phasesUnspecifiedCount;
    const metaFailedFactor = Math.ceil(session.phasesFailedCount / (sessionTotalCount || 1) * 10) / 10;
    const metaUnspecifiedFactor = Math.ceil(session.phasesUnspecifiedCount / (sessionTotalCount || 1) * 10) / 10;

    const statusColors = getStatusColors();

    const stack: ItemStack[] = [
        {
            value: metaUnspecifiedFactor * 100,
            name: "Unspecified",
            color: statusColors.get(StatusColor.Unspecified)!,
        },
        {
            value: metaFailedFactor * 100,
            name: "Failure",
            color: statusColors.get(StatusColor.Failure)!,
        },
        {
            value: (1 - (metaFailedFactor + metaUnspecifiedFactor)) * 100,
            name: "Passed",
            color: statusColors.get(StatusColor.Success)!
        }
    ];

    return PieChart(container, stack, radius, innerRadius, padAngle);
}

const ConcentricCircle = (container: SVGGElement, data: ItemStack[], radius: number) => {
    const color = d3.scaleOrdinal()
        .domain(data.map(d => d.name))
        .range(data.map(d => d.color));
    
    const g = d3.select(container)
        .selectAll("centric")
        .data(data)
        .join("circle")
            .attr("class", "centric")
            .attr("fill", d => color(d.name) as any)
            .attr("r", d => radius * d.value)
        .append("title")
            .text(d => d.title? `${Math.ceil(d.value)}% ${d.name}` : "");

    return g;
}

const SessionConcentricCircle = (container: SVGGElement, session: TestSessionResult, radius: number) => {

    const sessionTotalCount = session.phasesSucceededCount + session.phasesFailedCount + session.phasesUnspecifiedCount;
    const metaFailedFactor = Math.ceil(clampMin(session.phasesFailedCount / (sessionTotalCount || 1)) * 5) / 5;
    const metaUnspecifiedFactor = Math.ceil(clampMin(session.phasesUnspecifiedCount / (sessionTotalCount || 1)) * 5) / 5;
    let metaSuccessFactor = 1 - (metaFailedFactor + metaUnspecifiedFactor);
    metaSuccessFactor = metaSuccessFactor < 0? 0 : metaSuccessFactor

    const statusColors = getStatusColors();

    const stack: ItemStack[] = [
        {
            value: metaSuccessFactor && 1,
            name: "Passed",
            color: statusColors.get(StatusColor.Success)!
        },
        {
            value: metaUnspecifiedFactor && ((metaSuccessFactor && (metaFailedFactor + metaUnspecifiedFactor)) || 1),
            name: "Unspecified",
            color: statusColors.get(StatusColor.Unspecified)!,
        },
        {
            value: metaFailedFactor,
            name: "Failure",
            color: statusColors.get(StatusColor.Failure)!,
        },
    ];

    return ConcentricCircle(container, stack, radius);
}
