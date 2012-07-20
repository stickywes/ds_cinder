#include "sprite.h"
#include "cinder/gl/gl.h"
#include "gl/GL.h"
#include "ds/app/blob_reader.h"
#include "ds/app/blob_registry.h"
#include "ds/data/data_buffer.h"
#include "ds/debug/logger.h"
#include "ds/math/math_defs.h"
#include "sprite_engine.h"
#include "ds/math/math_func.h"
#include "cinder/Camera.h"
#include "ds/math/random.h"
#include "ds/app/environment.h"
#include "ds/util/string_util.h"

using namespace ci;

namespace {

const std::string DefaultBaseFrag = 
"uniform sampler2D tex0;\n"
"void main()\n"
"{\n"
"    vec4 acolor = texture2D( tex0, gl_TexCoord[0].st );\n"
"    acolor *= gl_Color;\n"
"    gl_FragColor = acolor;\n"
"}\n";

const std::string DefaultBaseVert = 
"void main()\n"
"{\n"
"  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"  gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;\n"
"  gl_TexCoord[0] = gl_TextureMatrix[0] * gl_MultiTexCoord0;\n"
"  gl_FrontColor = gl_Color;\n"
"}\n";

}

#pragma warning (disable : 4355)    // disable 'this': used in base member initializer list

using namespace ci;

