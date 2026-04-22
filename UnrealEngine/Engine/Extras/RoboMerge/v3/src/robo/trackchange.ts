// Copyright Epic Games, Inc. All Rights Reserved.

import { ContextualLogger } from '../common/logger';
import { DescribeResult, isExecP4Error, PerforceContext } from '../common/perforce';
import { Node } from './graph';
import { NodeBotInterface } from './bot-interfaces';
import { RoboMerge } from './robo';
import { Status } from './status';

const RobomergeMethodStrings = ['initialSubmit','merge_with_conflict','automerge','transfer'] as const;
type RobomergeMethods = typeof RobomergeMethodStrings[number];
type MergeMethod = RobomergeMethods|'populate'|'manual_merge'

interface TrackFilters {
    streams?: string
    bots?: string
    depots?: string
}

interface TrackedCL {
    cl: number
    serverId?: string
}

function compareTrackedCL(a: TrackedCL, b: TrackedCL) {
    return a.cl == b.cl && PerforceContext.sameServerID(a.serverId, b.serverId)
}

interface TrackedChange {
    desc: DescribeResult
    mergeMethod: MergeMethod
    node?: Node
    sourceCL?: TrackedCL
    destCLs: TrackedCL[]
}

interface TrackChangeResults {
    originalCL: TrackedCL,
    swarmURL?: string,
    changes: any
}

interface GatherOptions {
    parentCL?: TrackedCL,
    lastCL?: TrackedCL,
    initialSubmit?: boolean
    inSourceChain?: boolean
}

