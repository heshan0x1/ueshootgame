// Copyright Epic Games, Inc. All Rights Reserved.
import { FunctionalTest, P4Util, Stream } from '../framework'

const streams: Stream[] = [
	{name: 'Main', streamType: 'mainline'},
	{name: 'Dev-Pootle', streamType: 'development', parent: 'Main'},
	{name: 'Dev-Perkin', streamType: 'development', parent: 'Main'},
	{name: 'Dev-Posie', streamType: 'development', parent: 'Main'}
]

export class TestEdgeBlockAssetsReconsider extends FunctionalTest {

	async setup() {
		await this.p4.depot('stream', this.depotSpec())
		await this.createStreamsAndWorkspaces(streams)

		await P4Util.addFileAndSubmit(this.getClient('Main'), 'test.uasset', 'Initial content')

		const desc = 'Initial branch of files from Main'
		await Promise.all([
			this.p4.populate(this.getStreamPath('Dev-Perkin'), desc),
			this.p4.populate(this.getStreamPath('Dev-Pootle'), desc),
			this.p4.populate(this.getStreamPath('Dev-Posie'), desc)
		])
	}

	async run() {
		// making edit in set-up, i.e. before added to RoboMerge
		const revisionCl = await P4Util.editFileAndSubmit(this.getClient('Main'), 'test.uasset', 'Initial content\n\nFirst addition')

        return Promise.all([
			this.reconsider('Main', revisionCl, 'Dev-Perkin'),
			this.reconsider('Main', revisionCl, 'Dev-Posie', `#robomerge ${this.fullBranchName('Dev-Posie')} | #robomerge #disregardAssetBlock`)
		])
	}

	verify() {
		return Promise.all([
			this.checkHeadRevision('Main', 'test.uasset', 2),
			this.checkHeadRevision('Dev-Pootle', 'test.uasset', 2),
			this.checkHeadRevision('Dev-Perkin', 'test.uasset', 1),
			this.checkHeadRevision('Dev-Posie', 'test.uasset', 2)
		])
	}

	getBranches() {
		const mainDef = this.makeBranchDef('Main', ['Dev-Perkin', 'Dev-Pootle', 'Dev-Posie'])
        mainDef.forceFlowTo = [this.fullBranchName('Dev-Pootle')]
		mainDef.blockAssetReconsider = [this.fullBranchName('Dev-Perkin'), this.fullBranchName('Dev-Pootle'), this.fullBranchName('Dev-Posie')]
		return [
			mainDef,
			this.makeBranchDef('Dev-Perkin', []),
			this.makeBranchDef('Dev-Pootle', []),
			this.makeBranchDef('Dev-Posie', [])
        ]
	}

}
