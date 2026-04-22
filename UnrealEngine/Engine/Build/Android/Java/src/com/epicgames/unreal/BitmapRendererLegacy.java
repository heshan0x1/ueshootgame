// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.unreal;

import android.graphics.SurfaceTexture;
import android.opengl.*;
import android.util.Log;

/*
	All this internal surface view does is manage the
	offscreen bitmap that the media player decoding can
	render into for eventual extraction to the UE4 buffers.
*/
class BitmapRendererLegacy implements BitmapRenderer, SurfaceTexture.OnFrameAvailableListener
{
    private static final String TAG = "BitmapRenderer";
    private java.nio.Buffer mFrameData = null;
    private int mLastFramePosition = -1;
    private android.graphics.SurfaceTexture mSurfaceTexture = null;
    private int mTextureWidth = -1;
    private int mTextureHeight = -1;
    private android.view.Surface mSurface = null;
    private boolean mFrameAvailable = false;
    private int mTextureID = -1;
    private int mFBO = -1;
    private int mBlitVertexShaderID = -1;
    private int mBlitFragmentShaderID = -1;
    private float[] mTransformMatrix = new float[16];
    private boolean mTriangleVerticesDirty = true;
    private boolean mTextureSizeChanged = true;
    private boolean mUseOwnContext = true;
    private boolean mVulkanRenderer = false;
    private boolean mSwizzlePixels = false;
	private boolean mCorrectUV = false;

    private int GL_TEXTURE_EXTERNAL_OES = 0x8D65;

    private EGLDisplay mEglDisplay;
    private EGLContext mEglContext;
    private EGLSurface mEglSurface;

    private EGLDisplay mSavedDisplay;
    private EGLContext mSavedContext;
    private EGLSurface mSavedSurfaceDraw;
    private EGLSurface mSavedSurfaceRead;

    private boolean mCreatedEGLDisplay = false;

    public BitmapRendererLegacy(boolean swizzlePixels, boolean isVulkan, boolean correctUV)
    {
        mSwizzlePixels = swizzlePixels;
        mVulkanRenderer = isVulkan;
		mCorrectUV = correctUV;

        mEglSurface = EGL14.EGL_NO_SURFACE;
        mEglContext = EGL14.EGL_NO_CONTEXT;
        mUseOwnContext = true;

        if (mVulkanRenderer)
        {
            mSwizzlePixels = true;
        }
        else
        {
            String RendererString = GLES20.glGetString(GLES20.GL_RENDERER);

            // Do not use shared context if Adreno before 400 or on older Android than Marshmallow
            if (RendererString.contains("Adreno (TM) "))
            {
                java.util.regex.Pattern pattern = java.util.regex.Pattern.compile("Adreno \\(TM\\) (\\d*)");
                java.util.regex.Matcher matcher = pattern.matcher(RendererString);
                if (matcher.find() && matcher.groupCount() >= 1)
                {
                    int AdrenoVersion = Integer.parseInt(matcher.group(1));
                    if (AdrenoVersion < 400 || android.os.Build.VERSION.SDK_INT < 22)
                    {
                        Log.d(TAG, "MediaPlayer14: disabled shared GL context on " + RendererString);
                        mUseOwnContext = false;
                    }
                }
            }
        }

        if (mUseOwnContext)
        {
            initContext();
            saveContext();
            makeCurrent();
            initSurfaceTexture();
            restoreContext();
        }
        else
        {
            initSurfaceTexture();
        }
    }

