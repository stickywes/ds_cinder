#pragma once
#ifndef DS_UI_SPRITE_WEB_H_
#define DS_UI_SPRITE_WEB_H_

#include "ds/ui/sprite/sprite.h"
#include "cinder/app/KeyEvent.h"
#include "cinder/app/MouseEvent.h"

namespace Awesomium {
class WebCore;
class WebView;
}

namespace ds {
namespace web {
class Service;
}

namespace ui {

/**
 * \class ds::ui::Web
 * \brief Display a web page.
 */
class Web : public ds::ui::Sprite 
{
public:
	Web(ds::ui::SpriteEngine &engine, float width = 0.0f, float height = 0.0f);
	~Web();
	void setSizeAll(float width, float height, float depth);
	void updateServer(const ds::UpdateParams &updateParams);
	void drawLocalClient();

	void loadUrl(const std::wstring &url);
	void loadUrl(const std::string &url);

	void sendKeyDownEvent(const ci::app::KeyEvent &event);
	void sendKeyUpEvent(const ci::app::KeyEvent &event);
	void sendMouseDownEvent(const ci::app::MouseEvent &event);
	void sendMouseDragEvent(const ci::app::MouseEvent &event);
	void sendMouseUpEvent(const ci::app::MouseEvent &event);

	bool isActive() const;
	void setTransitionTime(const float transitionTime);
	void activate();
	void deactivate();
private:
	void handleTouch(const ds::ui::TouchInfo &touchInfo);

	ds::web::Service&		mService;
	Awesomium::WebView*		mWebViewPtr;
	
	ci::gl::Texture			mWebTexture;
	ci::gl::Texture			mLoadingTexture;

	float					mLoadingAngle;
	bool					mActive;
	float					mTransitionTime;
};

} // namespace ui
} // namespace ds

#endif // DS_UI_SPRITE_WEB_H_