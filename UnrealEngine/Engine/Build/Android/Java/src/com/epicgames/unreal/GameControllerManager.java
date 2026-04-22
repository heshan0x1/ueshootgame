// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.unreal;

import android.content.Context;
import android.hardware.input.InputManager;
import android.util.Log;
import android.view.InputDevice;
import android.view.MotionEvent;

import java.util.ArrayList;
import java.util.List;

public class GameControllerManager
{
    private static final String FINGERPRINT_DEVICE_NAME = "uinput-fpc";
    private static final int GAMECONTROLLER_SOURCE_MASK = InputDevice.SOURCE_DPAD | InputDevice.SOURCE_JOYSTICK | InputDevice.SOURCE_GAMEPAD;

    private final InputManager inputManager;
	private static Logger Log = new Logger("UE", "GameControllerManager");;

	private GameControllerThread gameControllerThread;

	private ArrayList<Integer> listDeviceId = new ArrayList<Integer>();

	enum InputDeviceType
	{
		Unknown,
		TouchScreen,
		GameController,
	}

	enum InputDeviceState
	{
		Added,
		Removed,
		Changed,
	}

    public GameControllerManager(InputManager inputMgr)
    {
        inputManager = inputMgr;
    }
    
	public InputManager getAppInputManager()
    {
        return inputManager;
    }

    public void scanDevices()
    {
        int[] deviceIds = inputManager.getInputDeviceIds();

		// add new devices
		for (int deviceId : deviceIds)
		{
			if (listDeviceId.indexOf(deviceId) == -1)
			{
				handleInputDeviceAdded(deviceId);
			}
		}

		Log.debug("scanDevices stage 1, listDeviceId size: " + listDeviceId.size());

		// remove disconnected devices
		for (int index = 0; index < listDeviceId.size(); ++index)
		{
			int prevId = listDeviceId.get(index);
			boolean found = false;
			for (int deviceId : deviceIds) 
			{
				if (prevId == deviceId)
				{
					found = true;
					break;
				}
			}

			if (!found)
			{
				handleInputDeviceRemoved(prevId, false);
				listDeviceId.remove(index--);

				Log.debug("scanDevices removes device: " + prevId);
			}
		}
		
		Log.debug("scanDevices stage 2, listDeviceId size: " + listDeviceId.size());
    }

    public void onStop() 
	{
        if (gameControllerThread != null) 
		{
            gameControllerThread.stopListening();
			Log.debug("stopListening...");
        }
    }

    public void onStart() 
	{
        if (gameControllerThread != null) 
		{
			// back to foreground and update all connected devices
            scanDevices();
            gameControllerThread.startListening();
			Log.debug("startListening...");
        } 
		else 
		{
			Log = new Logger("UE", "GameControllerManager");

            gameControllerThread = new GameControllerThread();
            gameControllerThread.init(this);
            gameControllerThread.start();
        }
    }

	private static boolean isTouchScreen(int deviceId)
	{
		InputDevice inputDevice = InputDevice.getDevice(deviceId);
		if (inputDevice != null)
		{
			int inputDeviceSources = inputDevice.getSources();
			int maskTouchScreen = InputDevice.SOURCE_ANY & InputDevice.SOURCE_TOUCHSCREEN;
			if ((inputDevice.isVirtual() == false) &&
				((inputDeviceSources & maskTouchScreen) != 0))
			{
				return true;
			}
		}
		return false;
	}

    private static boolean isGameController(int deviceId) 
	{
        InputDevice inputDevice = InputDevice.getDevice(deviceId);
        if (inputDevice != null) 
		{
            int inputDeviceSources = inputDevice.getSources();
            int sourceMask = InputDevice.SOURCE_ANY & GAMECONTROLLER_SOURCE_MASK;
            if ((inputDevice.isVirtual() == false) && ((inputDeviceSources & sourceMask) != 0))
			{
				String deviceName = inputDevice.getName();
				List<InputDevice.MotionRange> motionRanges = inputDevice.getMotionRanges();
				if (motionRanges != null && motionRanges.size() > 0)
				{
					// Some physical keyboards include DPAD and JOYSTICK sources, but
					// only report a single AXIS_GENERIC_1 motion range, screen them out
					// as they aren't really game controllers
					if (motionRanges.size() == 1 && motionRanges.get(0).getAxis() == MotionEvent.AXIS_GENERIC_1)
					{
						// Ignore the fingerprint reader, which can also identify
						// as a game controller for some reason
						if (deviceName.equalsIgnoreCase(FINGERPRINT_DEVICE_NAME))
						{
							return false;
						}
					}

					return true;
				}
            }
        }

        return false;
    }

	private native void nativeOnInputDeviceStateEvent(int deviceId, int state, int type);

	private InputDeviceType GetDeviceType(int deviceId)
	{
		if (isTouchScreen(deviceId))
		{
			return InputDeviceType.TouchScreen;
		}
		if (isGameController(deviceId))
		{
			return InputDeviceType.GameController;
		}
		return InputDeviceType.Unknown;
	}

	private void handleInputDeviceAdded(int deviceId)
	{
		if (listDeviceId.indexOf(deviceId) == -1)
		{
			listDeviceId.add(deviceId);
		}
		
		InputDeviceType deviceType = GetDeviceType(deviceId);
		if (deviceType != InputDeviceType.Unknown)
		{
			nativeOnInputDeviceStateEvent(deviceId, InputDeviceState.Added.ordinal(), deviceType.ordinal());
		}
	}

	private void handleInputDeviceRemoved(int deviceId, boolean removeFromList)
	{
		if (removeFromList)
		{
			for (int index = 0; index < listDeviceId.size(); ++index)
			{
				if (listDeviceId.get(index) == deviceId)
				{
					listDeviceId.remove(index);
					break;
				}
			}
		}

		// the device has been removed. use Unknown for the type parameter.
		nativeOnInputDeviceStateEvent(deviceId, InputDeviceState.Removed.ordinal(), InputDeviceType.Unknown.ordinal());
	}

    public void onInputDeviceAdded(int deviceId)
    {
	    Log.debug("onInputDeviceAdded, id: " + deviceId);
		handleInputDeviceAdded(deviceId);
    }

    public void onInputDeviceRemoved(int deviceId)
    {
        Log.debug("onInputDeviceRemoved, id: " + deviceId);
		handleInputDeviceRemoved(deviceId, true);
    }

    public void onInputDeviceChanged(int deviceId)
    {
        Log.debug("onInputDeviceChanged, id: " + deviceId);

		if (listDeviceId.indexOf(deviceId) == -1)
		{
			Log.warn("The changed input device wasn't recorded before, id: " + deviceId);
			listDeviceId.add(deviceId);
		}

		InputDeviceType deviceType = GetDeviceType(deviceId);
		nativeOnInputDeviceStateEvent(deviceId, InputDeviceState.Changed.ordinal(), deviceType.ordinal());
    }
}
