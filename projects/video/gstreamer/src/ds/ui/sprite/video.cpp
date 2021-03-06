#include "ds/ui/sprite/video.h"

#include <cinder/Camera.h>
#include <ds/data/resource_list.h>
#include <ds/debug/debug_defines.h>
#include <ds/debug/logger.h>
#include <ds/math/math_func.h>
#include <ds/ui/sprite/sprite_engine.h>

#include "gstreamer/_2RealGStreamerWrapper.h"
#include "gstreamer/video_meta_cache.h"

using namespace ci;

using namespace _2RealGStreamerWrapper;

static _2RealGStreamerWrapper::GStreamerWrapper* new_movie() {
	_2RealGStreamerWrapper::GStreamerWrapper*	ans = new _2RealGStreamerWrapper::GStreamerWrapper();
	if (!ans) throw std::runtime_error("GStreamer Video can't create mMovie");
	return ans;
}

namespace {
// Previously was using a cache of "gstreamer" but I've changed the format, so let's make a new one
ds::ui::VideoMetaCache		CACHE("gstreamer_2");
}

namespace ds {
namespace ui {

/**
 * \class ds::ui::Video
 */
Video::Video(SpriteEngine& engine)
		: inherited(engine)
		, mMoviePtr(new_movie())
		, mMovie(*mMoviePtr)
		, mLooping(false)
		, mMuted(false)
		, mInternalMuted(true)
		, mVolume(1.0f)
		, mStatusDirty(false)
		, mStatusFn(nullptr)
		, mIsTransparent(true)
		, mGeneratingSingleFrame(false)
		, mFboCreated(false) {
	setUseShaderTextuer(true);
	setTransparent(false);
	setStatus(Status::STATUS_STOPPED);
}

Video::~Video() {
	mMovie.stop();
	mMovie.close();
	delete mMoviePtr;
}

void Video::updateServer(const UpdateParams& up) {
	inherited::updateServer(up);

	if (mStatusDirty) {
		mStatusDirty = false;
		if (mStatusFn) mStatusFn(mStatus);
	}
			
	mMovie.update();
}

void Video::drawLocalClient() {
	if (!mFbo) return;

	if (mMovie.getState() == STOPPED) setStatus(Status::STATUS_STOPPED);
	else if (mMovie.getState() == PLAYING) setStatus(Status::STATUS_PLAYING);
	else setStatus(Status::STATUS_PAUSED);

	if (!inBounds()) {
		if (!mInternalMuted) {
			mMovie.setVolume(0.0f);
			mInternalMuted = true;
		}
		return;
	}

	if (mInternalMuted) {
		mInternalMuted = false;
		setMovieVolume();
	}

	bool gotVideo(false);

	if(mMovie.isNewVideoFrame()){
		unsigned char* pImg = mMovie.getVideo();
		if(pImg != nullptr){	
			gotVideo = true;
			int vidWidth( mMovie.getWidth()), vidHeight(mMovie.getHeight());
			if(mIsTransparent){
				mFrameTexture = ci::gl::Texture(pImg, GL_RGBA, vidWidth, vidHeight);
			} else {
				mFrameTexture = ci::gl::Texture(pImg, GL_RGB, vidWidth, vidHeight);
			}
		}
	}

	if ( mFrameTexture ) {
		{
			ci::gl::pushMatrices();
			mSpriteShader.getShader().unbind();
			ci::gl::setViewport(mFrameTexture.getBounds());
			ci::CameraOrtho camera;
			camera.setOrtho(float(mFrameTexture.getBounds().getX1()), float(mFrameTexture.getBounds().getX2()), float(mFrameTexture.getBounds().getY2()), float(mFrameTexture.getBounds().getY1()), -1.0f, 1.0f);
			ci::gl::setMatrices(camera);
			// bind the framebuffer - now everything we draw will go there
			mFbo.bindFramebuffer();

			glPushAttrib( GL_TRANSFORM_BIT | GL_ENABLE_BIT );
			for (int i = 0; i < 4; ++i) {
				glDisable( GL_CLIP_PLANE0 + i );
			}

			if(mIsTransparent){
				ci::gl::clear(ci::ColorA(0.0f, 0.0f, 0.0f, 0.0f));
			} else {
				ci::gl::clear(ci::Color(0.0f, 0.0f, 0.0f));
			}

			ci::gl::draw(mFrameTexture);

			glPopAttrib();
			mFbo.unbindFramebuffer();
			mSpriteShader.getShader().bind();
			ci::gl::popMatrices();
		}

		Rectf screenRect = mEngine.getScreenRect();
		ci::gl::setViewport(Area((int)screenRect.getX1(), (int)screenRect.getY2(), (int)screenRect.getX2(), (int)screenRect.getY1()));

		if (getPerspective()) {
			Rectf area(0.0f, 0.0f, getWidth(), getHeight());
			ci::gl::draw( mFbo.getTexture(0), area );
		} else {
			Rectf area(0.0f, getHeight(), getWidth(), 0.0f);
			ci::gl::draw( mFbo.getTexture(0), area );
		}
		DS_REPORT_GL_ERRORS();
	}

	if (gotVideo && mGeneratingSingleFrame) {
		unloadVideo();
		mGeneratingSingleFrame = false;
	}
}

void Video::setSize(float width, float height) {
	setScale( width / getWidth(), height / getHeight() );
}

Video& Video::loadVideo(const std::string& filename) {
	if (filename.empty()) {
		DS_LOG_WARNING("Video::loadVideo recieved a blank filename. Cancelling load.");
		return *this;
	}

	VideoMetaCache::Type		type(VideoMetaCache::ERROR_TYPE);
	try {
		int						videoWidth(-1),
								videoHeight(-1);
		double					videoDuration(0.0f);
		CACHE.getValues(filename, type, videoWidth, videoHeight, videoDuration);
		bool					generateVideoBuffer = true;
		if (type == VideoMetaCache::AUDIO_TYPE) {
			generateVideoBuffer = false;
			// Otherwise I am permanently muted
			mInternalMuted = false;
		}
		mMovie.open(filename, generateVideoBuffer, false, mIsTransparent, videoWidth, videoHeight, videoDuration);
		mMovie.setPosition(0);
		if (mLooping) {
			mMovie.setLoopMode(LOOP);
		} else {
			mMovie.setLoopMode(NO_LOOP);
		}
		//mMovie.play();
		setMovieVolume();
		if (type == VideoMetaCache::AUDIO_TYPE) {
			// Otherwise I am permanently muted
			mInternalMuted = false;
		} else {
			mInternalMuted = true;
		}
		mMovie.setVideoCompleteCallback([this](GStreamerWrapper* video){ handleVideoComplete(video);});

		setStatus(Status::STATUS_PLAYING);
#ifdef _DEBUG
	} catch (std::exception const& ex) {
		DS_DBG_CODE(std::cout << "ERROR Video::loadVideo() ex=" << ex.what() << std::endl);
		return *this;
#else
	} catch (std::exception const&) {
		return *this;
#endif
	}

	if (type == VideoMetaCache::VIDEO_TYPE) setupForVideo(filename);
	return *this;
}

Video& Video::setResourceId(const ds::Resource::Id &resourceId) {
	DS_LOG_WARNING("Set resource ID is currentlt not implemented!");
	try {
		ds::Resource            res;
		if (mEngine.getResources().get(resourceId, res)) {
			Sprite::setSizeAll(res.getWidth(), res.getHeight(), mDepth);
			std::string filename = res.getAbsoluteFilePath();
			loadVideo(filename);
		}
#ifdef _DEBUG
	} catch (std::exception const& ex) {
		DS_DBG_CODE(std::cout << "ERROR Video::loadVideo() ex=" << ex.what() << std::endl);
		return *this;
#else
	} catch (std::exception const&) {
		return *this;
#endif
	}

	//mFbo = ci::gl::Fbo(static_cast<int>(getWidth()), static_cast<int>(getHeight()), true);
	return *this;
}

void Video::play() {
	mMovie.play();
}

void Video::stop() {
	mMovie.stop();
}

void Video::pause() {
	mMovie.pause();
}

void Video::seek(double t) {
	mMovie.setPosition(static_cast<float>(t));
	//mMovie.setTimePositionInMs(t * mMovie.getDurationInMs());
	//	mMovie.seekToTime(t);
}

double Video::duration() {
	return mMovie.getDurationInMs();
}

bool Video::isPlaying() {
	if(mMovie.getState() == PLAYING){
		return true;
	} else {
		return false;
	}
}

void Video::loop(bool flag) {
	mLooping = flag;
	if(mLooping){
		mMovie.setLoopMode(LOOP);
	} else {
		mMovie.setLoopMode(NO_LOOP);
	}
}

bool Video::isLooping() const {
	return mLooping;
}

void Video::setVolume(float volume) {
	mVolume = volume;
	setMovieVolume();
}
		
void Video::setMute(const bool doMute) {
	mMuted = doMute;
	setMovieVolume();
}

float Video::getVolume() const {
	return mVolume;
}

void Video::setStatusCallback(const std::function<void(const Status&)>& fn) {
	DS_ASSERT_MSG(mEngine.getMode() == mEngine.CLIENTSERVER_MODE, "Currently only works in ClientServer mode, fill in the UDP callbacks if you want to use this otherwise");
	mStatusFn = fn;
}

void Video::setStatus(const int code) {
	if (code == mStatus.mCode) return;

	mStatus.mCode = code;
	mStatusDirty = true;
}

void Video::setMovieVolume() {
	if (mMuted || mInternalMuted) {
		mMovie.setVolume(0.0f);
	} else {
		mMovie.setVolume(mVolume);
	}
}

double Video::currentTime() {
	return mMovie.getPosition();
}

void Video::unloadVideo() {
	mMovie.stop();
	mMovie.close();
}

// set this before loading a video
void Video::setAlphaMode(bool isTransparent) {
	mIsTransparent = isTransparent;
}

void Video::setVideoCompleteCallback(const std::function<void(Video* video)>& func) {
	mVideoCompleteCallback = func;
}

void Video::handleVideoComplete(GStreamerWrapper* wrapper) {
	if (mVideoCompleteCallback) {
		mVideoCompleteCallback(this);
	}
}

void Video::generateSingleFrame(const std::string& filename) {
	mGeneratingSingleFrame = true;
	loadVideo(filename);
	play();
}

void Video::setupForVideo(const std::string& filename) {
	if (mMovie.getWidth() < 1.0f || mMovie.getHeight() < 1.0f) {
		DS_LOG_WARNING("Video is too small to be used or didn't load correctly! " << filename << " " << getWidth() << " " << getHeight());
		return;
	}

	const float					preWidth = getWidth();
	const float					preHeight = getHeight();

	Sprite::setSizeAll(static_cast<float>(mMovie.getWidth()), static_cast<float>(mMovie.getHeight()), mDepth);

	if (getWidth() > 0 && getHeight() > 0) {
		setSize(getWidth() * getScale().x,  getHeight() * getScale().y);
	}

	if(!(mFboCreated && getWidth() == preWidth && getHeight() == preHeight)){
		mFbo = ci::gl::Fbo(static_cast<int>(getWidth()), static_cast<int>(getHeight()), true);
		mFboCreated = true;
	} 
}

} // namespace ui
} // namespace ds