    private void initContext()
    {
        mEglDisplay = EGL14.EGL_NO_DISPLAY;
        EGLContext shareContext = EGL14.EGL_NO_CONTEXT;

        int majorver[] = new int[] { 0 };
        int minorver[] = new int[] { 0 };
        if (!mVulkanRenderer)
        {
            mEglDisplay = EGL14.eglGetCurrentDisplay();
            shareContext = EGL14.eglGetCurrentContext();

            if (android.os.Build.VERSION.SDK_INT >= 18 &&
                    EGL14.eglQueryContext(mEglDisplay, shareContext, EGLExt.EGL_CONTEXT_MAJOR_VERSION_KHR, majorver, 0) &&
                    EGL14.eglQueryContext(mEglDisplay, shareContext, EGLExt.EGL_CONTEXT_MINOR_VERSION_KHR, minorver, 0))
            {
                Log.d(TAG, "MediaPlayer14: Existing GL context is version " + majorver[0] + "." + minorver[0]);
            }
            else
                // on some devices eg Galaxy S6, the above fails but we do get EGL14.EGL_CONTEXT_CLIENT_VERSION=3
                if (EGL14.eglQueryContext(mEglDisplay, shareContext, EGL14.EGL_CONTEXT_CLIENT_VERSION, majorver, 0))
                {
                    Log.d(TAG, "MediaPlayer14: Existing GL context is version " + majorver[0]);
                }
                else
                {
                    Log.d(TAG, "MediaPlayer14: Existing GL context version not detected");
                }
        }
        else
        {
            mEglDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
            if (mEglDisplay == EGL14.EGL_NO_DISPLAY)
            {
                Log.d(TAG, "unable to get EGL14 display");
                return;
            }
            int[] version = new int[2];
            if (!EGL14.eglInitialize(mEglDisplay, version, 0, version, 1))
            {
                mEglDisplay = null;
                Log.d(TAG, "unable to initialize EGL14 display");
                return;
            }

            mCreatedEGLDisplay = true;
        }

        int[] configSpec = new int[]
                {
                        EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                        EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT,
                        EGL14.EGL_NONE
                };
        EGLConfig[] configs = new EGLConfig[1];
        int[] num_config = new int[1];
        EGL14.eglChooseConfig(mEglDisplay, configSpec, 0, configs, 0, 1, num_config, 0);
        int[] contextAttribsES2 = new int[]
                {
                        EGL14.EGL_CONTEXT_CLIENT_VERSION, 2,
                        EGL14.EGL_NONE
                };
        int[] contextAttribsES31 = new int[]
                {
                        EGLExt.EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
                        EGLExt.EGL_CONTEXT_MINOR_VERSION_KHR, 1,
                        EGL14.EGL_NONE
                };
        mEglContext = EGL14.eglCreateContext(mEglDisplay, configs[0], shareContext, majorver[0]==3 ? contextAttribsES31 : contextAttribsES2, 0);

        if (EGL14.eglQueryString(mEglDisplay, EGL14.EGL_EXTENSIONS).contains("EGL_KHR_surfaceless_context"))
        {
            mEglSurface = EGL14.EGL_NO_SURFACE;
        }
        else
        {
            int[] pbufferAttribs = new int[]
                    {
                            EGL14.EGL_NONE
                    };
            mEglSurface = EGL14.eglCreatePbufferSurface(mEglDisplay, configs[0], pbufferAttribs, 0);
        }
    }

    private void saveContext()
    {
        mSavedDisplay = EGL14.eglGetCurrentDisplay();
        mSavedContext = EGL14.eglGetCurrentContext();
        mSavedSurfaceDraw = EGL14.eglGetCurrentSurface(EGL14.EGL_DRAW);
        mSavedSurfaceRead = EGL14.eglGetCurrentSurface(EGL14.EGL_READ);
    }

    private void makeCurrent()
    {
        EGL14.eglMakeCurrent(mEglDisplay, mEglSurface, mEglSurface, mEglContext);
    }

    private void restoreContext()
    {
        EGL14.eglMakeCurrent(mSavedDisplay, mSavedSurfaceDraw, mSavedSurfaceRead, mSavedContext);
    }

