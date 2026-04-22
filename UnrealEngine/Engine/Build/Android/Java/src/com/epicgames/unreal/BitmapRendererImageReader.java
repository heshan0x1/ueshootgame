// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.unreal;

import android.graphics.ImageFormat;
import android.graphics.SurfaceTexture;
import android.hardware.HardwareBuffer;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.util.Log;
import android.view.Surface;

import java.nio.Buffer;

interface BitmapRenderer
{
    class UpdateParam
    {
        int destTexture;
    }

    // unified Frame Data encapsulating all kinds of outputs
    class FrameData
    {
        public void release()
        {
            if (hardwareBuffer != null)
            {
                hardwareBuffer.close();
                hardwareBuffer = null;
            }

            if (image != null)
            {
                image.close();
                image = null;
            }
        }

        // output of BitmapRendererImageReader, Image/HardwareBuffer
        public Image image = null;
        public HardwareBuffer hardwareBuffer = null;
        public float UScale = 0.0f;
        public float UOffset = 0.0f;
        public float VScale = 0.0f;
        public float VOffset = 0.0f;

        // output of BitmapRendererLegacy, byte array
        public Buffer binaryBuffer = null;
        // output of BitmapRendererLegacy, result of rendering to an external texture
        public boolean frameReady = false;
    }

    boolean isValid();
    boolean resolutionChanged();
    void setSize(int width, int height);
    Surface getSurface();
    SurfaceTexture getSurfaceTexture();
    FrameData updateFrameData(UpdateParam param);
    void release();
}

class BitmapRendererImageReader implements BitmapRenderer, ImageReader.OnImageAvailableListener
{
    private static final String TAG = "BitmapRenderer";
    private ImageReader mImageReader = null;
    private Handler mImageReaderHandler = null;
    private int mTextureWidth;
    private int mTextureHeight;
    private boolean mTextureSizeChanged = true;
    private Surface mSurface;
    private boolean mUseVulkanRenderer;

    public BitmapRendererImageReader(boolean swizzlePixels, boolean isVulkan, int width, int height, int maxQueueLength)
    {
        mUseVulkanRenderer = isVulkan;

        mTextureWidth = width;
        mTextureHeight = height;

        //mImageReader = ImageReader.newInstance(width, 1, ImageFormat.YUV_420_888, maxQueueLength, HardwareBuffer.USAGE_GPU_SAMPLED_IMAGE);
		mImageReader = ImageReader.newInstance(width, height, ImageFormat.PRIVATE, maxQueueLength, HardwareBuffer.USAGE_GPU_SAMPLED_IMAGE);

        mSurface = mImageReader.getSurface();

        mImageReaderHandler = new android.os.Handler(android.os.Looper.getMainLooper());
        mImageReader.setOnImageAvailableListener(this, mImageReaderHandler);
    }

    public void onImageAvailable(ImageReader reader)
    {
        //nativeSignalSurfaceReadEvent(mParentHandle);
    }

    public synchronized FrameData updateFrameData(UpdateParam param)
    {
        if (mImageReader == null)
        {
            // Can't update if there's no surface to update into.
            return null;
        }

        Image image;
        try
        {
            // Get image data from Surface / Queue...
            image = mImageReader.acquireNextImage();
        }
        catch (Exception e)
        {
            Log.d(TAG, String.format("updateFrameData: ImageReader.acquireNextImage() threw: %s", e.toString()));
            return null;
        }

        if (image == null)
        {
            //Log.d(TAG, "updateFrameData requested, but no images queued...");
            return null;
        }

        HardwareBuffer hwBuffer = image.getHardwareBuffer();
        if (hwBuffer == null)
        {
            Log.d(TAG, "updateFrameData: Could not get HardwareBuffer from Image!");
            return null;
        }

        FrameData frameData = new FrameData();
        frameData.image = image;
        frameData.hardwareBuffer = hwBuffer;

        android.graphics.Rect CropRect = frameData.image.getCropRect();
        frameData.UScale = (float)CropRect.width() / mTextureWidth;
        frameData.UOffset = (float)CropRect.left / mTextureWidth;
        frameData.VScale = (float)CropRect.height() / mTextureHeight;
        frameData.VOffset = (float)CropRect.top / mTextureHeight;

		//Log.d(TAG, "updateFrameData: new Image aquired. size: " + CropRect.width() + "x" + CropRect.height() + "; view: " + width + "x" + height);
        return frameData;
    }

    public boolean isValid()
    {
        return mImageReader != null;
    }

    public Surface getSurface()
    {
        return mSurface;
    }

    public SurfaceTexture getSurfaceTexture()
    {
        // TODO
        throw new UnsupportedOperationException();
    }

    public synchronized void setSize(int width, int height)
    {
        if (width != mTextureWidth || height != mTextureHeight)
        {
            mTextureWidth = width;
            mTextureHeight = height;

            // TODO - Reinitialize the image reader

            mTextureSizeChanged = true;
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

    public synchronized void release()
    {
        if (mImageReader != null)
        {
            mImageReader.setOnImageAvailableListener(null, null);
        }

        if (null != mSurface)
        {
            mSurface.release();
            mSurface = null;
        }
        if (mImageReader != null)
        {
            mImageReader.close();
            mImageReader = null;
        }
    }
}
