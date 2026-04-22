// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.unreal;

import android.content.res.AssetFileDescriptor;
import android.content.res.AssetManager;
import android.opengl.*;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import android.media.MediaDataSource;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.media.MediaPlayer;

import java.util.ArrayList;
import java.net.URL;
import java.net.HttpURLConnection;

/*
	Custom media player that renders video to a frame buffer. This
	variation is for API 14 and above.
*/
public class MediaPlayer14
	extends android.media.MediaPlayer
{
	private boolean SwizzlePixels = true;
	private boolean VulkanRenderer = false;
	private boolean NeedTrackInfo = true;
	private boolean Looping = false;
	private boolean AudioEnabled = true;
	private float AudioVolume = 1.0f;
	private volatile boolean WaitOnBitmapRender = false;
	private volatile boolean Prepared = false;
	private volatile boolean Completed = false;

	private BitmapRenderer mBitmapRenderer = null;
	private OESTextureRenderer mOESTextureRenderer = null;

	public class FrameUpdateInfo {
		public int CurrentPosition;
		public boolean FrameReady;
		public boolean RegionChanged;
		public float UScale;
		public float UOffset;
		public float VScale;
		public float VOffset;
	}

	public class AudioTrackInfo {
		public int Index;
		public String MimeType;
		public String DisplayName;
		public String Language;
		public int Channels;
		public int SampleRate;
	}

	public class CaptionTrackInfo {
		public int Index;
		public String MimeType;
		public String DisplayName;
		public String Language;
	}

	public class VideoTrackInfo {
		public int Index;
		public String MimeType;
		public String DisplayName;
		public String Language;
		public int BitRate;
		public int Width;
		public int Height;
		public float FrameRate;
	}

	private ArrayList<AudioTrackInfo> audioTracks = new ArrayList<AudioTrackInfo>();
	private ArrayList<VideoTrackInfo> videoTracks = new ArrayList<VideoTrackInfo>();

	// ======================================================================================

	public MediaPlayer14(boolean swizzlePixels, boolean vulkanRenderer, boolean needTrackInfo)
	{
		SwizzlePixels = swizzlePixels;
		VulkanRenderer = vulkanRenderer;
		NeedTrackInfo = needTrackInfo;
		WaitOnBitmapRender = false;
		AudioEnabled = true;
		AudioVolume = 1.0f;

		setOnErrorListener(new MediaPlayer.OnErrorListener() {
			@Override
			public boolean onError(MediaPlayer mp, int what, int extra) {
				GameActivity.Log.debug("MediaPlayer14: onError returned what=" + what + ", extra=" + extra);
				return true;
			}
		});

		setOnPreparedListener(new MediaPlayer.OnPreparedListener() {
			@Override
			public void onPrepared(MediaPlayer player) {
//				GameActivity.Log.debug("*** MEDIA PREPARED ***");
				synchronized(player)
				{
					Prepared = true;
				}
			}
		});

		setOnCompletionListener(new MediaPlayer.OnCompletionListener() {
			@Override
			public void onCompletion(MediaPlayer player) {
				synchronized(player)
				{
//					GameActivity.Log.debug("*** MEDIA COMPLETION ***");
					if (Looping)
					{
						seekTo(0);
						start();
					}
					Completed = true;
				}
			}
		});
	}

	public boolean isPrepared()
	{
		boolean result;
		synchronized(this)
		{
			result = Prepared;
		}
		return result;
	}

	public boolean didComplete()
	{
		boolean result;
		synchronized(this)
		{
			result = Completed;
			Completed = false;
		}
		return result;
	}

	public boolean isLooping()
	{
		return Looping;
	}

	private void updateTrackInfo(MediaExtractor extractor)
	{
		if (extractor == null)
		{
			return;
		}

		int numTracks = extractor.getTrackCount();
		int numAudioTracks = 0;
		int numVideoTracks = 0;
		audioTracks.ensureCapacity(numTracks);
		videoTracks.ensureCapacity(numTracks);

		for (int index=0; index < numTracks; index++)
		{
			MediaFormat mediaFormat = extractor.getTrackFormat(index);
			String mimeType = mediaFormat.getString(MediaFormat.KEY_MIME);
			if (mimeType.startsWith("audio"))
			{
				AudioTrackInfo audioTrack = new AudioTrackInfo();
				audioTrack.Index = index;
				audioTrack.MimeType = mimeType;
				audioTrack.DisplayName = "Audio Track " + numAudioTracks + " (Stream " + index + ")";
				audioTrack.Language = "und";
				audioTrack.Channels = mediaFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
				audioTrack.SampleRate = mediaFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE);
				audioTracks.add(audioTrack);
				numAudioTracks++;
			}
			else if (mimeType.startsWith("video"))
			{
				VideoTrackInfo videoTrack = new VideoTrackInfo();
				videoTrack.Index = index;
				videoTrack.MimeType = mimeType;
				videoTrack.DisplayName = "Video Track " + numVideoTracks + " (Stream " + index + ")";
				videoTrack.Language = "und";
				videoTrack.BitRate = 0;
				videoTrack.Width = mediaFormat.getInteger(MediaFormat.KEY_WIDTH);
				videoTrack.Height = mediaFormat.getInteger(MediaFormat.KEY_HEIGHT);
				videoTrack.FrameRate = 30.0f;
				if (mediaFormat.containsKey(MediaFormat.KEY_FRAME_RATE))
				{
					videoTrack.FrameRate = mediaFormat.getInteger(MediaFormat.KEY_FRAME_RATE);
				}
				videoTracks.add(videoTrack);
				numVideoTracks++;
			}
		}
	}

	private AudioTrackInfo findAudioTrackIndex(int index)
	{
		for (AudioTrackInfo track : audioTracks)
		{
			if (track.Index == index)
			{
				return track;
			}
		}
		return null;
	}

	private VideoTrackInfo findVideoTrackIndex(int index)
	{
		for (VideoTrackInfo track : videoTracks)
		{
			if (track.Index == index)
			{
				return track;
			}
		}
		return null;
	}

	public static String RemoteFileExists(String URLName)
	{
		// do not try more than 5 redirects, return final URL or null
		int MaxRedirects = 5;
		
		// we set redirect to false and do it manually so we can handle redirect between HTTP/HTTPS which Java doesn't do
		boolean restoreRedirects = HttpURLConnection.getFollowRedirects();
		HttpURLConnection.setFollowRedirects(false);

		while (MaxRedirects-- > 0)
		{
            try {
				HttpURLConnection con = (HttpURLConnection) new URL(URLName).openConnection();
				con.setRequestMethod("HEAD");
				int responseCode = con.getResponseCode();
				if (responseCode == HttpURLConnection.HTTP_OK)
				{
					con.disconnect();
					HttpURLConnection.setFollowRedirects(restoreRedirects);
					return URLName;
				}
				if (responseCode == HttpURLConnection.HTTP_SEE_OTHER || responseCode == HttpURLConnection.HTTP_MOVED_PERM || responseCode == HttpURLConnection.HTTP_MOVED_TEMP)
				{
					String Location = con.getHeaderField("Location");
					URL url = con.getURL();
					con.disconnect();
					URLName = Location.contains("://") ? Location : url.getProtocol() + "://" + url.getHost() + Location;
					continue;
				}
				HttpURLConnection.setFollowRedirects(restoreRedirects);
				return null;
			}
			catch (Exception e) {
				e.printStackTrace();
				HttpURLConnection.setFollowRedirects(restoreRedirects);
				return null;
			}
		}
		HttpURLConnection.setFollowRedirects(restoreRedirects);
		return null;
	}

	public boolean setDataSourceURL(
		String UrlPath)
		throws IOException,
			java.lang.InterruptedException,
			java.util.concurrent.ExecutionException
	{
		synchronized(this)
		{
			Prepared = false;
			Completed = false;
		}
		Looping = false;
		AudioEnabled = true;
		audioTracks.clear();
		videoTracks.clear();

		UrlPath = RemoteFileExists(UrlPath);
		if (UrlPath == null)
		{
			return false;
		}

		try
		{
			setDataSource(UrlPath);
			releaseOESTextureRenderer();
			releaseBitmapRenderer();
			if (NeedTrackInfo && android.os.Build.VERSION.SDK_INT >= 16)
			{
				MediaExtractor extractor = new MediaExtractor();
				if (extractor != null)
				{
					try
					{
						extractor.setDataSource(UrlPath);
						updateTrackInfo(extractor);
						extractor.release();
						extractor = null;
					}
					catch (Exception e)
					{
						GameActivity.Log.debug("setDataSourceURL: Exception = " + e);

						// unable to collect track info, but can still try to play it
						GameActivity.Log.debug("setDataSourceURL: Continuing without track info");
						extractor.release();
						extractor = null;
						return true;
					}
				}
			}
		}
		catch(IOException e)
		{
			GameActivity.Log.debug("setDataSourceURL: Exception = " + e);
			return false;
		}
		return true;
	}

	public native int nativeReadAt(long identifier, long position, java.nio.ByteBuffer buffer, int offset, int size);

	public class PakDataSource extends MediaDataSource {
		java.nio.ByteBuffer fileBuffer;
		long identifier;
		long fileSize;

		public PakDataSource(long inIdentifier, long inFileSize)
		{
			fileBuffer = java.nio.ByteBuffer.allocateDirect(65536);
			identifier = inIdentifier;
			fileSize = inFileSize;
		}

		@Override
		public synchronized int readAt(long position, byte[] buffer, int offset, int size) throws IOException
		{
			synchronized(fileBuffer)
			{
				//GameActivity.Log.debug("PDS: readAt(" + position + ", " + offset + ", " + size + ") bufferLen=" + buffer.length + ", fileBuffer.length=" + fileSize);
				if (position >= fileSize)
				{
					return -1;  // EOF
				}
				if (position + size > fileSize)
				{
					size = (int)(fileSize - position);
				}
				if (size > 0)
				{
					int readBytes = nativeReadAt(identifier, position, fileBuffer, 0, size);
					if (readBytes > 0)
					{
						System.arraycopy(fileBuffer.array(), fileBuffer.arrayOffset(), buffer, offset, readBytes);
					}

					return readBytes;
				}
				return 0;
			}
		}

		@Override
		public synchronized long getSize() throws IOException
		{
			//GameActivity.Log.debug("PDS: getSize() = " + fileSize);
			return fileSize;
		}

		@Override
		public synchronized void close() throws IOException
		{
			//GameActivity.Log.debug("PDS: close()");
		}
	}

	public boolean setDataSourceArchive(
		long identifier, long size)
		throws IOException,
			java.lang.InterruptedException,
			java.util.concurrent.ExecutionException
	{
		synchronized(this)
		{
			Prepared = false;
			Completed = false;
		}
		Looping = false;
		AudioEnabled = true;
		audioTracks.clear();
		videoTracks.clear();

		// Android 6.0 required for MediaDataSource
		if (android.os.Build.VERSION.SDK_INT < 23)
		{
			return false;
		}

		try
		{
			PakDataSource dataSource = new PakDataSource(identifier, size);
			setDataSource(dataSource);

			releaseOESTextureRenderer();
			releaseBitmapRenderer();

			if (NeedTrackInfo && android.os.Build.VERSION.SDK_INT >= 16)
			{
				MediaExtractor extractor = new MediaExtractor();
				if (extractor != null)
				{
					extractor.setDataSource(dataSource);
					updateTrackInfo(extractor);
					extractor.release();
					extractor = null;
				}
			}
		}
		catch(IOException e)
		{
			GameActivity.Log.debug("setDataSource (archive): Exception = " + e);
			return false;
		}
		return true;
	}

	public boolean setDataSource(
		String moviePath, long offset, long size)
		throws IOException,
			java.lang.InterruptedException,
			java.util.concurrent.ExecutionException
	{
		synchronized(this)
		{
			Prepared = false;
			Completed = false;
		}
		Looping = false;
		AudioEnabled = true;
		audioTracks.clear();
		videoTracks.clear();

		try
		{
			File f = new File(moviePath);
			if (!f.exists() || !f.isFile()) 
			{
				return false;
			}
			RandomAccessFile data = new RandomAccessFile(f, "r");
			setDataSource(data.getFD(), offset, size);
			releaseOESTextureRenderer();
			releaseBitmapRenderer();

			if (NeedTrackInfo && android.os.Build.VERSION.SDK_INT >= 16)
			{
				MediaExtractor extractor = new MediaExtractor();
				if (extractor != null)
				{
					extractor.setDataSource(data.getFD(), offset, size);
					updateTrackInfo(extractor);
					extractor.release();
					extractor = null;
				}
			}
		}
		catch(IOException e)
		{
			GameActivity.Log.debug("setDataSource (file): Exception = " + e);
			return false;
		}
		return true;
	}
	
	public boolean setDataSource(
		AssetManager assetManager, String assetPath, long offset, long size)
		throws java.lang.InterruptedException,
			java.util.concurrent.ExecutionException
	{
		synchronized(this)
		{
			Prepared = false;
			Completed = false;
		}
		Looping = false;
		AudioEnabled = true;
		audioTracks.clear();
		videoTracks.clear();

		try
		{
			AssetFileDescriptor assetFD = assetManager.openFd(assetPath);
			setDataSource(assetFD.getFileDescriptor(), offset, size);
			releaseOESTextureRenderer();
			releaseBitmapRenderer();

			if (NeedTrackInfo && android.os.Build.VERSION.SDK_INT >= 16)
			{
				MediaExtractor extractor = new MediaExtractor();
				if (extractor != null)
				{
					extractor.setDataSource(assetFD.getFileDescriptor(), offset, size);
					updateTrackInfo(extractor);
					extractor.release();
					extractor = null;
				}
			}
		}
		catch(IOException e)
		{
			GameActivity.Log.debug("setDataSource (asset): Exception = " + e);
			return false;
		}
		return true;
	}

	private boolean mVideoEnabled = true;
	
	public void setVideoEnabled(boolean enabled)
	{
		WaitOnBitmapRender = true;

		mVideoEnabled = enabled;
		if (mVideoEnabled)
		{
			if (null != mOESTextureRenderer && null != mOESTextureRenderer.getSurface())
			{
				setSurface(mOESTextureRenderer.getSurface());
			}

			if (null != mBitmapRenderer && null != mBitmapRenderer.getSurface())
			{
				setSurface(mBitmapRenderer.getSurface());
			}
		}
		else
		{
			setSurface(null);
		}

		WaitOnBitmapRender = false;
	}
	
	public void setAudioEnabled(boolean enabled)
	{
		AudioEnabled = enabled;
		if (enabled)
		{
			setVolume(AudioVolume,AudioVolume);
		}
		else
		{
			setVolume(0,0);
		}
	}

	public void setAudioVolume(float volume)
	{
		AudioVolume = volume;
		setAudioEnabled(AudioEnabled);
	}
	
	public boolean didResolutionChange()
	{
		if (null != mOESTextureRenderer)
		{
			return mOESTextureRenderer.resolutionChanged();
		}
		if (null != mBitmapRenderer)
		{
			return mBitmapRenderer.resolutionChanged();
		}
		return false;
	}

	public int getExternalTextureId()
	{
		if (null != mOESTextureRenderer)
		{
			return mOESTextureRenderer.getExternalTextureId();
		}
		if (!usingBitmapRendererImageReader && (null != mBitmapRenderer) && (mBitmapRenderer instanceof BitmapRendererLegacy))
		{
			return ((BitmapRendererLegacy)mBitmapRenderer).getExternalTextureId();
		}
		return 0; // 0 is the only value never used in GLES - the red book
	}

	public void prepare() throws IOException, IllegalStateException
	{
		synchronized(this)
		{
			Completed = false;
			try
			{
				super.prepare();
			}
			catch (IOException e)
			{
				GameActivity.Log.debug("MediaPlayer14: Prepare IOException: " + e.toString());
				throw e;
			}
			catch (IllegalStateException e)
			{
				GameActivity.Log.debug("MediaPlayer14: Prepare IllegalStateExecption: " + e.toString());
				throw e;
			}
			catch (Exception e)
			{
				GameActivity.Log.debug("MediaPlayer14: Prepare Exception: " + e.toString());
				throw e;
			}
			Prepared = true;
		}
	}

	public void start()
	{
		synchronized(this)
		{
			Completed = false;
			if (Prepared)
			{
				super.start();
			}
		}
	}

	public void pause()
	{
		synchronized(this)
		{
			Completed = false;
			if (Prepared)
			{
				super.pause();
			}
		}
	}

	public void stop()
	{
		synchronized(this)
		{
			Completed = false;
			if (Prepared)
			{
				super.stop();
			}
		}
	}

	public int getCurrentPosition()
	{
		int position = 0;

		synchronized(this)
		{
			if (Prepared)
			{
				position = super.getCurrentPosition();
			}
		}

		return position;
	}

	public int getDuration()
	{
		int duration = 0;
		
		synchronized(this)
		{
			if (Prepared)
			{
				duration = super.getDuration();
			}
		}

		return duration;
	}
	
	public void seekTo(int position)
	{
		synchronized (this)
		{
			Completed = false;
			if (Prepared)
			{
				super.seekTo(position);
			}
		}
	}

	public void setLooping(boolean looping)
	{
		// don't set on player
		Looping = looping;
	}

	public void release()
	{
		if (null != mOESTextureRenderer)
		{
			while (WaitOnBitmapRender) ;
			releaseOESTextureRenderer();
		}
		if (null != mBitmapRenderer)
		{
			while (WaitOnBitmapRender) ;
			releaseBitmapRenderer();
		}
		super.release();
	}

	public void reset()
	{
		synchronized(this)
		{
			Prepared = false;
			Completed = false;
		}
		if (null != mOESTextureRenderer)
		{
			while (WaitOnBitmapRender) ;
			releaseOESTextureRenderer();
		}
		if (null != mBitmapRenderer)
		{
			while (WaitOnBitmapRender) ;
			releaseBitmapRenderer();
		}
		super.reset();
	}

	// ======================================================================================

	private boolean usingBitmapRendererImageReader = false;
	private static final int ImageReaderQueueLength = 16;
	private BitmapRenderer.UpdateParam updateParam = new BitmapRenderer.UpdateParam();

	public void setUsingBitmapRendererImageReader(boolean enabled)
	{
		usingBitmapRendererImageReader = enabled;
	}

	private boolean CreateBitmapRenderer()
	{
		releaseBitmapRenderer();

		GameActivity.Log.debug("MediaPlayer - CREATE BITMAPRENDERER, VIDEO SIZE: " + getVideoWidth() + " x " + getVideoHeight() + ", BRIR: " + usingBitmapRendererImageReader + ", Vulkan: " + VulkanRenderer);
		
		if (usingBitmapRendererImageReader)
		{
			mBitmapRenderer = new BitmapRendererImageReader(SwizzlePixels, VulkanRenderer,
					getVideoWidth(), getVideoHeight(), ImageReaderQueueLength);
		}
		else
		{
			mBitmapRenderer = new BitmapRendererLegacy(SwizzlePixels, VulkanRenderer, true);
		}

		if (!mBitmapRenderer.isValid())
		{
			mBitmapRenderer = null;
			return false;
		}
		
		// set this here as the size may have been set before the GL resources were created.
		mBitmapRenderer.setSize(getVideoWidth(),getVideoHeight());

		setOnVideoSizeChangedListener(new android.media.MediaPlayer.OnVideoSizeChangedListener() {
			public void onVideoSizeChanged(android.media.MediaPlayer player, int w, int h)
			{
//				GameActivity.Log.debug("VIDEO SIZE CHANGED: " + w + " x " + h);
				if (null != mBitmapRenderer)
				{
					mBitmapRenderer.setSize(w,h);
				}
			}
		});
		setVideoEnabled(true);
		if (AudioEnabled)
		{
			setAudioEnabled(true);
		}
		return true;
	}

	void releaseBitmapRenderer()
	{
		if (null != mBitmapRenderer)
		{
			mBitmapRenderer.release();
			mBitmapRenderer = null;
			setSurface(null);
			setOnVideoSizeChangedListener(null);
		}
	}

	public void initBitmapRenderer()
	{
		// if not already allocated.
		// Create bitmap renderer's gl resources in the renderer thread.
		if (null == mBitmapRenderer)
		{
			if (!CreateBitmapRenderer())
			{
				GameActivity.Log.warn("initBitmapRenderer failed to alloc mBitmapRenderer ");
				reset();
			}
		}
	}

	public BitmapRenderer.FrameData getVideoLastMediaFrameData()
	{
		if (usingBitmapRendererImageReader)
		{
			initBitmapRenderer();
			if (mBitmapRenderer != null)
			{
				WaitOnBitmapRender = true;
				BitmapRenderer.FrameData data = mBitmapRenderer.updateFrameData(updateParam);
				WaitOnBitmapRender = false;
				return data;
			}
		}
		return null;
	}

	public java.nio.Buffer getVideoLastFrameData()
	{
		if (usingBitmapRendererImageReader)
		{
			return null;
		}
		
		initBitmapRenderer();
		if (null != mBitmapRenderer)
		{
			WaitOnBitmapRender = true;

			updateParam.destTexture = 0;
			BitmapRenderer.FrameData data = mBitmapRenderer.updateFrameData(updateParam);

			WaitOnBitmapRender = false;
			return data != null ? data.binaryBuffer : null;
		}
		return null;
	}

	public boolean getVideoLastFrame(int destTexture)
	{
//		GameActivity.Log.debug("getVideoLastFrame: " + destTexture);
		if (usingBitmapRendererImageReader)
		{
			return false;
		}

		initBitmapRenderer();
		if (null != mBitmapRenderer)
		{
			WaitOnBitmapRender = true;

			updateParam.destTexture = destTexture;
			BitmapRenderer.FrameData data = mBitmapRenderer.updateFrameData(updateParam);

			WaitOnBitmapRender = false;
			return data != null ? data.frameReady : false;
		}
		return false;
	}

	// ======================================================================================

	private boolean CreateOESTextureRenderer(int OESTextureId)
	{
		releaseOESTextureRenderer();

		mOESTextureRenderer = new OESTextureRenderer(OESTextureId);
		if (!mOESTextureRenderer.isValid())
		{
			mOESTextureRenderer = null;
			return false;
		}
		
		// set this here as the size may have been set before the GL resources were created.
		mOESTextureRenderer.setSize(getVideoWidth(),getVideoHeight());

		setOnVideoSizeChangedListener(new android.media.MediaPlayer.OnVideoSizeChangedListener() {
			public void onVideoSizeChanged(android.media.MediaPlayer player, int w, int h)
			{
//				GameActivity.Log.debug("VIDEO SIZE CHANGED: " + w + " x " + h);
				if (null != mOESTextureRenderer)
				{
					mOESTextureRenderer.setSize(w,h);
				}
			}
		});
		setVideoEnabled(true);
		if (AudioEnabled)
		{
			setAudioEnabled(true);
		}
		return true;
	}

	void releaseOESTextureRenderer()
	{
		if (null != mOESTextureRenderer)
		{
			mOESTextureRenderer.release();
			mOESTextureRenderer = null;
			setSurface(null);
			setOnVideoSizeChangedListener(null);
		}
	}
	
	public FrameUpdateInfo updateVideoFrame(int externalTextureId)
	{
		if (null == mOESTextureRenderer)
		{
			if (!CreateOESTextureRenderer(externalTextureId))
			{
				GameActivity.Log.warn("updateVideoFrame failed to alloc mOESTextureRenderer ");
				reset();
				return null;
			}
		}

		WaitOnBitmapRender = true;
		FrameUpdateInfo result = mOESTextureRenderer.updateVideoFrame();
		WaitOnBitmapRender = false;
		return result;
	}

	/*
		This handles events for our OES texture
	*/
	class OESTextureRenderer
		implements android.graphics.SurfaceTexture.OnFrameAvailableListener
	{
		private android.graphics.SurfaceTexture mSurfaceTexture = null;
		private int mTextureWidth = -1;
		private int mTextureHeight = -1;
		private android.view.Surface mSurface = null;
		private boolean mFrameAvailable = false;
		private int mTextureID = -1;
		private float[] mTransformMatrix = new float[16];
		private boolean mTextureSizeChanged = true;
		private int GL_TEXTURE_EXTERNAL_OES = 0x8D65;

		private float mUScale = 1.0f;
		private float mVScale = -1.0f;
		private float mUOffset = 0.0f;
		private float mVOffset = 1.0f;

		public OESTextureRenderer(int OESTextureId)
		{
			mTextureID = OESTextureId;

			mSurfaceTexture = new android.graphics.SurfaceTexture(mTextureID);
			mSurfaceTexture.setOnFrameAvailableListener(this);
			mSurface = new android.view.Surface(mSurfaceTexture);
		}

		public void release()
		{
			if (null != mSurface)
			{
				mSurface.release();
				mSurface = null;
			}
			if (null != mSurfaceTexture)
			{
				mSurfaceTexture.release();
				mSurfaceTexture = null;
			}
		}

		public boolean isValid()
		{
			return mSurfaceTexture != null;
		}

		public void onFrameAvailable(android.graphics.SurfaceTexture st)
		{
			synchronized(this)
			{
				mFrameAvailable = true;
			}
		}

		public android.graphics.SurfaceTexture getSurfaceTexture()
		{
			return mSurfaceTexture;
		}

		public android.view.Surface getSurface()
		{
			return mSurface;
		}

		public int getExternalTextureId()
		{
			return mTextureID;
		}

		// NOTE: Synchronized with updateFrameData to prevent frame
		// updates while the surface may need to get reallocated.
		public void setSize(int width, int height)
		{
			synchronized(this)
			{
				if (width != mTextureWidth ||
					height != mTextureHeight)
				{
					mTextureWidth = width;
					mTextureHeight = height;
					mTextureSizeChanged = true;
				}
			}
		}

		public boolean resolutionChanged()
		{
			boolean changed;
			synchronized(this)
			{
				changed = mTextureSizeChanged;
				mTextureSizeChanged = false;
			}
			return changed;
		}

		public FrameUpdateInfo updateVideoFrame()
		{
			synchronized(this)
			{
				return getFrameUpdateInfo();
			}
		}

		private FrameUpdateInfo getFrameUpdateInfo()
		{
			FrameUpdateInfo frameUpdateInfo = new FrameUpdateInfo();

			frameUpdateInfo.CurrentPosition = getCurrentPosition();
			frameUpdateInfo.FrameReady = false;
			frameUpdateInfo.RegionChanged = false;
			frameUpdateInfo.UScale = mUScale;
			frameUpdateInfo.UOffset = mUOffset;

			// note: the matrix has V flipped
			frameUpdateInfo.VScale = -mVScale;
			frameUpdateInfo.VOffset = 1.0f - mVOffset;

			if (!mFrameAvailable)
			{
				// We only return fresh data when we generate it. At other
				// time we return nothing to indicate that there was nothing
				// new to return. The media player deals with this by keeping
				// the last frame around and using that for rendering.
				return frameUpdateInfo;
			}
			mFrameAvailable = false;
			if (null == mSurfaceTexture)
			{
				// Can't update if there's no surface to update into.
				return frameUpdateInfo;
			}

			frameUpdateInfo.FrameReady = true;

			// Get the latest video texture frame.
			mSurfaceTexture.updateTexImage();
			mSurfaceTexture.getTransformMatrix(mTransformMatrix);

			if (mUScale != mTransformMatrix[0] ||
				mVScale != mTransformMatrix[5] ||
				mUOffset != mTransformMatrix[12] ||
				mVOffset != mTransformMatrix[13])
			{
				mUScale = mTransformMatrix[0];
				mVScale = mTransformMatrix[5];
				mUOffset = mTransformMatrix[12];
				mVOffset = mTransformMatrix[13];

				frameUpdateInfo.RegionChanged = true;
				frameUpdateInfo.UScale = mUScale;
				frameUpdateInfo.UOffset = mUOffset;

				// note: the matrix has V flipped
				frameUpdateInfo.VScale = -mVScale;
				frameUpdateInfo.VOffset = 1.0f - mVOffset;
			}

			// updateTexImage binds an external texture to active texture unit
			// make sure to unbind it to prevent state leak
			GLES20.glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
									
			return frameUpdateInfo;
		}
	};

	// ======================================================================================

	public AudioTrackInfo[] GetAudioTracks()
	{
		if (NeedTrackInfo && android.os.Build.VERSION.SDK_INT >= 16)
		{
			TrackInfo[] trackInfo = getTrackInfo();
			int CountTracks = 0;
			for (int Index=0; Index < trackInfo.length; Index++)
			{
				if (trackInfo[Index].getTrackType() == TrackInfo.MEDIA_TRACK_TYPE_AUDIO)
				{
					CountTracks++;
				}
			}

			AudioTrackInfo[] AudioTracks = new AudioTrackInfo[CountTracks];
			int TrackIndex = 0;
			for (int Index=0; Index < trackInfo.length; Index++)
			{
				if (trackInfo[Index].getTrackType() == TrackInfo.MEDIA_TRACK_TYPE_AUDIO)
				{
					AudioTracks[TrackIndex] = new AudioTrackInfo();
					AudioTracks[TrackIndex].Index = Index;
					AudioTracks[TrackIndex].DisplayName = "Audio Track " + TrackIndex + " (Stream " + Index + ")";
					AudioTracks[TrackIndex].Language = trackInfo[Index].getLanguage();
	
					boolean formatValid = false;
					if (android.os.Build.VERSION.SDK_INT >= 19)
					{
						MediaFormat mediaFormat = trackInfo[Index].getFormat();
						if (mediaFormat != null)
						{
							formatValid = true;
							AudioTracks[TrackIndex].MimeType = mediaFormat.getString(MediaFormat.KEY_MIME);
							AudioTracks[TrackIndex].Channels = mediaFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
							AudioTracks[TrackIndex].SampleRate = mediaFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE);
						}
					}
					if (!formatValid && audioTracks.size() > 0)
					{
						AudioTrackInfo extractTrack = findAudioTrackIndex(Index);
						if (extractTrack != null)
						{
							formatValid = true;
							AudioTracks[TrackIndex].MimeType = extractTrack.MimeType;
							AudioTracks[TrackIndex].Channels = extractTrack.Channels;
							AudioTracks[TrackIndex].SampleRate = extractTrack.SampleRate;
						}
					}
					if (!formatValid)
					{
						AudioTracks[TrackIndex].MimeType = "audio/unknown";
						AudioTracks[TrackIndex].Channels = 2;
						AudioTracks[TrackIndex].SampleRate = 44100;
					}

//					GameActivity.Log.debug(TrackIndex + " (" + Index + ") Audio: Mime=" + AudioTracks[TrackIndex].MimeType + ", Lang=" + AudioTracks[TrackIndex].Language + ", Channels=" + AudioTracks[TrackIndex].Channels + ", SampleRate=" + AudioTracks[TrackIndex].SampleRate);
					TrackIndex++;
				}
			}

			return AudioTracks;
		}

		AudioTrackInfo[] AudioTracks = new AudioTrackInfo[1];

		AudioTracks[0] = new AudioTrackInfo();
		AudioTracks[0].Index = 0;
		AudioTracks[0].MimeType = "audio/unknown";
		AudioTracks[0].DisplayName = "Audio Track 0 (Stream 0)";
		AudioTracks[0].Language = "und";
		AudioTracks[0].Channels = 2;
		AudioTracks[0].SampleRate = 44100;

		return AudioTracks;
	}

	public CaptionTrackInfo[] GetCaptionTracks()
	{
		if (NeedTrackInfo && android.os.Build.VERSION.SDK_INT >= 21)
		{
			TrackInfo[] trackInfo = getTrackInfo();
			int CountTracks = 0;
			for (int Index=0; Index < trackInfo.length; Index++)
			{
				if (trackInfo[Index].getTrackType() == 4) // TrackInfo.MEDIA_TRACK_TYPE_SUBTITLE
				{
					CountTracks++;
				}
			}

			CaptionTrackInfo[] CaptionTracks = new CaptionTrackInfo[CountTracks];
			int TrackIndex = 0;
			for (int Index=0; Index < trackInfo.length; Index++)
			{
				if (trackInfo[Index].getTrackType() == 4) // TrackInfo.MEDIA_TRACK_TYPE_SUBTITLE
				{
					CaptionTracks[TrackIndex] = new CaptionTrackInfo();
					CaptionTracks[TrackIndex].Index = Index;
					CaptionTracks[TrackIndex].DisplayName = "Caption Track " + TrackIndex + " (Stream " + Index + ")";

					MediaFormat mediaFormat = trackInfo[Index].getFormat();
					if (mediaFormat != null)
					{
						CaptionTracks[TrackIndex].MimeType = mediaFormat.getString(MediaFormat.KEY_MIME);
						CaptionTracks[TrackIndex].Language = mediaFormat.getString(MediaFormat.KEY_LANGUAGE);
					}
					else
					{
						CaptionTracks[TrackIndex].MimeType = "caption/unknown";
						CaptionTracks[TrackIndex].Language = trackInfo[Index].getLanguage();
					}

//					GameActivity.Log.debug(TrackIndex + " (" + Index + ") Caption: Mime=" + CaptionTracks[TrackIndex].MimeType + ", Lang=" + CaptionTracks[TrackIndex].Language);
					TrackIndex++;
				}
			}

			return CaptionTracks;
		}

		CaptionTrackInfo[] CaptionTracks = new CaptionTrackInfo[0];

		return CaptionTracks;
	}

	public VideoTrackInfo[] GetVideoTracks()
	{
		int Width = getVideoWidth();
		int Height = getVideoHeight();

		if (NeedTrackInfo && android.os.Build.VERSION.SDK_INT >= 16)
		{
			TrackInfo[] trackInfo = getTrackInfo();
			int CountTracks = 0;
			for (int Index=0; Index < trackInfo.length; Index++)
			{
				if (trackInfo[Index].getTrackType() == TrackInfo.MEDIA_TRACK_TYPE_VIDEO)
				{
					CountTracks++;
				}
			}

			VideoTrackInfo[] VideoTracks = new VideoTrackInfo[CountTracks];
			int TrackIndex = 0;
			for (int Index=0; Index < trackInfo.length; Index++)
			{
				if (trackInfo[Index].getTrackType() == TrackInfo.MEDIA_TRACK_TYPE_VIDEO)
				{
					VideoTracks[TrackIndex] = new VideoTrackInfo();
					VideoTracks[TrackIndex].Index = Index;
					VideoTracks[TrackIndex].DisplayName = "Video Track " + TrackIndex + " (Stream " + Index + ")";
					VideoTracks[TrackIndex].Language = trackInfo[Index].getLanguage();
					VideoTracks[TrackIndex].BitRate = 0;

					boolean formatValid = false;
					if (android.os.Build.VERSION.SDK_INT >= 19)
					{
						MediaFormat mediaFormat = trackInfo[Index].getFormat();
						if (mediaFormat != null)
						{
							formatValid = true;
							VideoTracks[TrackIndex].MimeType = mediaFormat.getString(MediaFormat.KEY_MIME);
							VideoTracks[TrackIndex].Width = Integer.parseInt(mediaFormat.getString(MediaFormat.KEY_WIDTH));
							VideoTracks[TrackIndex].Height = Integer.parseInt(mediaFormat.getString(MediaFormat.KEY_HEIGHT));
							VideoTracks[TrackIndex].FrameRate = mediaFormat.getFloat(MediaFormat.KEY_FRAME_RATE);
						}
					}
					if (!formatValid && videoTracks.size() > 0)
					{
						VideoTrackInfo extractTrack = findVideoTrackIndex(Index);
						if (extractTrack != null)
						{
							formatValid = true;
							VideoTracks[TrackIndex].MimeType = extractTrack.MimeType;
							VideoTracks[TrackIndex].Width = extractTrack.Width;
							VideoTracks[TrackIndex].Height = extractTrack.Height;
							VideoTracks[TrackIndex].FrameRate = extractTrack.FrameRate;
						}
					}
					if (!formatValid)
					{
						VideoTracks[TrackIndex].MimeType = "video/unknown";
						VideoTracks[TrackIndex].Width = Width;
						VideoTracks[TrackIndex].Height = Height;
						VideoTracks[TrackIndex].FrameRate = 30.0f;
					}

//					GameActivity.Log.debug(TrackIndex + " (" + Index + ") Video: Mime=" + VideoTracks[TrackIndex].MimeType + ", Lang=" + VideoTracks[TrackIndex].Language + ", Width=" + VideoTracks[TrackIndex].Width + ", Height=" + VideoTracks[TrackIndex].Height + ", FrameRate=" + VideoTracks[TrackIndex].FrameRate);
					TrackIndex++;
				}
			}

			// If we found any video tracks, return them
			if (VideoTracks.length > 0)
			{
				return VideoTracks;
			}
		}

		// if we have a non-zero width/height, create a fallback video track entry
		if (Width > 0 && Height > 0)
		{
			VideoTrackInfo[] VideoTracks = new VideoTrackInfo[1];

			VideoTracks[0] = new VideoTrackInfo();
			VideoTracks[0].Index = 0;
			VideoTracks[0].MimeType = "video/unknown";
			VideoTracks[0].DisplayName = "Video Track 0 (Stream 0)";
			VideoTracks[0].Language = "und";
			VideoTracks[0].BitRate = 0;
			VideoTracks[0].Width = Width;
			VideoTracks[0].Height = Height;
			VideoTracks[0].FrameRate = 30.0f;

			return VideoTracks;
		}

		VideoTrackInfo[] VideoTracks = new VideoTrackInfo[0];

		return VideoTracks;
	}
}