    private void initSurfaceTexture()
    {
        int[] textures = new int[1];
        GLES20.glGenTextures(1, textures, 0);
        mTextureID = textures[0];
        if (mTextureID <= 0)
        {
            Log.e(TAG, "mTextureID <= 0");
            release();
            return;
        }
        mSurfaceTexture = new android.graphics.SurfaceTexture(mTextureID);
        mSurfaceTexture.setOnFrameAvailableListener(this);
        mSurface = new android.view.Surface(mSurfaceTexture);

        int[] glInt = new int[1];

        GLES20.glGenFramebuffers(1,glInt,0);
        mFBO = glInt[0];
        if (mFBO <= 0)
        {
            Log.e(TAG, "mFBO <= 0");
            release();
            return;
        }

        // Special shaders for blit of movie texture.
        mBlitVertexShaderID = createShader(GLES20.GL_VERTEX_SHADER, mBlitVextexShader);
        if (mBlitVertexShaderID == 0)
        {
            Log.e(TAG, "mBlitVertexShaderID == 0");
            release();
            return;
        }
        int mBlitFragmentShaderID = createShader(GLES20.GL_FRAGMENT_SHADER,
                mSwizzlePixels ? mBlitFragmentShaderBGRA : mBlitFragmentShaderRGBA);
        if (mBlitFragmentShaderID == 0)
        {
            Log.e(TAG, "mBlitFragmentShaderID == 0");
            release();
            return;
        }
        mProgram = GLES20.glCreateProgram();
        if (mProgram <= 0)
        {
            Log.e(TAG, "mProgram <= 0");
            release();
            return;
        }
        GLES20.glAttachShader(mProgram, mBlitVertexShaderID);
        GLES20.glAttachShader(mProgram, mBlitFragmentShaderID);
        GLES20.glLinkProgram(mProgram);
        int[] linkStatus = new int[1];
        GLES20.glGetProgramiv(mProgram, GLES20.GL_LINK_STATUS, linkStatus, 0);
        if (linkStatus[0] != GLES20.GL_TRUE)
        {
            Log.e(TAG, "Could not link program: ");
            Log.e(TAG, GLES20.glGetProgramInfoLog(mProgram));
            GLES20.glDeleteProgram(mProgram);
            mProgram = 0;
            release();
            return;
        }
        mPositionAttrib = GLES20.glGetAttribLocation(mProgram, "Position");
        mTexCoordsAttrib = GLES20.glGetAttribLocation(mProgram, "TexCoords");
        mTextureUniform = GLES20.glGetUniformLocation(mProgram, "VideoTexture");

        GLES20.glGenBuffers(1,glInt,0);
        mBlitBuffer = glInt[0];
        if (mBlitBuffer <= 0)
        {
            Log.e(TAG, "mBlitBuffer <= 0");
            release();
            return;
        }

        // Create blit mesh.
        mTriangleVertices = java.nio.ByteBuffer.allocateDirect(
                        mTriangleVerticesData.length * FLOAT_SIZE_BYTES)
                .order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer();
        mTriangleVerticesDirty = true;

        // Set up GL state
        if (mUseOwnContext)
        {
            GLES20.glDisable(GLES20.GL_BLEND);
            GLES20.glDisable(GLES20.GL_CULL_FACE);
            GLES20.glDisable(GLES20.GL_SCISSOR_TEST);
            GLES20.glDisable(GLES20.GL_STENCIL_TEST);
            GLES20.glDisable(GLES20.GL_DEPTH_TEST);
            GLES20.glDisable(GLES20.GL_DITHER);
            GLES20.glColorMask(true,true,true,true);
        }
    }

