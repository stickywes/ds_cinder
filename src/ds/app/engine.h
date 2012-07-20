#pragma once
#ifndef DS_APP_ENGINE_H_
#define DS_APP_ENGINE_H_

#include <memory>
#include <unordered_map>
#include "ds/app/app_defs.h"
#include "ds/app/blob_registry.h"
#include "ds/ui/sprite/sprite.h"
#include "ds/params/update_params.h"
#include "ds/params/draw_params.h"
#include <cinder/app/App.h>
#include <cinder/app/TouchEvent.h>
#include <cinder/app/AppBasic.h>
#include "TuioClient.h"
#include "ds/data/resource_list.h"
#include "ds/config/settings.h"
#include "ds/ui/sprite/sprite_engine.h"
#include "ds/ui/touch/touch_manager.h"
#include "ds/ui/tween/tweenline.h"
#include "cinder/Camera.h"
#include "ds/network/zmq_connection.h"
#include "ds/data/raw_data_buffer.h"
#include "cinder/gl/Fbo.h"

using namespace ci;
using namespace ci::app;

namespace ds {
class App;

extern const ds::BitMask	ENGINE_LOG;

/**
 * \class ds::Engine
 * Container and manager for all views.
 */
class Engine : public ui::SpriteEngine {
  public:
    ~Engine();

    virtual void	              update() = 0;
    virtual void                draw() = 0;

    virtual ds::ui::Tweenline  &getTweenline() { return mTweenline; }
    virtual const ds::cfg::Settings
                               &getDebugSettings() { return mDebugSettings; }

    // only valid after setup() is called
    ui::Sprite                 &getRootSprite();
    void                        loadCinderSettings( ci::app::AppBasic::Settings& );
    //called in app setup; loads settings files and what not.
    virtual void                setup(ds::App&);
    virtual void                setupTuio(ds::App&) = 0;

    bool                        isIdling() const;
    void                        startIdling();
    
    // Called during app construction, to register the sprites as blob handlers.
    virtual void                installSprite(const std::function<void(ds::BlobRegistry&)>& asServer,
                                              const std::function<void(ds::BlobRegistry&)>& asClient) = 0;

    virtual ds::sprite_id_t     nextSpriteId();
    virtual void                registerSprite(ds::ui::Sprite&);
    virtual void                unregisterSprite(ds::ui::Sprite&);
    virtual ds::ui::Sprite*     findSprite(const ds::sprite_id_t);

    tuio::Client               &getTuioClient();
    void                        mouseTouchBegin( MouseEvent event, int id );
    void                        mouseTouchMoved( MouseEvent event, int id );
    void                        mouseTouchEnded( MouseEvent event, int id );
    void                        touchesBegin( TouchEvent event );
    void                        touchesMoved( TouchEvent event );
    void                        touchesEnded( TouchEvent event );

    virtual ds::ResourceList   &getResources();
    float                       getMinTouchDistance() const;
    float                       getMinTapDistance() const;
    unsigned                    getSwipeQueueSize() const;
    float                       getDoubleTapTime() const;

    ci::Rectf                   getScreenRect() const;
    float                       getWidth() const;
    float                       getHeight() const;
    float                       getWorldWidth() const;
    float                       getWorldHeight() const;

    void                        setCamera();
  protected:
    Engine(ds::App&, const ds::cfg::Settings&);

    ds::BlobRegistry            mBlobRegistry;
    std::unordered_map<ds::sprite_id_t, ds::ui::Sprite*>
                                mSprites;

    // Conveniences for the subclases
    void	                      updateClient();
    void	                      updateServer();
    void                        drawClient();
    void                        drawServer();

    static const int            NumberOfNetworkThreads;
  private:
    ds::ui::Tweenline           mTweenline;
    ui::Sprite                  mRootSprite;
    // A cache of all the resources in the system
    ResourceList                mResources;
    UpdateParams                mUpdateParams;
    DrawParams                  mDrawParams;
    float                       mLastTime;
    bool                        mIdling;
    float                       mLastTouchTime;
    float                       mIdleTime;

    tuio::Client                mTuio;
    ui::TouchManager            mTouchManager;

    float                       mMinTouchDistance;
    float                       mMinTapDistance;
    int                         mSwipeQueueSize;
    float                       mDoubleTapTime;

    ci::Rectf                   mScreenRect;
    ci::Vec2f                   mWorldSize;
    const ds::cfg::Settings    &mSettings;
    ds::cfg::Settings           mDebugSettings;
    ci::CameraOrtho             mCamera;

    ci::gl::Fbo                 mFbo;
};

} // namespace ds

#endif // DS_APP_ENGINE_H_