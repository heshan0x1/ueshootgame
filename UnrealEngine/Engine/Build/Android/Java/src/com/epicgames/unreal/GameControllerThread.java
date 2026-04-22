// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.unreal;

import android.hardware.input.InputManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.lang.Thread;

public class GameControllerThread extends Thread implements InputManager.InputDeviceListener
{
    private GameControllerManager mGameControllerManager;
    private boolean activeInputDeviceListener = false;
    private Handler mHandler;

    public void init(GameControllerManager gcManager)
    {
        mGameControllerManager = gcManager;
    }

    @Override
    public void run ()
    {
        Looper.prepare();
        mHandler = new Handler(Looper.myLooper());
        startListening();
        Looper.loop();
    }

    public void startListening()
    {
        if (!activeInputDeviceListener)
        {
            mGameControllerManager.getAppInputManager().registerInputDeviceListener(this, mHandler);
            activeInputDeviceListener = true;
        }
    }

    public void stopListening()
    {
        if (activeInputDeviceListener)
        {
            mGameControllerManager.getAppInputManager().unregisterInputDeviceListener(this);
            activeInputDeviceListener = false;
        }
    }

    @Override
    public void onInputDeviceAdded(int deviceId)
    {
        mGameControllerManager.onInputDeviceAdded(deviceId);
    }

    @Override
    public void onInputDeviceRemoved(int deviceId)
    {
        mGameControllerManager.onInputDeviceRemoved(deviceId);
    }

    @Override
    public void onInputDeviceChanged(int deviceId)
    {
        mGameControllerManager.onInputDeviceChanged(deviceId);
    }
}