    private void UpdateVertexData()
    {
        if (!mTriangleVerticesDirty || mBlitBuffer <= 0)
        {
            return;
        }

        // fill it in
        mTriangleVertices.position(0);
        mTriangleVertices.put(mTriangleVerticesData).position(0);

        // save VBO state
        int[] glInt = new int[1];
        GLES20.glGetIntegerv(GLES20.GL_ARRAY_BUFFER_BINDING, glInt, 0);
        int previousVBO = glInt[0];

        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, mBlitBuffer);
        GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER,
                mTriangleVerticesData.length*FLOAT_SIZE_BYTES,
                mTriangleVertices, GLES20.GL_STATIC_DRAW);

        // restore VBO state
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, previousVBO);

        mTriangleVerticesDirty = false;
    }

    public boolean isValid()
    {
        return mSurfaceTexture != null;
    }

    private int createShader(int shaderType, String source)
    {
        int shader = GLES20.glCreateShader(shaderType);
        if (shader != 0)
        {
            GLES20.glShaderSource(shader, source);
            GLES20.glCompileShader(shader);
            int[] compiled = new int[1];
            GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, compiled, 0);
            if (compiled[0] == 0)
            {
                Log.e(TAG, "Could not compile shader " + shaderType + ":");
                Log.e(TAG, GLES20.glGetShaderInfoLog(shader));
                GLES20.glDeleteShader(shader);
                shader = 0;
            }
        }
        return shader;
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
                mFrameData = null;
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

    private static final int FLOAT_SIZE_BYTES = 4;
    private static final int TRIANGLE_VERTICES_DATA_STRIDE_BYTES = 4 * FLOAT_SIZE_BYTES;
    private static final int TRIANGLE_VERTICES_DATA_POS_OFFSET = 0;
    private static final int TRIANGLE_VERTICES_DATA_UV_OFFSET = 2;
    private float[] mTriangleVerticesData = {
            // X, Y, U, V
            -1.0f, -1.0f, 0.f, 0.f,
            1.0f, -1.0f, 1.f, 0.f,
            -1.0f, 1.0f, 0.f, 1.f,
            1.0f, 1.0f, 1.f, 1.f,
    };

    private java.nio.FloatBuffer mTriangleVertices;

    private final String mBlitVextexShader =
            "attribute vec2 Position;\n" +
                    "attribute vec2 TexCoords;\n" +
                    "varying vec2 TexCoord;\n" +
                    "void main() {\n" +
                    "	TexCoord = TexCoords;\n" +
                    "	gl_Position = vec4(Position, 0.0, 1.0);\n" +
                    "}\n";

    // NOTE: We read the fragment as BGRA so that in the end, when
    // we glReadPixels out of the FBO, we get them in that order
    // and avoid having to swizzle the pixels in the CPU.
    private final String mBlitFragmentShaderBGRA =
            "#extension GL_OES_EGL_image_external : require\n" +
                    "uniform samplerExternalOES VideoTexture;\n" +
                    "varying highp vec2 TexCoord;\n" +
                    "void main()\n" +
                    "{\n" +
                    "	gl_FragColor = texture2D(VideoTexture, TexCoord).bgra;\n" +
                    "}\n";
    private final String mBlitFragmentShaderRGBA =
            "#extension GL_OES_EGL_image_external : require\n" +
                    "uniform samplerExternalOES VideoTexture;\n" +
                    "varying highp vec2 TexCoord;\n" +
                    "void main()\n" +
                    "{\n" +
                    "	gl_FragColor = texture2D(VideoTexture, TexCoord).rgba;\n" +
                    "}\n";

    private int mProgram;
    private int mPositionAttrib;
    private int mTexCoordsAttrib;
    private int mBlitBuffer;
    private int mTextureUniform;

    public synchronized FrameData updateFrameData(UpdateParam param)
    {
        if (param.destTexture != 0)
        {
            FrameData frameData = new FrameData();
            frameData.binaryBuffer = null;
            frameData.frameReady = copyFrameTexture(param.destTexture, null);
            return frameData;
        }
        else
        {
            if (null == mFrameData && mTextureWidth > 0 && mTextureHeight > 0)
            {
                mFrameData = java.nio.ByteBuffer.allocateDirect(mTextureWidth*mTextureHeight*4);
            }
            // Copy surface texture to frame data.
            if (!copyFrameTexture(0, mFrameData))
            {
                return null;
            }

            FrameData frameData = new FrameData();
            frameData.binaryBuffer = mFrameData;
            frameData.frameReady = true;
            return frameData;
        }
    }

    public native void nativeClearCachedAttributeState(int PositionAttrib, int TexCoordsAttrib);

    // Copy the surface texture to another texture, or to raw data.
    // Note: copying to raw data creates a temporary FBO texture.
    private boolean copyFrameTexture(int destTexture, java.nio.Buffer destData)
    {
        if (!mFrameAvailable)
        {
            // We only return fresh data when we generate it. At other
            // time we return nothing to indicate that there was nothing
            // new to return. The media player deals with this by keeping
            // the last frame around and using that for rendering.
            return false;
        }

        mFrameAvailable = false;

        if (null == mSurfaceTexture)
        {
            // Can't update if there's no surface to update into.
            return false;
        }

        int[] glInt = new int[1];
        boolean[] glBool = new boolean[1];

        // Either use own context or save states
        boolean previousBlend=false, previousCullFace=false, previousScissorTest=false, previousStencilTest=false, previousDepthTest=false, previousDither=false;
        int previousFBO=0, previousVBO=0, previousMinFilter=0, previousMagFilter=0;
        int[] previousViewport = new int[4];
        if (mUseOwnContext)
        {
            // Received reports of these not being preserved when changing contexts
            GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
            GLES20.glGetTexParameteriv(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, glInt, 0);
            previousMinFilter = glInt[0];
            GLES20.glGetTexParameteriv(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, glInt, 0);
            previousMagFilter = glInt[0];

            saveContext();
            makeCurrent();
        }
        else
        {
            // Clear gl errors as they can creep in from the UE4 renderer.
            GLES20.glGetError();

            previousBlend = GLES20.glIsEnabled(GLES20.GL_BLEND);
            previousCullFace = GLES20.glIsEnabled(GLES20.GL_CULL_FACE);
            previousScissorTest = GLES20.glIsEnabled(GLES20.GL_SCISSOR_TEST);
            previousStencilTest = GLES20.glIsEnabled(GLES20.GL_STENCIL_TEST);
            previousDepthTest = GLES20.glIsEnabled(GLES20.GL_DEPTH_TEST);
            previousDither = GLES20.glIsEnabled(GLES20.GL_DITHER);
            GLES20.glGetIntegerv(GLES20.GL_FRAMEBUFFER_BINDING, glInt, 0);
            previousFBO = glInt[0];
            GLES20.glGetIntegerv(GLES20.GL_ARRAY_BUFFER_BINDING, glInt, 0);
            previousVBO = glInt[0];
            GLES20.glGetIntegerv(GLES20.GL_VIEWPORT, previousViewport, 0);

            GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
            GLES20.glGetTexParameteriv(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, glInt, 0);
            previousMinFilter = glInt[0];
            GLES20.glGetTexParameteriv(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, glInt, 0);
            previousMagFilter = glInt[0];

            glVerify("save state");
        }

        // Get the latest video texture frame.
        mSurfaceTexture.updateTexImage();

        mSurfaceTexture.getTransformMatrix(mTransformMatrix);

		if (mCorrectUV)
		{
			float UMin = mTransformMatrix[12];
			float UMax = UMin + mTransformMatrix[0];
			float VMin = mTransformMatrix[13];
			float VMax = VMin + mTransformMatrix[5];

			if (mTriangleVerticesData[2] != UMin ||
					mTriangleVerticesData[6] != UMax ||
					mTriangleVerticesData[11] != VMin ||
					mTriangleVerticesData[3] != VMax)
			{
				//GameActivity.Log.debug("Matrix:");
				//GameActivity.Log.debug(mTransformMatrix[0] + " " + mTransformMatrix[1] + " " + mTransformMatrix[2] + " " + mTransformMatrix[3]);
				//GameActivity.Log.debug(mTransformMatrix[4] + " " + mTransformMatrix[5] + " " + mTransformMatrix[6] + " " + mTransformMatrix[7]);
				//GameActivity.Log.debug(mTransformMatrix[8] + " " + mTransformMatrix[9] + " " + mTransformMatrix[10] + " " + mTransformMatrix[11]);
				//GameActivity.Log.debug(mTransformMatrix[12] + " " + mTransformMatrix[13] + " " + mTransformMatrix[14] + " " + mTransformMatrix[15]);
				mTriangleVerticesData[ 2] = mTriangleVerticesData[10] = UMin;
				mTriangleVerticesData[ 6] = mTriangleVerticesData[14] = UMax;
				mTriangleVerticesData[11] = mTriangleVerticesData[15] = VMin;
				mTriangleVerticesData[ 3] = mTriangleVerticesData[ 7] = VMax;
				mTriangleVerticesDirty = true;
				//GameActivity.Log.debug("U = " + mTriangleVerticesData[2] + ", " + mTriangleVerticesData[6]);
				//GameActivity.Log.debug("V = " + mTriangleVerticesData[11] + ", " + mTriangleVerticesData[3]);
			}
		}

        if (null != destData)
        {
            // Rewind data so that we can write to it.
            destData.position(0);
        }

        if (!mUseOwnContext)
        {
            GLES20.glDisable(GLES20.GL_BLEND);
            GLES20.glDisable(GLES20.GL_CULL_FACE);
            GLES20.glDisable(GLES20.GL_SCISSOR_TEST);
            GLES20.glDisable(GLES20.GL_STENCIL_TEST);
            GLES20.glDisable(GLES20.GL_DEPTH_TEST);
            GLES20.glDisable(GLES20.GL_DITHER);
            GLES20.glColorMask(true,true,true,true);

            glVerify("reset state");
        }

        GLES20.glViewport(0, 0, mTextureWidth, mTextureHeight);

        glVerify("set viewport");

        // Set-up FBO target texture..
        int FBOTextureID = 0;
        if (null != destData)
        {
            // Create temporary FBO for data copy.
            GLES20.glGenTextures(1,glInt,0);
            FBOTextureID = glInt[0];
        }
        else
        {
            // Use the given texture as the FBO.
            FBOTextureID = destTexture;
        }
        // Set the FBO to draw into the texture one-to-one.
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, FBOTextureID);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D,
                GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_NEAREST);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D,
                GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_NEAREST);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D,
                GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D,
                GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);
        // Create the temp FBO data if needed.
        if (null != destData)
        {
            //int w = 1<<(32-Integer.numberOfLeadingZeros(mTextureWidth-1));
            //int h = 1<<(32-Integer.numberOfLeadingZeros(mTextureHeight-1));
            GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0,
                    GLES20.GL_RGBA,
                    mTextureWidth, mTextureHeight,
                    0, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, null);
        }

        glVerify("set-up FBO texture");

        // Set to render to the FBO.
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, mFBO);

        GLES20.glFramebufferTexture2D(
                GLES20.GL_FRAMEBUFFER,
                GLES20.GL_COLOR_ATTACHMENT0,
                GLES20.GL_TEXTURE_2D, FBOTextureID, 0);

        // check status
        int status = GLES20.glCheckFramebufferStatus(GLES20.GL_FRAMEBUFFER);
        if (status != GLES20.GL_FRAMEBUFFER_COMPLETE)
        {
            Log.e(TAG, "Failed to complete framebuffer attachment ("+status+")");
        }

        // The special shaders to render from the video texture.
        GLES20.glUseProgram(mProgram);

        // Set the mesh that renders the video texture.
        UpdateVertexData();
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, mBlitBuffer);
        GLES20.glEnableVertexAttribArray(mPositionAttrib);
        GLES20.glVertexAttribPointer(mPositionAttrib, 2, GLES20.GL_FLOAT, false,
                TRIANGLE_VERTICES_DATA_STRIDE_BYTES, 0);
        GLES20.glEnableVertexAttribArray(mTexCoordsAttrib);
        GLES20.glVertexAttribPointer(mTexCoordsAttrib, 2, GLES20.GL_FLOAT, false,
                TRIANGLE_VERTICES_DATA_STRIDE_BYTES,
                TRIANGLE_VERTICES_DATA_UV_OFFSET*FLOAT_SIZE_BYTES);

        glVerify("setup movie texture read");

        GLES20.glClear( GLES20.GL_COLOR_BUFFER_BIT);

        // connect 'VideoTexture' to video source texture (mTextureID).
        // mTextureID is bound to GL_TEXTURE_EXTERNAL_OES in updateTexImage
        GLES20.glUniform1i(mTextureUniform, 0);
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glBindTexture(GL_TEXTURE_EXTERNAL_OES, mTextureID);

        // Draw the video texture mesh.
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);

        GLES20.glFlush();

        // Read the FBO texture pixels into raw data.
        if (null != destData)
        {
            GLES20.glReadPixels(
                    0, 0, mTextureWidth, mTextureHeight,
                    GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE,
                    destData);
        }

        glVerify("draw & read movie texture");

        // Restore state and cleanup.
        if (mUseOwnContext)
        {
            GLES20.glFramebufferTexture2D(
                    GLES20.GL_FRAMEBUFFER,
                    GLES20.GL_COLOR_ATTACHMENT0,
                    GLES20.GL_TEXTURE_2D, 0, 0);

            if (null != destData && FBOTextureID > 0)
            {
                glInt[0] = FBOTextureID;
                GLES20.glDeleteTextures(1, glInt, 0);
            }

            restoreContext();

            // Restore previous texture filtering
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, previousMinFilter);
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, previousMagFilter);
        }
        else
        {
            GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, previousFBO);
            if (null != destData && FBOTextureID > 0)
            {
                glInt[0] = FBOTextureID;
                GLES20.glDeleteTextures(1, glInt, 0);
            }
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, previousVBO);

            GLES20.glViewport(previousViewport[0], previousViewport[1],	previousViewport[2], previousViewport[3]);
            if (previousBlend) GLES20.glEnable(GLES20.GL_BLEND);
            if (previousCullFace) GLES20.glEnable(GLES20.GL_CULL_FACE);
            if (previousScissorTest) GLES20.glEnable(GLES20.GL_SCISSOR_TEST);
            if (previousStencilTest) GLES20.glEnable(GLES20.GL_STENCIL_TEST);
            if (previousDepthTest) GLES20.glEnable(GLES20.GL_DEPTH_TEST);
            if (previousDither) GLES20.glEnable(GLES20.GL_DITHER);

            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, previousMinFilter);
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, previousMagFilter);

            // invalidate cached state in RHI
            GLES20.glDisableVertexAttribArray(mPositionAttrib);
            GLES20.glDisableVertexAttribArray(mTexCoordsAttrib);
            nativeClearCachedAttributeState(mPositionAttrib, mTexCoordsAttrib);
        }

        return true;
    }

    private void showGlError(String op, int error)
    {
        switch (error)
        {
            case GLES20.GL_INVALID_ENUM:						Log.e(TAG, "MediaPlayer$BitmapRenderer: " + op + ": glGetError GL_INVALID_ENUM");  break;
            case GLES20.GL_INVALID_OPERATION:					Log.e(TAG, "MediaPlayer$BitmapRenderer: " + op + ": glGetError GL_INVALID_OPERATION");  break;
            case GLES20.GL_INVALID_FRAMEBUFFER_OPERATION:		Log.e(TAG, "MediaPlayer$BitmapRenderer: " + op + ": glGetError GL_INVALID_FRAMEBUFFER_OPERATION");  break;
            case GLES20.GL_INVALID_VALUE:						Log.e(TAG, "MediaPlayer$BitmapRenderer: " + op + ": glGetError GL_INVALID_VALUE");  break;
            case GLES20.GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:	Log.e(TAG, "MediaPlayer$BitmapRenderer: " + op + ": glGetError GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT");  break;
            case GLES20.GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:	Log.e(TAG, "MediaPlayer$BitmapRenderer: " + op + ": glGetError GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS");  break;
            case GLES20.GL_FRAMEBUFFER_UNSUPPORTED:				Log.e(TAG, "MediaPlayer$BitmapRenderer: " + op + ": glGetError GL_FRAMEBUFFER_UNSUPPORTED");  break;
            case GLES20.GL_OUT_OF_MEMORY:						Log.e(TAG, "MediaPlayer$BitmapRenderer: " + op + ": glGetError GL_OUT_OF_MEMORY");  break;
            default:											Log.e(TAG, "MediaPlayer$BitmapRenderer: " + op + ": glGetError " + error);
        }
    }

    private void glVerify(String op)
    {
        int error;
        while ((error = GLES20.glGetError()) != GLES20.GL_NO_ERROR)
        {
            showGlError(op, error);
            throw new RuntimeException(op + ": glGetError " + error);
        }
    }

    private void glWarn(String op)
    {
        int error;
        while ((error = GLES20.glGetError()) != GLES20.GL_NO_ERROR)
        {
            showGlError(op, error);
        }
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
        int[] glInt = new int[1];
        if (mBlitBuffer > 0)
        {
            glInt[0] = mBlitBuffer;
            GLES20.glDeleteBuffers(1,glInt,0);
            mBlitBuffer = -1;
        }
        if (mProgram > 0)
        {
            GLES20.glDeleteProgram(mProgram);
            mProgram = -1;
        }
        if (mBlitVertexShaderID > 0)
        {
            GLES20.glDeleteShader(mBlitVertexShaderID);
            mBlitVertexShaderID = -1;
        }
        if (mBlitFragmentShaderID > 0)
        {
            GLES20.glDeleteShader(mBlitFragmentShaderID);
            mBlitFragmentShaderID = -1;
        }
        if (mFBO > 0)
        {
            glInt[0] = mFBO;
            GLES20.glDeleteFramebuffers(1,glInt,0);
            mFBO = -1;
        }
        if (mTextureID > 0)
        {
            glInt[0] = mTextureID;
            GLES20.glDeleteTextures(1,glInt,0);
            mTextureID = -1;
        }
        if (mEglSurface != EGL14.EGL_NO_SURFACE)
        {
            EGL14.eglDestroySurface(mEglDisplay, mEglSurface);
            mEglSurface = EGL14.EGL_NO_SURFACE;
        }
        if (mEglContext != EGL14.EGL_NO_CONTEXT)
        {
            EGL14.eglDestroyContext(mEglDisplay, mEglContext);
            mEglContext = EGL14.EGL_NO_CONTEXT;
        }
        if (mCreatedEGLDisplay)
        {
            EGL14.eglTerminate(mEglDisplay);
            mEglDisplay = EGL14.EGL_NO_DISPLAY;
            mCreatedEGLDisplay = false;
        }
    }
}