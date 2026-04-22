// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.unreal;

import android.app.Notification;
import android.app.NotificationManager;
import android.app.NotificationChannel;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import androidx.core.app.NotificationCompat;

public class LocalNotificationReceiver extends BroadcastReceiver
{
	private static final String NOTIFICATION_CHANNEL_ID = "ue4-push-notification-channel-id";
	private static final String NOTICATION_CHANNEL_NAME = "ue4-push-notification-channel";
	private static final String NOTIFICATION_CHANNEL_NAME_RESOURCE = "UEPushChannel";

	public static final String KEY_LOCAL_NOTIFICATION_ID = "local-notification-ID";
	public static final String KEY_LOCAL_NOTIFICATION_TITLE = "local-notification-title";
	public static final String KEY_LOCAL_NOTIFICATION_BODY = "local-notification-body";
	public static final String KEY_LOCAL_NOTIFICATION_ACTION = "local-notification-action";
	public static final String KEY_LOCAL_NOTIFICATION_ACTION_EVENT = "local-notification-activationEvent";

	public void onReceive(Context context, Intent intent)
	{
		int notificationID = intent.getIntExtra(KEY_LOCAL_NOTIFICATION_ID , 0);
		String title  = intent.getStringExtra(KEY_LOCAL_NOTIFICATION_TITLE);
		String details  = intent.getStringExtra(KEY_LOCAL_NOTIFICATION_BODY);
		String action = intent.getStringExtra(KEY_LOCAL_NOTIFICATION_ACTION);
		String activationEvent = intent.getStringExtra(KEY_LOCAL_NOTIFICATION_ACTION_EVENT);

		if(title == null || details == null || action == null || activationEvent == null)
		{
			// Do not schedule any local notification if any allocation failed
			return;
		}

		// Open UE5 app if clicked
		Intent notificationIntent = new Intent(context, GameActivity.class);

		// launch if closed but resume if running
		notificationIntent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
		notificationIntent.putExtra("localNotificationID" , notificationID);
		notificationIntent.putExtra("localNotificationAppLaunched" , true);
		notificationIntent.putExtra("localNotificationLaunchActivationEvent", activationEvent);

		int notificationIconID = getNotificationIconID(context);
		PendingIntent pendingNotificationIntent = PendingIntent.getActivity(context, notificationID, notificationIntent, PendingIntent.FLAG_IMMUTABLE);

		NotificationManager notificationManager = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
		@SuppressWarnings("deprecation")
		NotificationCompat.Builder builder = new NotificationCompat.Builder(context, NOTIFICATION_CHANNEL_ID)
			.setSmallIcon(notificationIconID)
			.setContentIntent(pendingNotificationIntent)
			.setWhen(System.currentTimeMillis())
			.setTicker(details)		// note: will not show up on Lollipop up except for accessibility
			.setContentTitle(title)
			.setStyle(new NotificationCompat.BigTextStyle().bigText(details));			
		if (android.os.Build.VERSION.SDK_INT >= 21)
		{
			builder.setContentText(details);
			builder.setColor(0xff0e1e43);
		}

		if (android.os.Build.VERSION.SDK_INT >= 26)
		{
			if(notificationManager != null)
			{
				NotificationChannel channel = notificationManager.getNotificationChannel(NOTIFICATION_CHANNEL_ID);
				if (channel == null)
				{
					CharSequence channelName = getLocalizedResource(context.getApplicationContext(), NOTIFICATION_CHANNEL_NAME_RESOURCE, NOTICATION_CHANNEL_NAME);
					channel = new NotificationChannel(NOTIFICATION_CHANNEL_ID, channelName, NotificationManager.IMPORTANCE_DEFAULT);
					channel.enableVibration(true);
					channel.enableLights(true);
					notificationManager.createNotificationChannel(channel);
				}
			}
		}
		Notification notification = builder.build();

		// Stick with the defaults
		notification.flags |= Notification.FLAG_AUTO_CANCEL;
		notification.defaults |= Notification.DEFAULT_SOUND | Notification.DEFAULT_VIBRATE;

		if(notificationManager != null)
		{
			// show the notification
			notificationManager.notify(notificationID, notification);
			
			// clear the stored notification details if they exist
			GameActivity.LocalNotificationRemoveDetails(context, notificationID);
		}
	}

	public static int getNotificationIconID(Context context)
	{
		int notificationIconID = context.getResources().getIdentifier("ic_notification_simple", "drawable", context.getPackageName());
		if (notificationIconID == 0)
		{
			notificationIconID = context.getResources().getIdentifier("ic_notification", "drawable", context.getPackageName());
		}
		if (notificationIconID == 0)
		{
			notificationIconID = context.getResources().getIdentifier("icon", "drawable", context.getPackageName());
		}
		return notificationIconID;
	}

	private static int getResourceId(Context context, String VariableName, String ResourceName, String PackageName)
	{
		try {
			return context.getResources().getIdentifier(VariableName, ResourceName, PackageName);
		}
		catch (Exception e) {
			e.printStackTrace();
			return -1;
		} 
	}

	private static String getResourceStringOrDefault(Context context, String PackageName, String ResourceName, String DefaultString)
	{
		int resourceId = getResourceId(context, ResourceName, "string", PackageName);
		return (resourceId < 1) ? DefaultString : context.getResources().getString(resourceId);
	}

	public static String getLocalizedResource(Context context, String baseName, String defaultValue)
	{
		String packageName = context.getApplicationContext().getPackageName();

		// first try using Android default locale handling
		String checkValue = getResourceStringOrDefault(context, packageName, baseName, "");
		if (!"".equals(checkValue))
		{
			return checkValue;
		}

		// check for default English (if it doesn't exist, no other locales do either
		String englishValue = getResourceStringOrDefault(context, packageName, baseName + "_en", "");
		if ("".equals(englishValue))
		{
			return defaultValue;
		}

		// now try exact locale
		String locale = java.util.Locale.getDefault().toString().replace("-", "_");
		checkValue = getResourceStringOrDefault(context, packageName, baseName + "_" + locale, "");
		if (!"".equals(checkValue))
		{
			return checkValue;
		}

		// next try without underscore if there is one (ex. es_MX -> es)
		if (locale.contains("_"))
		{
			String partialLocale = locale.substring(0, locale.indexOf("_"));
			checkValue = getResourceStringOrDefault(context, packageName, baseName + "_" + partialLocale, "");
			if (!"".equals(checkValue))
			{
				return checkValue;
			}
		}

		// finally try some remappings
		switch (locale)
		{
			case "es_419": locale = "es_MX"; break;
			case "zh": locale = "zh_CN"; break;
			case "zh_Hant": locale = "zh_TW"; break;
			case "zh_rTW": locale = "zh_TW"; break;
			case "zh_rHK": locale = "zh_TW"; break;
			case "zh_rMO": locale = "zh_TW"; break;
		}
		checkValue = getResourceStringOrDefault(context, packageName, baseName + "_" + locale, "");
		if (!"".equals(checkValue))
		{
			return checkValue;
		}

		// finally, fall back to English
		return englishValue;
	}
}
