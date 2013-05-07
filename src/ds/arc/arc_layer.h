#pragma once
#ifndef DS_ARC_ARCLAYER_H_
#define DS_ARC_ARCLAYER_H_

#include <memory>
#include "ds/arc/arc.h"
#include "ds/arc/arc_color_array.h"

namespace ds {
namespace arc {

/**
 * \class ds::arc::Layer
 * \brief A special arc used to render layers of an image.
 */
class Layer : public Arc
{
public:
	Layer();

	virtual void					renderCircle(RenderCircleParams&) const;

	virtual void					readXml(const ci::XmlTree&);

private:
	std::unique_ptr<Arc>	mArc;
	ColorArray						mColor;
	std::function<double(const RenderCircleParams&)>
												mInput;
};

} // namespace arc
} // namespace ds

#endif // DS_ARC_ARCRENDER_H_