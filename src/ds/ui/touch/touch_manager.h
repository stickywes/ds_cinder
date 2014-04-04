#pragma once
#ifndef DS_UI_TOUCH_MANAGER_H
#define DS_UI_TOUCH_MANAGER_H
#include <map>
#include "cinder/app/TouchEvent.h"
#include "cinder/app/MouseEvent.h"
#include "cinder/Color.h"

using namespace ci;
using namespace ci::app;

namespace ds {

class Engine;

namespace ui {

class Sprite;

class TouchManager
{
  public:
    TouchManager(Engine &engine);

    void                        mouseTouchBegin( MouseEvent event, int id );
    void                        mouseTouchMoved( MouseEvent event, int id );
    void                        mouseTouchEnded( MouseEvent event, int id );

    void                        touchesBegin( TouchEvent event );
    void                        touchesMoved( TouchEvent event );
    void                        touchesEnded( TouchEvent event );

    void                        drawTouches() const;
    void                        setTouchColor(const ci::Color &color);

	void                        clearFingers( const std::vector<int> &fingers );

	void						setSpriteForFinger( const int fingerId, ui::Sprite* theSprite );
  private:
    // Utility to get the hit sprite in either the othorganal or perspective root sprites
    Sprite*                     getHit(const ci::Vec3f &point);

	// If the window is stretched, the mouse points will be off. Fix that shit!
	ci::Vec2f					translateMousePoint(const ci::Vec2i);

    Engine &mEngine;

    std::map<int, ui::Sprite *> mFingerDispatcher;
    std::map<int, Vec3f>        mTouchStartPoint;
    std::map<int, Vec3f>        mTouchPreviousPoint;
    ci::Color                   mTouchColor;

	// If system multitouch is on, Cinder will get both mouse and touch events for the first touch.
	// So we track the first touch id to ignore that finger (cause the mouse will count for that)
	int							mIgnoreFirstTouchId;
};

} // namespace ui
} // namespace ds

#endif//DS_UI_TOUCH_MANAGER_H
