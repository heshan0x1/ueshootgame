// Copyright Epic Games, Inc. All Rights Reserved.

import { Args } from '../common/args';

export class RoboArgs {

	private static _args: Args
	
	static Init(args: Args) {
		this._args = args
	}

	static getExclusiveLockOpenedsToRun() {
		return this._args.exclusiveLockOpenedsToRun
	}

	static useMongo() {
		return this._args.mongoDB_URI.length > 0
	}

	static get args() {
		return this._args
	}
}