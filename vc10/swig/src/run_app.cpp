#include "run_app.h"

namespace ds {
	int runApp( App *app ) {
		cinder::app::AppBasic::prepareLaunch();
		cinder::app::Renderer *ren = new RendererGl();
		cinder::app::AppBasic::executeLaunch( app, ren, "CinderApp" );
		cinder::app::AppBasic::cleanupLaunch();
		return 0;
	}

	int go() {
		cinder::app::AppBasic::prepareLaunch();
		cinder::app::AppBasic *app = new App();
		cinder::app::Renderer *ren = new RendererGl();
		cinder::app::AppBasic::executeLaunch( app, ren, "CinderApp" );
		cinder::app::AppBasic::cleanupLaunch();
		return 0;
	} 
}
