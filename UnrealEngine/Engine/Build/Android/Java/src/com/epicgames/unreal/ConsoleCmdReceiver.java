/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.epicgames.unreal;

import java.util.Objects;

import android.annotation.SuppressLint;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;

import androidx.annotation.NonNull;

public class ConsoleCmdReceiver extends BroadcastReceiver {

	private GameActivity gameActivity;

	// Constructor...
	public ConsoleCmdReceiver(GameActivity InGameActivity) {
		gameActivity = InGameActivity;
	}

	@Override
	public void onReceive(Context context, Intent intent) {
		// example usage
		// adb shell "am broadcast -a android.intent.action.RUN -e cmd 'stat fps'"
		String action = intent.getAction();
		if (Objects.equals(action, Intent.ACTION_RUN)) {
			String cmd = intent.getStringExtra("cmd");
			if (cmd != null) {
				gameActivity.nativeConsoleCommand(cmd);
			}
		}
	}

	/**
	 * Registers this to the given context using Unreal Engine conventions.  In the current version, that implies receiving the RUN action publicly.
	 * You need to call `contextWrapper.unregisterReceiver(mConsoleCmdReceiver)` when the context is torn down.
	 * @param contextWrapper The context to which this will be registered.
	 */
	public void register(@NonNull ContextWrapper contextWrapper) {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
			contextWrapper.registerReceiver(this, new IntentFilter(Intent.ACTION_RUN), Context.RECEIVER_EXPORTED);
		} else {
			compatRegister(contextWrapper);
		}
	}

	@SuppressLint("UnspecifiedRegisterReceiverFlag")
	private void compatRegister(@NonNull ContextWrapper contextWrapper) {
		contextWrapper.registerReceiver(this, new IntentFilter(Intent.ACTION_RUN));
	}
}