namespace ds {
namespace ui {

const char          SPRITE_ID_ATTRIBUTE = 1;

namespace {
char                BLOB_TYPE         = 0;

const DirtyState    ID_DIRTY 			    = newUniqueDirtyState();
const DirtyState    PARENT_DIRTY 			= newUniqueDirtyState();
const DirtyState    CHILD_DIRTY 			= newUniqueDirtyState();
const DirtyState    FLAGS_DIRTY 	   	= newUniqueDirtyState();
const DirtyState    SIZE_DIRTY 	    	= newUniqueDirtyState();
const DirtyState    POSITION_DIRTY 		= newUniqueDirtyState();
const DirtyState    SCALE_DIRTY 	  	= newUniqueDirtyState();
const DirtyState    COLOR_DIRTY 	  	= newUniqueDirtyState();
const DirtyState    OPACITY_DIRTY 	  = newUniqueDirtyState();

const char          PARENT_ATT        = 2;
const char          SIZE_ATT          = 3;
const char          FLAGS_ATT         = 4;
const char          POSITION_ATT      = 5;
const char          SCALE_ATT         = 6;
const char          COLOR_ATT         = 7;
const char          OPACITY_ATT       = 8;

// flags
const int           VISIBLE_F         = (1<<0);
const int           TRANSPARENT_F     = (1<<1);
const int           ENABLED_F         = (1<<2);
const int           DRAW_SORTED_F     = (1<<3);

const ds::BitMask   SPRITE_LOG        = ds::Logger::newModule("sprite");
}

void Sprite::installAsServer(ds::BlobRegistry& registry)
{
  BLOB_TYPE = registry.add([](BlobReader& r) {Sprite::handleBlobFromClient(r);});
}

void Sprite::installAsClient(ds::BlobRegistry& registry)
{
  BLOB_TYPE = registry.add([](BlobReader& r) {Sprite::handleBlobFromServer<Sprite>(r);});
}

void Sprite::handleBlobFromClient(ds::BlobReader& r)
{
  ds::DataBuffer&       buf(r.mDataBuffer);
  if (buf.read<char>() != SPRITE_ID_ATTRIBUTE) return;
  ds::sprite_id_t       id = buf.read<ds::sprite_id_t>();
  Sprite*               s = r.mSpriteEngine.findSprite(id);
  if (s) s->readFrom(r);
}

Sprite::Sprite( SpriteEngine& engine, float width /*= 0.0f*/, float height /*= 0.0f*/ )
    : mEngine(engine)
    , mId(ds::EMPTY_SPRITE_ID)
    , mWidth(width)
    , mHeight(height)
    , mTouchProcess(engine, *this)
{
  init(mEngine.nextSpriteId());
  setSize(width, height);
}

Sprite::Sprite( SpriteEngine& engine, const ds::sprite_id_t id )
    : mEngine(engine)
    , mId(ds::EMPTY_SPRITE_ID)
    , mTouchProcess(engine, *this)
{
  init(id);
}

void Sprite::init(const ds::sprite_id_t id)
{
  mSpriteFlags = VISIBLE_F | TRANSPARENT_F;
  mWidth = 0;
  mHeight = 0;
  mCenter = ci::Vec3f(0.0f, 0.0f, 0.0f);
  mRotation = ci::Vec3f(0.0f, 0.0f, 0.0f);
  mZLevel = 0.0f;
  mScale = ci::Vec3f(1.0f, 1.0f, 1.0f);
  mUpdateTransform = true;
  mParent = nullptr;
  mOpacity = 1.0f;
  mColor = Color(1.0f, 1.0f, 1.0f);
  mMultiTouchEnabled = false;
  mCheckBounds = false;
  mBoundsNeedChecking = true;
  mInBounds = true;
  mDepth = 1.0f;
  mDragDestination = nullptr;
  mBlobType = BLOB_TYPE;
  mBlendMode = NORMAL;

  setSpriteId(id);

  mServerColor = ColorA(math::random()*0.5 + 0.5f, math::random()*0.5 + 0.5f, math::random()*0.5 + 0.5f, 0.4f);

  mShaderBaseNameVert = Environment::getAppFolder("data/shaders", "base.vert");
  mShaderBaseNameFrag = Environment::getAppFolder("data/shaders", "base.frag");
}

Sprite::~Sprite()
{
    remove();
    setSpriteId(0);
}

void Sprite::updateClient( const UpdateParams &updateParams )
{
  if (mCheckBounds) {
    updateCheckBounds();
  }

    for ( auto it = mChildren.begin(), it2 = mChildren.end(); it != it2; ++it )
    {
        (*it)->updateClient(updateParams);
    }
}

void Sprite::updateServer( const UpdateParams &updateParams )
{
  if (mCheckBounds) {
    updateCheckBounds();
  }

  for ( auto it = mChildren.begin(), it2 = mChildren.end(); it != it2; ++it )
  {
    (*it)->updateServer(updateParams);
  }
}

void Sprite::drawClient( const Matrix44f &trans, const DrawParams &drawParams )
{
    if ((mSpriteFlags&VISIBLE_F) == 0)
        return;

    if (!mShaderBase) {
      loadShaders();
    }

    buildTransform();

    Matrix44f totalTransformation = trans*mTransformation;

    // Sprites really do not deal well with being 0,0 size so for now avoid it
    if ((mSpriteFlags&TRANSPARENT_F) == 0 && mWidth > 0 && mHeight > 0) {
      std::unique_ptr<ci::gl::Fbo> fbo = std::move(mEngine.getFbo(mWidth, mHeight));
      if (!fbo) return;
      {
        gl::SaveFramebufferBinding bindingSaver;
        // bind the framebuffer - now everything we draw will go there
        fbo->bindFramebuffer();

        gl::setViewport(fbo->getBounds());

        ci::CameraOrtho camera;
        camera.setOrtho(0.0f, fbo->getWidth(), fbo->getHeight(), 0.0f, -1.0f, 1.0f);

        gl::pushModelView();
        gl::setMatrices(camera);

        gl::disableAlphaBlending();
        gl::clear( ColorA( 0.0f, 0.0f, 0.0f, 0.0f ) );
        gl::color(mColor.r, mColor.g, mColor.b, mOpacity);

        drawLocalClient();
        gl::popModelView();
      }

      gl::enableAlphaBlending();
      mEngine.setCamera();
      Rectf screen(0.0f, fbo->getHeight(), fbo->getWidth(), 0.0f);
      gl::pushModelView();
      glLoadIdentity();
      gl::multModelView(totalTransformation);
      gl::color(ColorA(1.0f, 1.0f, 1.0f, 1.0f));

      fbo->bindTexture();

      if (mShaderBase) {
        mShaderBase.bind();
        mShaderBase.uniform("tex0", 0);
      }

      gl::drawSolidRect(screen);

      if (mShaderBase) {
        mShaderBase.unbind();
      }

      fbo->unbindTexture();
      //gl::draw( fbo->getTexture(0), screen );
      gl::popModelView();
      mEngine.giveBackFbo(std::move(fbo));
    }

    if ((mSpriteFlags&DRAW_SORTED_F) == 0)
    {
        for ( auto it = mChildren.begin(), it2 = mChildren.end(); it != it2; ++it )
        {
            (*it)->drawClient(totalTransformation, drawParams);
        }
    }
    else
    {
        std::list<Sprite *> mCopy = mChildren;
        mCopy.sort([](Sprite *i, Sprite *j)
        {
            return i->getZLevel() < j->getZLevel();
        });

        for ( auto it = mCopy.begin(), it2 = mCopy.end(); it != it2; ++it )
        {
            (*it)->drawClient(totalTransformation, drawParams);
        }
    }
}

void Sprite::drawServer( const Matrix44f &trans, const DrawParams &drawParams )
{
  if ((mSpriteFlags&VISIBLE_F) == 0)
    return;

  buildTransform();

  Matrix44f totalTransformation = trans*mTransformation;

  glPushMatrix();
  //gl::multModelView(totalTransformation);
  gl::multModelView(totalTransformation);
  gl::color(mServerColor);

  if ((mSpriteFlags&TRANSPARENT_F) == 0 && isEnabled())
    drawLocalServer();

  glPopMatrix();

  if ((mSpriteFlags&DRAW_SORTED_F) == 0)
  {
    for ( auto it = mChildren.begin(), it2 = mChildren.end(); it != it2; ++it )
    {
      (*it)->drawServer(totalTransformation, drawParams);
    }
  }
  else
  {
    std::list<Sprite *> mCopy = mChildren;
    mCopy.sort([](Sprite *i, Sprite *j)
    {
      return i->getZLevel() < j->getZLevel();
    });

    for ( auto it = mCopy.begin(), it2 = mCopy.end(); it != it2; ++it )
    {
      (*it)->drawServer(totalTransformation, drawParams);
    }
  }
}

void Sprite::setPosition( float x, float y, float z )
{
  setPosition(Vec3f(x, y, z));
}

void Sprite::setPosition( const Vec3f &pos )
{
  if (mPosition == pos) return;

  mPosition = pos;
  mUpdateTransform = true;
  mBoundsNeedChecking = true;
	markAsDirty(POSITION_DIRTY);
}

const Vec3f &Sprite::getPosition() const
{
    return mPosition;
}

void Sprite::setScale( float x, float y, float z )
{
  setScale(Vec3f(x, y, z));
}

void Sprite::setScale( const Vec3f &scale )
{
  if (mScale == scale) return;

  mScale = scale;
  mUpdateTransform = true;
  mBoundsNeedChecking = true;
	markAsDirty(SCALE_DIRTY);
}

const Vec3f &Sprite::getScale() const
{
  return mScale;
}

void Sprite::setCenter( float x, float y, float z )
{
  mCenter = Vec3f(x, y, z);
  mUpdateTransform = true;
  mBoundsNeedChecking = true;
}

void Sprite::setCenter( const Vec3f &center )
{
  mCenter = center;
  mUpdateTransform = true;
  mBoundsNeedChecking = true;
}

const Vec3f &Sprite::getCenter() const
{
    return mCenter;
}

void Sprite::setRotation( float rotZ )
{
    if ( math::isEqual(mRotation.z, rotZ) )
        return;

    mRotation.z = rotZ;
    mUpdateTransform = true;
    mBoundsNeedChecking = true;
}

void Sprite::setRotation( const Vec3f &rot )
{
  if ( math::isEqual(mRotation.x, rot.x) && math::isEqual(mRotation.y, rot.y) && math::isEqual(mRotation.z, rot.z) )
    return;

  mRotation = rot;
  mUpdateTransform = true;
  mBoundsNeedChecking = true;
}

Vec3f Sprite::getRotation() const
{
    return mRotation;
}

void Sprite::setZLevel( float zlevel )
{
    mZLevel = zlevel;
}

float Sprite::getZLevel() const
{
    return mZLevel;
}

void Sprite::setDrawSorted( bool drawSorted )
{
  setFlag(DRAW_SORTED_F, drawSorted, FLAGS_DIRTY, mSpriteFlags);
}

bool Sprite::getDrawSorted() const
{
  return getFlag(DRAW_SORTED_F, mSpriteFlags);
}

const Matrix44f &Sprite::getTransform() const
{
    buildTransform();
    return mTransformation;
}

void Sprite::addChild( Sprite &child )
{
    if ( containsChild(&child) )
        return;

    mChildren.push_back(&child);
    child.setParent(this);
}

void Sprite::removeChild( Sprite &child )
{
    if ( !containsChild(&child) )
        return;

    mChildren.remove(&child);
    child.setParent(nullptr);
}

void Sprite::setParent( Sprite *parent )
{
    removeParent();
    mParent = parent;
    if (mParent)
        mParent->addChild(*this);
    markAsDirty(PARENT_DIRTY);
}

void Sprite::removeParent()
{
    if ( mParent )
    {
        mParent->removeChild(*this);
        mParent = nullptr;
        markAsDirty(PARENT_DIRTY);
    }
}

bool Sprite::containsChild( Sprite *child ) const
{
    auto found = std::find(mChildren.begin(), mChildren.end(), child);

    if ( found != mChildren.end() )
        return true;
    return false;
}

void Sprite::clearChildren()
{
    auto tempList = mChildren;
    mChildren.clear();

    for ( auto it = tempList.begin(), it2 = tempList.end(); it != it2; ++it )
    {
    	if ( !(*it) )
            continue;
        (*it)->removeParent();
        (*it)->clearChildren();
        delete *it;
    }
}

void Sprite::buildTransform() const
{
  if (!mUpdateTransform)
    return;

    mUpdateTransform = false;

    mTransformation = Matrix44f::identity();

    mTransformation.setToIdentity();
    mTransformation.translate(Vec3f(mPosition.x, mPosition.y, mPosition.z));
    mTransformation.rotate(Vec3f(1.0f, 0.0f, 0.0f), mRotation.x * math::DEGREE2RADIAN);
    mTransformation.rotate(Vec3f(0.0f, 1.0f, 0.0f), mRotation.y * math::DEGREE2RADIAN);
    mTransformation.rotate(Vec3f(0.0f, 0.0f, 1.0f), mRotation.z * math::DEGREE2RADIAN);
    mTransformation.scale(Vec3f(mScale.x, mScale.y, mScale.z));
    mTransformation.translate(Vec3f(-mCenter.x*mWidth, -mCenter.y*mHeight, -mCenter.z*mDepth));
    //mTransformation.setToIdentity();
    //mTransformation.translate(Vec3f(-mCenter.x*mWidth, -mCenter.y*mHeight, -mCenter.z*mDepth));
    //mTransformation.scale(Vec3f(mScale.x, mScale.y, mScale.z));
    //mTransformation.rotate(Vec3f(0.0f, 0.0f, 1.0f), mRotation.z * math::DEGREE2RADIAN);
    //mTransformation.rotate(Vec3f(0.0f, 1.0f, 0.0f), mRotation.y * math::DEGREE2RADIAN);
    //mTransformation.rotate(Vec3f(1.0f, 0.0f, 0.0f), mRotation.x * math::DEGREE2RADIAN);
    //mTransformation.translate(Vec3f(mPosition.x, mPosition.y, 1.0f));

    mInverseTransform = mTransformation.inverted();
}

void Sprite::remove()
{
    clearChildren();
    removeParent();
}

void Sprite::setSize( float width, float height, float depth )
{
  if (mWidth == width && mHeight == height && mDepth == depth) return;

  mWidth = width;
  mHeight = height;
  mDepth = depth;
  markAsDirty(SIZE_DIRTY);
}

void Sprite::setSize( float width, float height )
{
  setSize(width, height, mDepth);
}

void Sprite::setColor( const Color &color )
{
  if (mColor == color) return;

  mColor = color;
  markAsDirty(COLOR_DIRTY);
}

void Sprite::setColor( float r, float g, float b )
{
  setColor(Color(r, g, b));
}

Color Sprite::getColor() const
{
    return mColor;
}

void Sprite::setOpacity( float opacity )
{
  if (mOpacity == opacity) return;

  mOpacity = opacity;
  markAsDirty(OPACITY_DIRTY);
}

float Sprite::getOpacity() const
{
    return mOpacity;
}

void Sprite::drawLocalClient()
{
    //glBegin(GL_QUADS);
    //gl::vertex( 0 , 0 );
    //gl::vertex( mWidth, 0 );
    //gl::vertex( mWidth, mHeight );
    //gl::vertex( 0, mHeight );
    //glEnd();
  gl::drawSolidRect(Rectf(0.0f, 0.0f, mWidth, mHeight));
}

void Sprite::drawLocalServer()
{
  gl::drawSolidRect(Rectf(0.0f, 0.0f, mWidth, mHeight));
}

void Sprite::setTransparent( bool transparent )
{
  setFlag(TRANSPARENT_F, transparent, FLAGS_DIRTY, mSpriteFlags);
}

bool Sprite::getTransparent() const
{
  return getFlag(TRANSPARENT_F, mSpriteFlags);
}

void Sprite::show()
{
  setFlag(VISIBLE_F, true, FLAGS_DIRTY, mSpriteFlags);
}

void Sprite::hide()
{
  setFlag(VISIBLE_F, false, FLAGS_DIRTY, mSpriteFlags);
}

bool Sprite::visible() const
{
  return getFlag(VISIBLE_F, mSpriteFlags);
}

int Sprite::getType() const
{
    return mType;
}

void Sprite::setType( int type )
{
    mType = type;
}

float Sprite::getWidth() const
{
    return mWidth;
}

float Sprite::getHeight() const
{
    return mHeight;
}

void Sprite::enable( bool flag )
{
  setFlag(ENABLED_F, flag, FLAGS_DIRTY, mSpriteFlags);
}

bool Sprite::isEnabled() const
{
  return getFlag(ENABLED_F, mSpriteFlags);
}

void Sprite::buildGlobalTransform() const
{
    buildTransform();

    mGlobalTransform = mTransformation;

    for ( Sprite *parent = mParent; parent; parent = parent->getParent() )
    {
        mGlobalTransform = parent->getGlobalTransform() * mGlobalTransform;
    }

    mInverseGlobalTransform = mGlobalTransform.inverted();
}

Sprite *Sprite::getParent() const
{
    return mParent;
}

const Matrix44f &Sprite::getGlobalTransform() const
{
    buildGlobalTransform();

    return mGlobalTransform;
}

Vec3f Sprite::globalToLocal( const Vec3f &globalPoint )
{
    buildGlobalTransform();

    Vec4f point = mInverseGlobalTransform * Vec4f(globalPoint.x, globalPoint.y, globalPoint.z, 1.0f);
    return Vec3f(point.x, point.y, point.z);
}

Vec3f Sprite::localToGlobal( const Vec3f &localPoint )
{
    buildGlobalTransform();
    Vec4f point = mGlobalTransform * Vec4f(localPoint.x, localPoint.y, localPoint.z, 1.0f);
    return Vec3f(point.x, point.y, point.z);
}

bool Sprite::contains( const Vec3f &point ) const
{
    buildGlobalTransform();

    Vec4f pR = Vec4f(point.x, point.y, point.z, 1.0f);

    Vec4f cA = mGlobalTransform * Vec4f(0.0f, 0.0f, 0.0f, 1.0f);
    Vec4f cB = mGlobalTransform * Vec4f(mWidth, 0.0f, 0.0f, 1.0f);
    Vec4f cC = mGlobalTransform * Vec4f(mWidth, mHeight, 0.0f, 1.0f);
    
    Vec4f v1 = cA - cB;
    Vec4f v2 = cC - cB;
    Vec4f v = pR - cB;

    float dot1 = v.dot(v1);
    float dot2 = v.dot(v2);
    float dot3 = v1.dot(v1);
    float dot4 = v2.dot(v2);

	return (
        dot1 >= 0 &&
        dot2 >= 0 &&
        dot1 <= dot3 &&
        dot2 <= dot4
	);
}

Sprite *Sprite::getHit( const Vec3f &point )
{
    if ( !getFlag(DRAW_SORTED_F, mSpriteFlags) )
    {
        for ( auto it = mChildren.rbegin(), it2 = mChildren.rend(); it != it2; ++it )
        {
            Sprite *child = *it;
            if ( child->isEnabled() && child->contains(point) )
                return child;
            Sprite *hitChild = child->getHit(point);
            if ( hitChild )
                return hitChild;
        }
    }
    else
    {
        std::list<Sprite *> mCopy = mChildren;
        mCopy.sort([](Sprite *i, Sprite *j)
        {
            return i->getZLevel() < j->getZLevel();
        });

        for ( auto it = mCopy.begin(), it2 = mCopy.end(); it != it2; ++it )
        {
            Sprite *child = *it;
            if ( child->isEnabled() && child->contains(point) )
                return child;
            Sprite *hitChild = child->getHit(point);
            if ( hitChild )
                return hitChild;
        }
    }

    if ( isEnabled() && contains(point) )
        return this;

    return nullptr;
}

void Sprite::setProcessTouchCallback( const std::function<void (Sprite *, const TouchInfo &)> &func )
{
  mProcessTouchInfoCallback = func;
}

void Sprite::processTouchInfo( const TouchInfo &touchInfo )
{
  mTouchProcess.processTouchInfo(touchInfo);
}

void Sprite::move( const Vec3f &delta )
{
  mPosition += delta;
  mUpdateTransform = true;
  mBoundsNeedChecking = true;
}

void Sprite::move( float deltaX, float deltaY, float deltaZ )
{
  mPosition += Vec3f(deltaX, deltaY, deltaZ);
  mUpdateTransform = true;
  mBoundsNeedChecking = true;
}

bool Sprite::multiTouchEnabled() const
{
  return mMultiTouchEnabled;
}

const Matrix44f &Sprite::getInverseGlobalTransform() const
{
  return mInverseGlobalTransform;
}

const Matrix44f    &Sprite::getInverseTransform() const
{
  buildTransform();
  return mInverseTransform;
}

bool Sprite::hasMultiTouchConstraint( const BitMask &constraint ) const
{
  return mMultiTouchConstraints & constraint;
}

bool Sprite::multiTouchConstraintNotZero() const
{
  return mMultiTouchConstraints.getFirstIndex() >= 0;
}

void Sprite::swipe( const Vec3f &swipeVector )
{
  if (mSwipeCallback)
    mSwipeCallback(this, swipeVector);
}

bool Sprite::hasDoubleTap() const
{
  if (mDoubleTapCallback)
    return true;
  return false;
}

void Sprite::tap( const Vec3f &tapPos )
{
  if (mTapCallback)
    mTapCallback(this, tapPos);
}

void Sprite::doubleTap( const Vec3f &tapPos )
{
  if (mDoubleTapCallback)
    mDoubleTapCallback(this, tapPos);
}

bool Sprite::hasTap() const
{
  if (mTapCallback)
    return true;
  return false;
}

void Sprite::processTouchInfoCallback( const TouchInfo &touchInfo )
{
  if (mProcessTouchInfoCallback)
    mProcessTouchInfoCallback(this, touchInfo);
}

void Sprite::setTapCallback( const std::function<void (Sprite *, const Vec3f &)> &func )
{
  mTapCallback = func;
}

void Sprite::setDoubleTapCallback( const std::function<void (Sprite *, const Vec3f &)> &func )
{
  mDoubleTapCallback = func;
}

void Sprite::enableMultiTouch( const BitMask &constraints )
{
  mMultiTouchEnabled = true;
  mMultiTouchConstraints.clear();
  mMultiTouchConstraints |= constraints;
}

void Sprite::disableMultiTouch()
{
  mMultiTouchEnabled = false;
  mMultiTouchConstraints.clear();
}

bool Sprite::checkBounds() const
{
  if (!mCheckBounds)
    return true;

  mBoundsNeedChecking = false;
  mInBounds = false;

  Rectf screenRect = mEngine.getScreenRect();

  float screenMinX = screenRect.getX1();
  float screenMaxX = screenRect.getX2();
  float screenMinY = screenRect.getY1();
  float screenMaxY = screenRect.getY2();

  float spriteMinX = 0.0f;
  float spriteMinY = 0.0f;
  float spriteMaxX = mWidth-1.0f;
  float spriteMaxY = mHeight-1.0f;

  Vec3f positions[4];

  buildGlobalTransform();

  positions[0] = (mGlobalTransform * Vec4f(spriteMinX, spriteMinY, 0.0f, 1.0f)).xyz();
  positions[1] = (mGlobalTransform * Vec4f(spriteMaxX, spriteMinY, 0.0f, 1.0f)).xyz();
  positions[2] = (mGlobalTransform * Vec4f(spriteMinX, spriteMaxY, 0.0f, 1.0f)).xyz();
  positions[3] = (mGlobalTransform * Vec4f(spriteMaxX, spriteMaxY, 0.0f, 1.0f)).xyz();


  spriteMinX = spriteMaxX = positions[0].x;
  spriteMinY = spriteMaxY = positions[0].y;

  for ( int i = 1; i < 4; ++i ) {
    if ( positions[i].x < spriteMinX )
      spriteMinX = positions[i].x;
    if ( positions[i].y < spriteMinY )
      spriteMinY = positions[i].y;
    if ( positions[i].x > spriteMaxX )
      spriteMaxX = positions[i].x;
    if ( positions[i].y > spriteMaxY )
      spriteMaxY = positions[i].y;
  }

  if ( spriteMinX == spriteMaxX || spriteMinY == spriteMaxY ) {
    return false;
  }

  if (spriteMinX > screenMaxX)
    return false;
  if (spriteMaxX < screenMinX)
    return false;
  if (spriteMinY > screenMaxY)
    return false;
  if (spriteMaxY < screenMinY)
    return false;

  for ( int i = 0; i < 4; ++i ) {
    if ( positions[i].x >= screenMinX && positions[i].x <= screenMaxX && positions[i].y >= screenMinY && positions[i].y <= screenMaxY ) {
      mInBounds = true;
      return true;
    }
  }

  Vec3f screenpos[4];

  screenpos[0] = Vec3f(screenMinX, screenMinY, 0.0f);
  screenpos[1] = Vec3f(screenMaxX, screenMinY, 0.0f);
  screenpos[2] = Vec3f(screenMinX, screenMaxY, 0.0f);
  screenpos[3] = Vec3f(screenMaxX, screenMaxY, 0.0f);

  for ( int i = 0; i < 4; ++i ) {
    if ( screenpos[i].x >= spriteMinX && screenpos[i].x <= spriteMaxX && screenpos[i].y >= spriteMinY && screenpos[i].y <= spriteMaxY ) {
      mInBounds = true;
      return true;
    }
  }


  for ( int i = 0; i < 4; ++i ) {
    for ( int j = 0; j < 4; ++j ) {
      if ( math::intersect2D( screenpos[i%4], screenpos[(i+1)%4], positions[i%4], positions[(i+1)%4] ) ) {
        mInBounds = true;
        return true;
      }
    }
  }
  
  mInBounds = true;
  return true;
}

void Sprite::setCheckBounds( bool checkBounds )
{
  mCheckBounds = checkBounds;
  mInBounds = !mCheckBounds;
  mBoundsNeedChecking = checkBounds;
}

bool Sprite::getCheckBounds() const
{
  return mCheckBounds;
}

void Sprite::updateCheckBounds() const
{
  if (mBoundsNeedChecking)
    checkBounds();
}

bool Sprite::inBounds() const
{
  updateCheckBounds();
  return mInBounds;
}

bool Sprite::isLoaded() const
{
  return true;
}

float Sprite::getDepth() const
{
  return mDepth;
}

void Sprite::setDragDestiantion( Sprite *dragDestination )
{
  mDragDestination = dragDestination;
}

Sprite *Sprite::getDragDestination() const
{
  return mDragDestination;
}

void Sprite::setDragDestinationCallback( const std::function<void (Sprite *, const DragDestinationInfo &)> &func )
{
  mDragDestinationCallback = func;
}

void Sprite::dragDestination( Sprite *sprite, const DragDestinationInfo &dragInfo )
{
  if (mDragDestinationCallback)
    mDragDestinationCallback(sprite, dragInfo);
}

bool Sprite::isDirty() const
{
  return !mDirty.isEmpty();
}

void Sprite::writeTo(ds::DataBuffer& buf)
{
  if (mDirty.isEmpty()) return;
  if (mId == ds::EMPTY_SPRITE_ID) {
    // This shouldn't be possible
    DS_LOG_WARNING_M("Sprite::writeTo() on empty sprite ID", SPRITE_LOG);
    return;
  }

  buf.add(mBlobType);
  buf.add(SPRITE_ID_ATTRIBUTE);
  buf.add(mId);

  writeAttributesTo(buf);
  // Terminate the sprite and attribute list
  buf.add(ds::TERMINATOR_CHAR);
  // If I wrote any attributes then make sure to terminate the block
  mDirty.clear();

  for (auto it=mChildren.begin(), end=mChildren.end(); it != end; ++it) {
    (*it)->writeTo(buf);
  }
}

void Sprite::writeAttributesTo(ds::DataBuffer& buf)
{
		if (mDirty.has(PARENT_DIRTY)) {
      buf.add(PARENT_ATT);
      if (mParent) buf.add(mParent->getId());
      else buf.add(ds::EMPTY_SPRITE_ID);
    }
		if (mDirty.has(SIZE_DIRTY)) {
      buf.add(SIZE_ATT);
      buf.add(mWidth);
      buf.add(mHeight);
      buf.add(mDepth);
    }
		if (mDirty.has(FLAGS_DIRTY)) {
      buf.add(FLAGS_ATT);
      buf.add(mSpriteFlags);
    }
		if (mDirty.has(POSITION_DIRTY)) {
      buf.add(POSITION_ATT);
      buf.add(mPosition.x);
      buf.add(mPosition.y);
      buf.add(mPosition.z);
    }
		if (mDirty.has(SCALE_DIRTY)) {
      buf.add(SCALE_ATT);
      buf.add(mScale.x);
      buf.add(mScale.y);
      buf.add(mScale.z);
    }
		if (mDirty.has(COLOR_DIRTY)) {
      buf.add(COLOR_ATT);
      buf.add(mColor.r);
      buf.add(mColor.g);
      buf.add(mColor.b);
    }
		if (mDirty.has(OPACITY_DIRTY)) {
      buf.add(OPACITY_ATT);
      buf.add(mOpacity);
    }
}

void Sprite::readFrom(ds::BlobReader& blob)
{
  ds::DataBuffer&       buf(blob.mDataBuffer);
  readAttributesFrom(buf);
}

void Sprite::readAttributesFrom(ds::DataBuffer& buf)
{
  char          id;
  bool          transformChanged = false;
  while (buf.canRead<char>() && (id=buf.read<char>()) != ds::TERMINATOR_CHAR) {
    if (id == PARENT_ATT) {
      const sprite_id_t     parentId = buf.read<sprite_id_t>();
      Sprite*               parent = mEngine.findSprite(parentId);
      if (parent) parent->addChild(*this);
    } else if (id == SIZE_ATT) {
      mWidth = buf.read<float>();
      mHeight = buf.read<float>();
      mDepth = buf.read<float>();
    } else if (id == FLAGS_ATT) {
      mSpriteFlags = buf.read<int>();
    } else if (id == POSITION_ATT) {
      mPosition.x = buf.read<float>();
      mPosition.y = buf.read<float>();
      mPosition.z = buf.read<float>();
      transformChanged = true;
    } else if (id == SCALE_ATT) {
      mScale.x = buf.read<float>();
      mScale.y = buf.read<float>();
      mScale.z = buf.read<float>();
      transformChanged = true;
    } else if (id == COLOR_ATT) {
      mColor.r = buf.read<float>();
      mColor.g = buf.read<float>();
      mColor.b = buf.read<float>();
    } else if (id == OPACITY_ATT) {
      mOpacity = buf.read<float>();
    } else {
      readAttributeFrom(id, buf);
    }
  }
  if (transformChanged) {
    mUpdateTransform = true;
    mBoundsNeedChecking = true;
  }
}

void Sprite::readAttributeFrom(const char attributeId, ds::DataBuffer& buf)
{
}

void Sprite::setSpriteId(const ds::sprite_id_t& id)
{
  if (mId == id) return;

  if (mId != ds::EMPTY_SPRITE_ID) mEngine.unregisterSprite(*this);
  mId = id;
  if (mId != ds::EMPTY_SPRITE_ID) mEngine.registerSprite(*this);
  markAsDirty(ID_DIRTY);
}

void Sprite::setFlag(const int newBit, const bool on, const DirtyState& dirty, int& oldFlags)
{
  int             newFlags = oldFlags;
  if (on) newFlags |= newBit;
  else newFlags &= ~newBit;
  if (newFlags == oldFlags) return;

  oldFlags = newFlags;
  markAsDirty(dirty);
}

bool Sprite::getFlag(const int bit, const int flags) const
{
    return (flags&bit) != 0;
}

void Sprite::markAsDirty(const DirtyState& dirty)
{
	mDirty |= dirty;
	Sprite*		      p = mParent;
	while (p) {
		if ((p->mDirty&CHILD_DIRTY) == true) break;

		p->mDirty |= CHILD_DIRTY;
		p = p->mParent;
	}

	// Opacity is a special case, since it's composed of myself and all parent values.
	// So make sure any children are notified that my opacity changed.
// This was true in BigWorld, probably still but not sure yet.
//	if (dirty.has(ds::OPACITY_DIRTY)) {
//		markChildrenAsDirty(ds::OPACITY_DIRTY);
//	}
}

void Sprite::markChildrenAsDirty(const DirtyState& dirty)
{
	mDirty |= dirty;
	for (auto it=mChildren.begin(), end=mChildren.end(); it != end; ++it) {
		(*it)->markChildrenAsDirty(dirty);
	}
}

void Sprite::setBlendMode( const BlendMode &blendMode )
{

}

Sprite::BlendMode Sprite::getBlendMode() const
{
  return mBlendMode;
}

void Sprite::setBaseShader(const std::string &location, const std::string &shadername)
{
  mShaderBaseName = shadername;
  mShaderBaseNameVert = location+mShaderBaseName+".vert";
  mShaderBaseNameFrag = location+mShaderBaseName+".frag";

  loadShaders();
}

std::string Sprite::getBaseShaderName() const
{
  return mShaderBaseName;
}

void Sprite::loadShaders()
{
  if (mShaderBaseNameVert.empty() || mShaderBaseNameFrag.empty()) {
    try {
      mShaderBase = gl::GlslProg(DefaultBaseVert.c_str(), DefaultBaseFrag.c_str());
    } catch ( std::exception &e ) {
      std::cout << e.what() << std::endl;
    }
  } else {
    try {
      mShaderBase = gl::GlslProg(loadFile(mShaderBaseNameVert), loadFile(mShaderBaseNameFrag));
    } catch ( std::exception &e ) {
      std::cout << e.what() << std::endl;
    }
  }
}

} // namespace ui
} // namespace ds
