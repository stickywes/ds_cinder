#pragma once
#ifndef DS_UI_SPRITE_SPRITEENGINE_H_
#define DS_UI_SPRITE_SPRITEENGINE_H_

namespace ds {
class WorkManager;

namespace ui {

/**
 * \class ds::ui::SpriteEngine
 * Interface for the API that is supplied to sprites.
 */
class SpriteEngine {
  public:
    virtual ds::WorkManager     &getWorkManager() = 0;

  protected:
    SpriteEngine()              { }
    virtual ~SpriteEngine()     { }
};

} // namespace ui

} // namespace ds

#endif // DS_APP_ENGINE_H_