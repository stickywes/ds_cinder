#pragma once
#ifndef DS_APP_ENGINE_ENGINEDATA_H_
#define DS_APP_ENGINE_ENGINEDATA_H_

#include <unordered_map>
#include <cinder/Rect.h>
#include "ds/app/event_notifier.h"
#include "ds/app/engine/engine_cfg.h"

namespace ds {
class EngineService;

/**
 * \class ds::EngineData
 * \brief Store all the data for an engine. Primarily a
 * programmer convenience, to hide and group info.
 */
class EngineData {
public:
	EngineData(const ds::cfg::Settings& engine_settings);

	EventNotifier			mNotifier;
	std::unordered_map<std::string, ds::EngineService*>
							mServices;

	// Will stop and delete them.
	void					clearServices();

	ds::EngineCfg			mEngineCfg;

	float					mMinTouchDistance;
	float					mMinTapDistance;
	int						mSwipeQueueSize;
	float					mDoubleTapTime;
	ci::Rectf				mScreenRect;
	ci::Vec2f				mWorldSize;
	float					mFrameRate;
	// The source rect in world bounds and the destination
	// local rect. Together these should obsolete mScreenRect.
	// The dest local rect.
	ci::Rectf				mSrcRect,
							mDstRect;

private:
	EngineData(const EngineData&);
	EngineData&				operator=(const EngineData&);
};

} // namespace ds

#endif // DS_APP_ENGINE_ENGINEDATA_H_