export async function trackChange(robo: RoboMerge, logger: ContextualLogger, initialCL: TrackedCL, userTags: Set<string>, filters: TrackFilters) {

    const streamFilter = (() => {
        return (filters.streams ? filters.streams.toUpperCase().replace('*','.*').split(',').map(s => new RegExp(s)) : [])
    })() 
    const botFilter = (() => { 
        return (filters.bots ? filters.bots.toUpperCase().split(',') : [])
    })()
    const depotFilter = (() => { 
        return (filters.depots ? filters.depots.toUpperCase().split(',').map(depot => `//${depot}`) : [])
    })()

    const graph = robo.graph.graph
    let data: TrackChangeResults = {originalCL: initialCL, swarmURL: PerforceContext.getServerContext(robo.roboMergeLogger).swarmURL, changes: {}}

    const getStreamFromPath = (path: string) => {
        const match = path.match(/\/\/[^\/]*\/[^\/]*/)
        return (match ? match[0] : null)
    }

    const getNode = (desc: DescribeResult) => {
        if (desc.path) {
            const stream = getStreamFromPath(desc.path)
            if (stream) {
                try {
                    return graph.findNodeForStream(stream, desc.path)
                }
                catch {						
                }
            }
        }
        return undefined
    }

    const getMergeMethod = (desc: string, cl: TrackedCL, opts?: GatherOptions): MergeMethod => {
        if (opts?.parentCL && !PerforceContext.sameServerID(cl.serverId, opts.parentCL.serverId)) {
            return 'transfer'
        } else if (desc.includes("#ROBOMERGE-CONFLICT")) {
            return 'merge_with_conflict'
        } else if (desc.includes("#ROBOMERGE-SOURCE")) {
            return 'automerge'
        } else if (opts?.initialSubmit) {
            return 'initialSubmit'
        } else if (desc.includes("Populate")) {
            return 'populate'
        } else {
            return 'manual_merge'
        }
    }

    const getMoreDescEntries = async (p4: PerforceContext, clToConsider: TrackedCL, desc: DescribeResult) => {
        // If the path is a root directory try and get a sampling 
        // of files, otherwise just get an additional block to evaluate
        if (desc.path.endsWith("/...")) {
            const rootDir = `${desc.path.slice(0,-3)}*@=${clToConsider.cl}`
            const FILES_PER_DIR = 5
            const DIRS_PER_SLICE = 5
            const filesPromises = [p4.files(rootDir, FILES_PER_DIR)]
            const dirs = await p4.dirs(rootDir)
            let fpIndex = 0
            let dirSliceBegin = 0
            while (true) {
                filesPromises.push(...dirs.slice(dirSliceBegin, dirSliceBegin+DIRS_PER_SLICE).map(dir => p4.files(`${dir}/...@=${clToConsider.cl}`, FILES_PER_DIR)))
                while (fpIndex < filesPromises.length) {
                    desc.entries.push(...(await filesPromises[fpIndex]))
                    fpIndex++
                }
                desc.entries = desc.entries.filter(e => e.action != 'move/delete')
                dirSliceBegin += DIRS_PER_SLICE
                if (desc.entries.length > 1 || dirSliceBegin >= dirs.length) {
                    break
                }
            }
        }
        else {
            desc.entries = (await p4.describe(clToConsider.cl, 100)).entries
        }
    }

    class Changes {
        private changes = new Map<string, TrackedChange>()

        static changeKey(cl: TrackedCL) {
            return JSON.stringify({cl: `${cl.cl}`, serverId: cl.serverId || 'default'})
        }

        public get(cl: TrackedCL) {
            return this.changes.get(Changes.changeKey(cl))
        }

        public set(cl: TrackedCL, tc: TrackedChange) {
            this.changes.set(Changes.changeKey(cl), tc)

        }
    }
    const changes = new Changes

    let determineSourceServer = (node: Node) => {
        let edges = graph.getEdgesForNode(node)
        const possibleServers = new Set([...edges].filter(e => e.target.stream === node.stream).map(e => e.source.serverID))
        if (possibleServers.size == 1) {
            return [...possibleServers][0]
        } 
        else {
            // TODO: figure out which one it is
        }
        return undefined
    }

    let sourceChain: TrackedCL[] = []
    let gatherCLInfo = async (cl: TrackedCL, opts?: GatherOptions) => {

        if (cl.serverId === undefined && opts?.lastCL) {
            const lastChange = changes.get(opts.lastCL)
            if (lastChange?.node) {
                cl.serverId = determineSourceServer(lastChange.node)
            }
        }

        let p4 = PerforceContext.getServerContext(robo.roboMergeLogger, cl.serverId)
        try {
            let desc = await p4.describe(cl.cl, 1)
            const clNode = getNode(desc)
            let initialSubmit = false
            if (!opts?.parentCL) {
                //TODO: abstract this in to a description parser per stream/branch/server
                const sourceMatch = desc.description.match(/#ROBOMERGE-SOURCE: (?<RMSource>.*)\n|Merge-CL: (?<MergeCL>\d*)\n/)
                if (sourceMatch?.groups!.RMSource) {
                    const newSources: TrackedCL[] = Array.from(sourceMatch.groups.RMSource.matchAll(/CL (\d+)/g)).map(m => {return {cl: parseInt(m[1])}}).reverse()
                    // TODO: this only solves the first step, if the server transfer is 2nd or further back then this will still fail to properly identify it
                    if (clNode) {
                        newSources[0].serverId = determineSourceServer(clNode)
                    }
                    sourceChain.push(...newSources)
                    opts = { ...opts, parentCL: newSources[0] }
                }
                else if (sourceMatch?.groups!.MergeCL) {
                    const parentCL = {
                        cl: parseInt(sourceMatch.groups.MergeCL),
                        serverId: clNode ? determineSourceServer(clNode) : undefined
                    }
                    opts = { ...opts, parentCL}
                    sourceChain.push(parentCL)
                }
                else {
                    for (const entry of desc.entries) {
                        // integrated for move/delete will point at the paired move/add not where it was merged to in another stream, so not useful to evaluate it
                        if (entry.action != 'move/delete') {
                            // Do an integrated check
                            const integrated = (await p4.integrated(null, entry.depotFile))
                                                        .filter(   x => x.how.includes('from') 
                                                                && x.toFile == entry.depotFile 
                                                                && x.endToRev == `#${entry.rev}` 
                                                                && getStreamFromPath(x.toFile) != getStreamFromPath(x.fromFile))
                            if (integrated.length > 0) {
                                const fstat = await p4.fstat(null, `${integrated[0].fromFile}${integrated[0].endFromRev}`)
                                opts = { ...opts, parentCL: {cl: fstat[0].headChange, serverId: p4.serverID } }
                                sourceChain.push(opts.parentCL!)
                                break
                            }
                        }
                        if (desc.entries.length == 1) {
                            await getMoreDescEntries(p4, cl, desc)
                        }
                    }
                }
                initialSubmit = opts?.parentCL === undefined
            }
            const mergeMethod = getMergeMethod(desc.description, cl, {...opts, initialSubmit} )
            if (clNode || opts?.inSourceChain || (RobomergeMethodStrings as readonly string[]).includes(mergeMethod)) {
                changes.set(cl, {desc, mergeMethod, node: clNode, sourceCL: opts?.parentCL, destCLs: (opts && opts.lastCL ? [opts.lastCL] : []) })
            }
        } 
        catch (reason) {
            if (!isExecP4Error(reason)) {
                throw reason
            }

            let [_, output] = reason
            // try to be resilient to a missing change
            if (output.includes("no such changelist.")) {
                return false
            }
            throw reason
        }
        return true
    }

    if (!(await gatherCLInfo(initialCL, {initialSubmit: true}))) {
        return {success: false, message: `Unable to lookup CL# ${initialCL.cl}`}
    }

    let lastCL = initialCL
    let originCL = initialCL
    for (let i=0; i < sourceChain.length; i++) {
        const sourceCL = sourceChain[i]
        const parentCL = sourceChain[i+1]
        if (await gatherCLInfo(sourceCL, {parentCL, lastCL, inSourceChain: true})) {
            if (i == 0) {
                let initialChange = changes.get(initialCL)!
                // Fix up the initial changes merge method and source now that we know it is not the initial submit
                initialChange.mergeMethod = getMergeMethod(initialChange.desc.description, initialCL, {parentCL: sourceCL})
                initialChange.sourceCL = sourceCL
            }
            originCL = sourceCL
        }
        lastCL = sourceCL
    }
    // We've now determined the earliest CL we are going to start tracking from
    data.originalCL = originCL

    const isFTE = userTags.has("fte")

    let changePromises: Promise<void>[] = []
    let clsToConsider: TrackedCL[] = [data.originalCL]

    const considerCL = async (clToConsider: TrackedCL) => {
        let changeToConsider = changes.get(clToConsider)
        if (!changeToConsider) {
            return
        }

        const externalServerTargets: Node[] = []
        let includeInResults = false
        let hasAutomergeTarget = false
        let streamDisplayName = ""
        if (changeToConsider.node) {
            let edges = graph.getEdgesForNode(changeToConsider.node)
            for (let edge of edges) {
                if (!includeInResults) {
                    const bot = edge.sourceAnnotation as NodeBotInterface
                    if (botFilter.length == 0 || botFilter.includes(bot.branchGraph.botname)) {
                        if (Status.includeBranch(bot.branchGraph.config.visibility, userTags, logger)) {
                            streamDisplayName = changeToConsider.node.stream
                            includeInResults = true
                        }
                    }
                }
                if (!hasAutomergeTarget && edge.flags.has('automatic')) {
                    if (typeof changeToConsider.node === 'string') {
                        hasAutomergeTarget = edge.source.stream == changeToConsider.node
                    }
                    else {
                        hasAutomergeTarget = edge.source.stream == changeToConsider.node.stream
                    }
                }
                if (!PerforceContext.sameServerID(clToConsider.serverId, edge.target.serverID)) {
                    externalServerTargets.push(edge.target)
                }
            }
        } else if (isFTE) {
            hasAutomergeTarget = true // we can't know this from the graph, but it doesn't hurt (much) to look
            if (botFilter.length == 0) {
                includeInResults = true
                if (changeToConsider.desc.path) {
                    streamDisplayName = getStreamFromPath(changeToConsider.desc.path) || changeToConsider.desc.path
                } else if (changeToConsider.desc.entries.length > 0) {
                    streamDisplayName = getStreamFromPath(changeToConsider.desc.entries[0].depotFile) || streamDisplayName
                }
            }
        }

        if (streamDisplayName.length == 0) {
            streamDisplayName = "//****/****"
        }

        if (includeInResults)
        {
            const upperSteamDisplayName = streamDisplayName.toUpperCase()
            includeInResults = depotFilter.length == 0 || depotFilter.some(depot => upperSteamDisplayName.startsWith(depot))
            includeInResults = includeInResults && (streamFilter.length == 0 || streamFilter.some(re => upperSteamDisplayName.match(re)))
        }

        const p4 = PerforceContext.getServerContext(logger, clToConsider.serverId)
        let runIntegrated = true
        for (let i=0; i < changeToConsider.desc.entries.length; i++) {
            const entry = changeToConsider.desc.entries[i]
            // integrated for move/delete will point at the paired move/add not where it was merged to in another stream, so not useful to evaluate it
            if (entry.action != 'move/delete') {
                for (let i=externalServerTargets.length-1; i>=0; i--) {
                    const externalTarget = externalServerTargets[i]
                    const externalP4 = PerforceContext.getServerContext(logger, externalTarget.serverID)
                    const streamName = getStreamFromPath(changeToConsider.node!.stream)
                    const targetPath = entry.depotFile.replace(streamName || changeToConsider.node!.stream, externalTarget.stream)
                    const changes = await externalP4.changes(targetPath, 0)
                    for (let change of changes) {
                        if (change.desc.includes(clToConsider.cl.toString())) {
                            const newChangeToConsider = {cl: change.change, serverId: externalTarget.serverID}
                            changeToConsider.destCLs.push(newChangeToConsider)
                            await gatherCLInfo(newChangeToConsider, {parentCL: clToConsider})
                            externalServerTargets.splice(i, 1)
                            break
                        }
                    }
                }

                if (runIntegrated) {
                    const integrated = await p4.integrated(null, entry.depotFile, {intoOnly: true, startCL: clToConsider.cl})
                    if (integrated.length > 0) {
                        for (let integ of integrated) {
                            const startToRev = integ.startToRev == "#none" ? 0 : parseInt(integ.startToRev.slice(1))
                            const endToRev = parseInt(integ.endToRev.slice(1))
                            if (entry.rev <= endToRev && entry.rev > startToRev)
                            {
                                runIntegrated = false
                                const newChangeToConsider = {cl: integ.change, serverId: clToConsider.serverId}
                                const destChange = changes.get(newChangeToConsider)
                                if (destChange) {
                                    const newChangeKey = Changes.changeKey(newChangeToConsider)
                                    if (!changeToConsider.destCLs.some(cl => Changes.changeKey(cl) === newChangeKey)) {
                                        changeToConsider.destCLs.push(newChangeToConsider)
                                    }
                                    destChange.sourceCL = clToConsider
                                } else {
                                    changeToConsider.destCLs.push(newChangeToConsider)
                                    await gatherCLInfo(newChangeToConsider, {parentCL: clToConsider})
                                }
                            }
                        }
                    }
                }

                if (!runIntegrated && externalServerTargets.length > 0) {
                    break
                }
            }
            if (hasAutomergeTarget && changeToConsider.desc.entries.length == 1 && 
                        (RobomergeMethodStrings as readonly string[]).includes(changeToConsider.mergeMethod)) {
                // If we only have 1 entry and we didn't get integration info off of it
                // and the graph suggests we are expecting there could be other changes
                // get more of the files. 
                await getMoreDescEntries(p4, clToConsider, changeToConsider.desc)
            }
        }
        for (const destCL of changeToConsider.destCLs) {
            if (!clsToConsider.find(cl => compareTrackedCL(cl, destCL))) {
                clsToConsider.push(destCL)
                changePromises.push(considerCL(destCL))
            }
        }

        if (includeInResults) {
            const swarmLink = p4.swarmURL ? `${p4.swarmURL}/changes/${clToConsider.cl}` : undefined
            data.changes[`${clToConsider.cl}`] = {streamDisplayName, swarmLink, mergeMethod: changeToConsider.mergeMethod, sourceCL: changeToConsider.sourceCL?.cl}
        }
    }

    changePromises.push(considerCL(clsToConsider[0]))
    for (const cp of changePromises) {
        await cp
    }

    return {success: true, data}
}