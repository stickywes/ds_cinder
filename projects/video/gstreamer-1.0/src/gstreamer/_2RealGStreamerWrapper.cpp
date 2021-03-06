/*
	CADET - Center for Advances in Digital Entertainment Technologies
	Copyright 2012 University of Applied Science Salzburg / MultiMediaTechnology

	http://www.cadet.at
	http://multimediatechnology.at/

	Licensed under the Apache License, Version 2.0 (the "License");
	you may not use this file except in compliance with the License.
	You may obtain a copy of the License at

	http://www.apache.org/licenses/LICENSE-2.0

	Unless required by applicable law or agreed to in writing, software
	distributed under the License is distributed on an "AS IS" BASIS,
	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	See the License for the specific language governing permissions and
	limitations under the License.

	CADET - Center for Advances in Digital Entertainment Technologies

	Authors: Steven Stojanovic, Robert Praxmarer
	Web: http://www.1n0ut.com
	Email: stevesparrow07@googlemail.com, support@cadet.at
	Created: 07-09-2011

	This wrapper uses GStreamer, and is credited as follows:
*/
/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gst.h: Main header for GStreamer, apps should include this
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef LINUX
    #include <math.h>
    #include <cstring>
    #include "_2RealGStreamerWrapper.h"
#else
    #include "_2RealGStreamerWrapper.h"
#endif

#include "ds/debug/logger.h"
#include <iostream>

#include "gstreamer-1.0/gst/net/gstnetclientclock.h"

namespace _2RealGStreamerWrapper
{

/* this code is for making use of threads for the message handling, so you don't have to call update manually
   but this introduces a dependency to boost::thread so it's up to you what you prefer, it shouldn't make much
   difference in practical terms */
#ifdef THREADED_MESSAGE_HANDLER
gboolean onHandleGstMessages(GstBus *bus, GstMessage *msg, gpointer data)
{
     GStreamerWrapper* obj = (GStreamerWrapper*)(data);
     switch (GST_MESSAGE_TYPE(msg))
     {
		 case GST_MESSAGE_ERROR:
					GError *err;
					gchar *debug;
					gst_message_parse_error( msg, &err, &debug );
					std::cout << "Embedded video playback halted: module " << gst_element_get_name( GST_MESSAGE_SRC( msg ) ) << " reported " << err->message << std::endl;
					obj->close();
					g_error_free(err);
					g_free(debug);
			break;

        case GST_MESSAGE_EOS:
				switch ( obj->getLoopMode() )
					{
						case NO_LOOP:
							 obj->stop();
							break;

						case LOOP:
							obj->stop();
							obj->play();
							break;

						case BIDIRECTIONAL_LOOP:
							 obj->setDirection((PlayDirection)-obj->getDirection());
							 obj->stop();
							 obj->play();
							break;

						default:
							break;
					}

            break;
        default:
            break;
    }
    return TRUE;
}

void threadedMessageHandler(GStreamerWrapper* obj)
{
	obj->m_GMainLoop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (obj->m_GMainLoop);		//g_main loop is needed to catch user defined callbacks for detection of EOS, the GST EOS callback works, but apparently you can't manipulate the pipeline (seek) from in there
}
#endif


/******************************************************************************/

GStreamerWrapper::GStreamerWrapper() :
	m_bFileIsOpen( false ),
	m_cVideoBuffer( NULL ),
	m_cAudioBuffer( NULL ),
	m_GstPipeline( NULL ),
	m_GstVideoSink( NULL ),
	m_GstAudioSink( NULL ),
	m_GstBus( NULL ),
	m_StartPlaying(true),
	m_StopOnLoopComplete(false)
{//
	static bool initialized(false);
	//if(!initialized){
		gst_init( NULL, NULL );
	//	initialized = true;
	//}
 
	m_CurrentPlayState = NOT_INITIALIZED;
	
	//std::cout << "Gstreamer version: " << GST_VERSION_MAJOR << " " << GST_VERSION_MICRO << " " << GST_VERSION_MINOR << std::endl;
}

//GStreamerWrapper::GStreamerWrapper( std::string strFilename, bool bGenerateVideoBuffer, bool bGenerateAudioBuffer ) :
//	m_bFileIsOpen( false ),
//	m_cVideoBuffer( NULL ),
//	m_cAudioBuffer( NULL ),
//	m_GstPipeline( NULL ),
//	m_GstVideoSink( NULL ),
//	m_GstAudioSink( NULL ),
//	m_GstBus( NULL )
//{
//	gst_init( NULL, NULL );
//
////	open( strFilename, bGenerateVideoBuffer, bGenerateAudioBuffer );
//}
//
//
GStreamerWrapper::~GStreamerWrapper()
{
	close();
}

bool GStreamerWrapper::open( std::string strFilename, bool bGenerateVideoBuffer, bool bGenerateAudioBuffer, bool isTransparent, int videoWidth, int videoHeight)
{
	// init property variables
	m_iNumVideoStreams = 0;
	m_iNumAudioStreams = 0;
	m_iCurrentVideoStream = 0;
	m_iCurrentAudioStream = 0;
	m_iWidth = videoWidth;		
	m_iHeight = videoHeight;
	m_iCurrentFrameNumber = 0;	// set to invalid, as it is not decoded yet
	m_dCurrentTimeInMs = 0;	// set to invalid, as it is not decoded yet
	m_bIsAudioSigned = false;
	m_bIsNewVideoFrame = false;
	m_iNumAudioChannels = 0;
	m_iAudioSampleRate = 0;
	m_iAudioBufferSize = 0;
	m_iAudioWidth = 0;
	m_AudioEndianness = LITTLE_ENDIAN;
	m_fFps = 0;
	m_dDurationInMs = 0;
	m_iNumberOfFrames = 0;

	m_fVolume = 1.0f;
	m_fSpeed = 1.0f;
	m_PlayDirection = FORWARD;
	m_CurrentPlayState = NOT_INITIALIZED;
	m_CurrentGstState = STATE_NULL;
	m_LoopMode = LOOP;
	m_strFilename = strFilename;
	m_PendingSeek = false;

	if( m_bFileIsOpen )
	{
		stop();
		close();
	}

#ifdef THREADED_MESSAGE_HANDLER
		m_MsgHandlingThread = boost::thread( boost::bind(threadedMessageHandler, this));
#endif

	if(videoWidth % 4 != 0){
		videoWidth += 4 - videoWidth % 4;
	}

	m_iWidth = videoWidth;

	if(isTransparent){
		m_cVideoBuffer = new unsigned char[8 * 4 * videoWidth * videoHeight];
	} else {
		m_cVideoBuffer = new unsigned char[8 * 3 * videoWidth * videoHeight];
	}

	////////////////////////////////////////////////////////////////////////// PIPELINE
	// Init main pipeline --> playbin2
	m_GstPipeline = gst_element_factory_make( "playbin", "pipeline" );

	std::replace(strFilename.begin(), strFilename.end(), '\\', '/');
	// Check and re-arrange filename string
	if ( strFilename.find( "file:/", 0 ) == std::string::npos &&
		 strFilename.find( "file:///", 0 ) == std::string::npos &&
		 strFilename.find( "http://", 0 ) == std::string::npos )
	{
		strFilename = "file:///" + strFilename;
	}

	// Open Uri
	g_object_set( m_GstPipeline, "uri", strFilename.c_str(), NULL );


	////////////////////////////////////////////////////////////////////////// VIDEO SINK
	// Extract and Config Video Sink
	if ( bGenerateVideoBuffer )
	{
		// Create the video appsink and configure it
		m_GstVideoSink = gst_element_factory_make( "appsink", "videosink" );

		//gst_base_sink_set_sync( GST_BASE_SINK( m_GstVideoSink ), true );
		gst_app_sink_set_max_buffers( GST_APP_SINK( m_GstVideoSink ), 8 );
		gst_app_sink_set_drop( GST_APP_SINK( m_GstVideoSink ), true );
		gst_base_sink_set_qos_enabled(GST_BASE_SINK(m_GstVideoSink), true);
		gst_base_sink_set_max_lateness( GST_BASE_SINK( m_GstVideoSink ), 40000000); // 1000000000 = 1 second, 40000000 = 40 ms

		// Set some fix caps for the video sink
		// It would seem that GStreamer then tries to transform any incoming video stream according to these caps
		GstCaps* caps;
		if(isTransparent){
			caps = gst_caps_new_simple( "video/x-raw",
				"format", G_TYPE_STRING, "RGBA",
				"width", G_TYPE_INT, videoWidth,
				"height", G_TYPE_INT, videoHeight,
			//"bpp", G_TYPE_INT, 32,
			//"depth", G_TYPE_INT, 24,
		//	"endianness",G_TYPE_INT,4321,
			//"red_mask",G_TYPE_INT,0xff000000,
			//"green_mask",G_TYPE_INT,0x00ff0000,
			//"blue_mask",G_TYPE_INT,0x0000ff00,
			//"alpha_mask",G_TYPE_INT,0x000000ff,
			NULL );
		} else  {
			caps= gst_caps_new_simple( "video/x-raw",
				"format", G_TYPE_STRING, "RGB",
				"pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
				//"bpp", G_TYPE_INT, 24,
				//"depth", G_TYPE_INT, 24,
				"width", G_TYPE_INT, videoWidth,
				"height", G_TYPE_INT, videoHeight,
			//	"endianness",G_TYPE_INT,4321,
			//	"red_mask",G_TYPE_INT,0xff0000,
			//	"green_mask",G_TYPE_INT,0x00ff00,
			//	"blue_mask",G_TYPE_INT,0x0000ff,
				NULL );
		}


		gst_app_sink_set_caps( GST_APP_SINK( m_GstVideoSink ), caps );
		gst_caps_unref( caps );

		// Set the configured video appsink to the main pipeline
		g_object_set( m_GstPipeline, "video-sink", m_GstVideoSink, (void*)NULL );
		// Tell the video appsink that it should not emit signals as the buffer retrieving is handled via callback methods
		g_object_set( m_GstVideoSink, "emit-signals", false, "sync", true, "async", true, (void*)NULL );

		// Set Video Sink callback methods
		m_GstVideoSinkCallbacks.eos = &GStreamerWrapper::onEosFromVideoSource;
		m_GstVideoSinkCallbacks.new_preroll = &GStreamerWrapper::onNewPrerollFromVideoSource;
		m_GstVideoSinkCallbacks.new_sample = &GStreamerWrapper::onNewBufferFromVideoSource;
		gst_app_sink_set_callbacks( GST_APP_SINK( m_GstVideoSink ), &m_GstVideoSinkCallbacks, this, NULL );
	}
	else
	{
		//GstElement* videoSink = gst_element_factory_make( "directdrawsink", NULL );
		DS_LOG_WARNING("Video size not detected or video buffer not set to be created. Ignoring video output.");
		GstElement* videoSink = gst_element_factory_make( "faksesink", NULL );
		g_object_set( m_GstPipeline, "video-sink", videoSink, NULL );

	}

	////////////////////////////////////////////////////////////////////////// AUDIO SINK
	// Extract and config Audio Sink
	if ( bGenerateAudioBuffer )
	{
		// Create and configure audio appsink
		m_GstAudioSink = gst_element_factory_make( "appsink", "audiosink" );
		gst_base_sink_set_sync( GST_BASE_SINK( m_GstAudioSink ), true );
		// Set the configured audio appsink to the main pipeline
		g_object_set( m_GstPipeline, "audio-sink", m_GstAudioSink, (void*)NULL );
		// Tell the video appsink that it should not emit signals as the buffer retrieving is handled via callback methods
		g_object_set( m_GstAudioSink, "emit-signals", false, "sync", true, (void*)NULL );

		// Set Audio Sink callback methods
		m_GstAudioSinkCallbacks.eos = &GStreamerWrapper::onEosFromAudioSource;
		m_GstAudioSinkCallbacks.new_preroll = &GStreamerWrapper::onNewPrerollFromAudioSource;
		m_GstAudioSinkCallbacks.new_sample = &GStreamerWrapper::onNewBufferFromAudioSource;
		gst_app_sink_set_callbacks( GST_APP_SINK( m_GstAudioSink ), &m_GstAudioSinkCallbacks, this, NULL );
	}
	else
	{
		GstElement* audioSink = gst_element_factory_make( "autoaudiosink", NULL );
		g_object_set ( m_GstPipeline, "audio-sink", audioSink, NULL );
	}

	////////////////////////////////////////////////////////////////////////// BUS
	// Set GstBus
	m_GstBus = gst_pipeline_get_bus( GST_PIPELINE( m_GstPipeline ) );

	if ( m_GstPipeline != NULL )
	{
//just add this callback for threaded message handling
#ifdef THREADED_MESSAGE_HANDLER
		gst_bus_add_watch (m_GstBus, onHandleGstMessages, this );
#endif
		// We need to stream the file a little bit in order to be able to retrieve information from it
		gst_element_set_state( m_GstPipeline, GST_STATE_READY );
		gst_element_set_state( m_GstPipeline, GST_STATE_PAUSED );

		// For some reason this is needed in order to gather video information such as size, framerate etc ...
		//GstState state;
		//gst_element_get_state( m_GstPipeline, &state, NULL, 20 * GST_SECOND );
		m_CurrentPlayState = OPENED;

 		if(m_StartPlaying){
 			gst_element_set_state(m_GstPipeline, GST_STATE_PLAYING);
 		}
	}

	// Retrieve and store all relevant Media Information
//	retrieveVideoInfo();
// 
// 	if( !hasVideo() && !hasAudio() )	// is a valid multimedia file?
// 	{
// 		DS_LOG_WARNING("Couldn't detect any audio or video streams in this file! " << strFilename);
// 		//close();
// 		return false;
// 	}
// 
// 	// Print Media Info
// 	printMediaFileInfo();

	// TODO: Check if everything was initialized correctly
	// A file has been opened
	m_bFileIsOpen = true;

	return true;
}

void GStreamerWrapper::close()
{

	m_bFileIsOpen = false;
	m_CurrentPlayState = NOT_INITIALIZED;


// get rid of message handler thread
#ifdef THREADED_MESSAGE_HANDLER
	g_main_loop_quit(m_GMainLoop);
	m_MsgHandlingThread.join();
#endif
	stop();

	//if ( m_GstVideoSink != NULL )
	//{
	//	gst_object_unref( m_GstVideoSink );
	//	m_GstVideoSink = NULL;
	//}

	//if ( m_GstAudioSink != NULL )
	//{
	//	gst_object_unref( m_GstAudioSink );
	//	m_GstAudioSink = NULL;
	//}

	if ( m_GstPipeline != NULL )
	{
		//gst_element_set_state( m_GstPipeline, GST_STATE_NULL );
		gst_object_unref( m_GstPipeline );
		m_GstPipeline = NULL;
		m_GstVideoSink = NULL;
		m_GstAudioSink = NULL;
	}

	if ( m_GstBus != NULL )
	{
		gst_object_unref( m_GstBus );
		m_GstBus = NULL;
	}

	delete [] m_cVideoBuffer;
	m_cVideoBuffer = NULL;

	delete [] m_cAudioBuffer;
	m_cAudioBuffer = NULL;
}

void GStreamerWrapper::update()
{
#ifndef THREADED_MESSAGE_HANDLER
	handleGStMessage();
#endif

	if(m_GstVideoSink && m_CurrentPlayState == PLAYING){

		//GstSample * sample = gst_app_sink_pull_sample (GST_APP_SINK (m_GstVideoSink));
		//if(sample){
		//	newVideoSinkBufferCallback(sample);
		//}
	}
}

void GStreamerWrapper::play()
{
	if ( m_GstPipeline != NULL )
	{
		GstStateChangeReturn gscr = gst_element_set_state( m_GstPipeline, GST_STATE_PLAYING );
		if(gscr == GST_STATE_CHANGE_FAILURE){
			DS_LOG_WARNING(" state change failure");
		}
		m_CurrentPlayState = PLAYING;
	}
}

void GStreamerWrapper::stop()
{
	if ( m_GstPipeline != NULL )
	{
		// "Hack" for stopping ...
		//gst_element_set_state( m_GstPipeline, GST_STATE_PAUSED );
		// The original setting of state paused was hacky, they were using the stop
		// call in the looping mechanism. Why not just seek there? 
		// Stop in this context now means a full clearing of the buffers in gstreamer
		gst_element_set_state( m_GstPipeline, GST_STATE_NULL );

		//if ( m_PlayDirection == FORWARD )
		//	seekFrame( 0 );
		//else if ( m_PlayDirection == BACKWARD )
		//	seekFrame( m_iDurationInNs );

		m_CurrentPlayState = STOPPED;
	}
}

void GStreamerWrapper::pause()
{
	if ( m_GstPipeline != NULL )
	{
		gst_element_set_state( m_GstPipeline, GST_STATE_PAUSED );
		m_CurrentPlayState = PAUSED;
	}
}

void GStreamerWrapper::printMediaFileInfo()
{
	return;

	std::cout << "-----------------------------------------------------------------" << std::endl;
	std::cout << "Loading file ..." << std::endl;
	std::cout << "> File Uri: " << m_strFilename << std::endl;
	std::cout << "> Duration in NanoSeconds: " << m_iDurationInNs << std::endl;
	std::cout << "> Video Streams: " << m_iNumVideoStreams << std::endl;
	std::cout << "> Audio Streams: " << m_iNumAudioStreams << std::endl;

	if ( m_iNumVideoStreams > 0 )
	{
		std::cout << std::endl << "Video Information ..." << std::endl;
	//	std::cout << "> Number of Frames: " << m_iNumberOfFrames << std::endl;
		std::cout << "> Video Width: " << m_iWidth << std::endl;
		std::cout << "> Video Height: " << m_iHeight << std::endl;
	//	std::cout << "> FPS: " << m_fFps << std::endl;
	}

	if ( m_iNumAudioStreams > 0 )
	{
		std::cout << std::endl << "Audio Information ..." << std::endl;
		std::cout << "> Sample Rate: " << m_iAudioSampleRate << std::endl;
		std::cout << "> Channels: " << m_iNumAudioChannels << std::endl;
		std::cout << "> Audio Buffer Size: " << m_iAudioBufferSize << std::endl;
		std::cout << "> Audio Decode Buffer Size: " << m_iAudioDecodeBufferSize << std::endl;
		std::cout << "> Is Audio Signed: " << m_bIsAudioSigned << std::endl;
		std::cout << "> Audio Width: " << m_iAudioWidth << std::endl;
		std::cout << "> Audio Endianness: " << m_AudioEndianness << std::endl;
	}
	std::cout << "-----------------------------------------------------------------" << std::endl;
}

void GStreamerWrapper::setCurrentVideoStream( int iCurrentVideoStream )
{
	if ( m_iCurrentVideoStream != iCurrentVideoStream )
	{
		if ( iCurrentVideoStream >= 0 && iCurrentVideoStream < m_iNumVideoStreams )
		{
			m_iCurrentVideoStream = iCurrentVideoStream;

			g_object_set( m_GstPipeline, "current-video", m_iCurrentVideoStream, NULL );
		}
	}
}

void GStreamerWrapper::setCurrentAudioStream( int iCurrentAudioStream )
{
	if ( m_iCurrentAudioStream != iCurrentAudioStream )
	{
		if ( iCurrentAudioStream >= 0 && iCurrentAudioStream < m_iNumAudioStreams )
		{
			m_iCurrentAudioStream = iCurrentAudioStream;

			g_object_set( m_GstPipeline, "current-audio", m_iCurrentAudioStream, NULL );
		}
	}
}

void GStreamerWrapper::setSpeed( float fSpeed )
{
	if( fSpeed != m_fSpeed )
	{
		m_fSpeed = fSpeed;
		if ( m_fSpeed < 0.0f )
			m_fSpeed = 0.0f;

		changeSpeedAndDirection( m_fSpeed, m_PlayDirection );
	}
}

void GStreamerWrapper::setDirection( PlayDirection direction )
{
	if ( m_PlayDirection != direction )
	{
		m_PlayDirection = direction;
		changeSpeedAndDirection( m_fSpeed, m_PlayDirection );
	}
}

void GStreamerWrapper::setLoopMode( LoopMode loopMode )
{
	m_LoopMode = loopMode;
}

void GStreamerWrapper::setFramePosition( gint64 iTargetFrameNumber )
{
	m_iCurrentFrameNumber = iTargetFrameNumber;

	setPosition( (float)m_iCurrentFrameNumber / (float)m_iNumberOfFrames );
}

void GStreamerWrapper::setTimePositionInMs( double dTargetTimeInMs )
{
	m_dCurrentTimeInMs = dTargetTimeInMs;
	seekFrame( (gint64)(m_dCurrentTimeInMs * 1000000) );
}

void GStreamerWrapper::setTimePositionInNs( gint64 iTargetTimeInNs )
{
	m_iCurrentTimeInNs = iTargetTimeInNs;
	seekFrame( m_iCurrentTimeInNs );
}

void GStreamerWrapper::setPosition(double fPos)
{
	if( fPos < 0.0 )
		fPos = 0.0;
	else if( fPos > 1.0 )
		fPos = 1.0;

	m_dCurrentTimeInMs = fPos * m_dCurrentTimeInMs;
	m_iCurrentFrameNumber = (gint64)(fPos * m_iNumberOfFrames);
	m_iCurrentTimeInNs = (gint64)(fPos * m_iDurationInNs);

	seekFrame( m_iCurrentTimeInNs );
}

bool GStreamerWrapper::hasVideo()
{
	return m_ContentType == VIDEO_AND_AUDIO || m_ContentType == VIDEO;
}

bool GStreamerWrapper::hasAudio()
{
	return m_ContentType == VIDEO_AND_AUDIO || m_ContentType == AUDIO;
}

std::string GStreamerWrapper::getFileName()
{
	return m_strFilename;
}

unsigned char* GStreamerWrapper::getVideo()
{
	m_bIsNewVideoFrame = false;
	return m_cVideoBuffer;
}

int GStreamerWrapper::getCurrentVideoStream()
{
	return m_iCurrentVideoStream;
}

int GStreamerWrapper::getCurrentAudioStream()
{
	return m_iCurrentAudioStream;
}

int GStreamerWrapper::getNumberOfVideoStreams()
{
	return m_iNumVideoStreams;
}

int GStreamerWrapper::getNumberOfAudioStreams()
{
	return m_iNumAudioStreams;
}

unsigned int GStreamerWrapper::getWidth()
{
	return m_iWidth;
}

unsigned int GStreamerWrapper::getHeight()
{
	return m_iHeight;
}

bool GStreamerWrapper::isNewVideoFrame()
{
	return m_bIsNewVideoFrame;
}

float GStreamerWrapper::getFps()
{
	return m_fFps;
}

float GStreamerWrapper::getSpeed()
{
	return m_fSpeed;
}

double GStreamerWrapper::getPosition() const
{
	return static_cast<double>(getCurrentTimeInNs()) / static_cast<double>(m_iDurationInNs);
}

gint64 GStreamerWrapper::getCurrentFrameNumber()
{
	m_iCurrentFrameNumber = (gint64)(floor( (double)getCurrentTimeInMs() / 1000.0 * m_fFps ));
	return m_iCurrentFrameNumber;
}

gint64 GStreamerWrapper::getNumberOfFrames()
{
	return m_iNumberOfFrames;
}

double GStreamerWrapper::getCurrentTimeInMs()
{
	return (double)(getCurrentTimeInNs() / 1000000);
}

double GStreamerWrapper::getDurationInMs() const
{
	return m_dDurationInMs;
}

gint64 GStreamerWrapper::getCurrentTimeInNs() const
{
	GstFormat gstFormat = GST_FORMAT_TIME;
	gst_element_query_position( GST_ELEMENT( m_GstPipeline ), gstFormat, &m_iCurrentTimeInNs );
	return m_iCurrentTimeInNs;
}

gint64 GStreamerWrapper::getDurationInNs()
{
	return m_iDurationInNs;
}

PlayState GStreamerWrapper::getState() const
{
	return m_CurrentPlayState;
}

PlayDirection GStreamerWrapper::getDirection()
{
	return m_PlayDirection;
}

LoopMode GStreamerWrapper::getLoopMode()
{
	return m_LoopMode;
}

ContentType GStreamerWrapper::getContentType()
{
	return m_ContentType;
}

void GStreamerWrapper::setVolume( float fVolume )
{
	if ( m_fVolume != fVolume )
	{
		m_fVolume = fVolume;
		if ( m_fVolume < 0.0f )
			m_fVolume = 0.0f;
		else if ( m_fVolume > 1.0f )
			m_fVolume = 1.0f;

		g_object_set( m_GstPipeline, "volume", m_fVolume, NULL );
	}
}

unsigned char* GStreamerWrapper::getAudio()
{
	return m_cAudioBuffer;
}

bool GStreamerWrapper::getIsAudioSigned()
{
	return m_bIsAudioSigned;
}

int	GStreamerWrapper::getNumOfAudioChannels()
{
	return m_iNumAudioChannels;
}

int GStreamerWrapper::getAudioSampleRate()
{
	return m_iAudioSampleRate;
}

int GStreamerWrapper::getAudioBufferSize()
{
	return m_iAudioBufferSize;
}

int GStreamerWrapper::getAudioDecodeBufferSize()
{
	return m_iAudioDecodeBufferSize;
}

int GStreamerWrapper::getAudioWidth()
{
	return m_iAudioWidth;
}

float GStreamerWrapper::getCurrentVolume()
{
	return m_fVolume;
}

Endianness GStreamerWrapper::getAudioEndianness()
{
	return m_AudioEndianness;
}

bool GStreamerWrapper::seekFrame( gint64 iTargetTimeInNs )
{
	if(m_CurrentGstState != STATE_PLAYING){
		m_PendingSeekTime = iTargetTimeInNs;
		m_PendingSeek = true;
		return false;
	}

	GstFormat gstFormat = GST_FORMAT_TIME;
	// The flags determine how the seek behaves, in this case we simply want to jump to certain part in stream
	// while keeping the pre-set speed and play direction
//	GstSeekFlags gstSeekFlags = ( GstSeekFlags ) ( GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH );
	GstSeekFlags gstSeekFlags = ( GstSeekFlags ) ( GST_SEEK_FLAG_FLUSH );

	gboolean bIsSeekSuccessful = false;

	if ( m_PlayDirection == FORWARD )
	{
		bIsSeekSuccessful = gst_element_seek( GST_ELEMENT( m_GstPipeline ),
			m_fSpeed,
			gstFormat,
			gstSeekFlags,
			GST_SEEK_TYPE_SET,
			iTargetTimeInNs,
			GST_SEEK_TYPE_NONE,
			GST_CLOCK_TIME_NONE );
	}
	else if ( m_PlayDirection == BACKWARD )
	{
		bIsSeekSuccessful = gst_element_seek( GST_ELEMENT( m_GstPipeline ),
			-m_fSpeed,
			gstFormat,
			gstSeekFlags,
			GST_SEEK_TYPE_SET,
			0,
			GST_SEEK_TYPE_SET,
			iTargetTimeInNs );
	}
	if(!(bIsSeekSuccessful == 0)){
		m_PendingSeek = false;
	}

	return bIsSeekSuccessful != 0;
}

bool GStreamerWrapper::changeSpeedAndDirection( float fSpeed, PlayDirection direction )
{
	GstFormat gstFormat = GST_FORMAT_TIME;
	// The flags determine how the seek behaves, in this case we stay at the current position in the stream but simply
	// want to change either speed, play direction or both
	GstSeekFlags gstSeekFlags = (GstSeekFlags)( GST_SEEK_FLAG_SKIP | GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH );

	gboolean bIsSeekSuccessful = false;

	if ( direction == FORWARD )
	{
		bIsSeekSuccessful = gst_element_seek( GST_ELEMENT( m_GstPipeline ),
			fSpeed,
			gstFormat,
			gstSeekFlags,
			GST_SEEK_TYPE_SET,
			getCurrentTimeInNs(),
			GST_SEEK_TYPE_SET,
			m_iDurationInNs );
	}
	else if ( direction == BACKWARD )
	{
		bIsSeekSuccessful = gst_element_seek( GST_ELEMENT( m_GstPipeline ),
			-fSpeed,
			gstFormat,
			gstSeekFlags,
			GST_SEEK_TYPE_SET,
			0,
			GST_SEEK_TYPE_SET,
			getCurrentTimeInNs() );
	}

	return bIsSeekSuccessful != 0;
}

void GStreamerWrapper::retrieveVideoInfo()
{
	////////////////////////////////////////////////////////////////////////// Media Duration
	// Nanoseconds
	GstFormat gstFormat = GST_FORMAT_TIME;
	gst_element_query_duration( GST_ELEMENT( m_GstPipeline ), gstFormat, &m_iDurationInNs );

	// Milliseconds
	m_dDurationInMs = (double)(GST_TIME_AS_MSECONDS( m_iDurationInNs ));

	////////////////////////////////////////////////////////////////////////// Stream Info
	// Number of Video Streams
	g_object_get( m_GstPipeline, "n-video", &m_iNumVideoStreams, NULL );

	// Number of Audio Streams
	g_object_get( m_GstPipeline, "n-audio", &m_iNumAudioStreams, NULL );

	// Set Content Type according to the number of available Video and Audio streams
	if ( m_iNumVideoStreams > 0 && m_iNumAudioStreams > 0 ){
		m_ContentType = VIDEO_AND_AUDIO;
	} else if ( m_iNumVideoStreams > 0 ){
		m_ContentType = VIDEO;
	} else if ( m_iNumAudioStreams > 0 ){
		m_ContentType = AUDIO;
	}
	//if(m_iNumVideoStreams < 1){
	//	m_iNumVideoStreams = 1;
	//}

	////////////////////////////////////////////////////////////////////////// Video Data
	//if ( m_iNumVideoStreams > 0 )
	//{
		//GstPad* gstPad = gst_element_get_static_pad( m_GstVideoSink, "sink" );
		//if ( gstPad )
		//{
			
			//const GstCaps* caps = gst_pad_get_allowed_caps(gstPad);
			//gboolean isFixed = gst_caps_is_fixed(caps);
			//std::cout << "Is fixed caps: " << isFixed << std::endl;
			//const GstStructure *str;
			//gint widthy, heighty;
			//str = gst_caps_get_structure (caps, 0);
			//if (!gst_structure_get_int (str, "width", &widthy) ||
			//	!gst_structure_get_int (str, "height", &heighty)) {
			//		std::cout << "No width/height available" << std::endl;
			//		return;
			//}


			//gint width, height;
			//const GstStructure *str;


			//str = gst_caps_get_structure (caps, 0);
			//if (!gst_structure_get_int (str, "width", &width) ||
			//	!gst_structure_get_int (str, "height", &height)) {
			//		g_print ("No width/height available\n");
			//		return;
			//}

			//g_print ("The video size of this set of capabilities is %dx%d\n",
			//	width, height);

			//// Video Size
			//gst_video_get_size( GST_PAD( gstPad ), &m_iWidth, &m_iHeight );

			//// Frame Rate
			//const GValue* framerate = gst_video_frame_rate( gstPad );

			//int iFpsNumerator = gst_value_get_fraction_numerator( framerate );
			//int iFpsDenominator = gst_value_get_fraction_denominator( framerate );

			//// Number of frames
			//m_iNumberOfFrames = (gint64)((float)( m_iDurationInNs / GST_SECOND ) * (float)iFpsNumerator / (float)iFpsDenominator);

			//// FPS
			//m_fFps = (float)iFpsNumerator / (float)iFpsDenominator;


			//gst_object_unref( gstPad );
		//}

		//if(m_iWidth < 1 || m_iHeight < 1){
		//	DS_LOG_WARNING( "Error finding the size of the video!" );
		//}
	//}
}

void GStreamerWrapper::handleGStMessage()
{
	if ( m_GstBus != NULL )
	{
		while ( gst_bus_have_pending( m_GstBus ) )
		{
			m_GstMessage = gst_bus_pop( m_GstBus );

			if ( m_GstMessage != NULL )
			{
				// std::cout << "Message Type: " << GST_MESSAGE_TYPE_NAME( m_GstMessage ) << std::endl;

				switch ( GST_MESSAGE_TYPE( m_GstMessage ) )
				{
				case GST_MESSAGE_INFO:{
					GError* err;
					gchar* debug;
					gst_message_parse_info(m_GstMessage, &err, &debug);
					std::cout <<"Gst info: " << err->message << " " << debug << std::endl;
					}
					break;
				case GST_MESSAGE_ERROR:
					GError* err;
					gchar* debug;
					gst_message_parse_error( m_GstMessage, &err, &debug );

					std::cout << "Embedded video playback halted: module " << gst_element_get_name( GST_MESSAGE_SRC( m_GstMessage ) ) <<
						" reported " << err->message << std::endl;

					close();

					g_error_free(err);
					g_free(debug);
					break;
				case GST_MESSAGE_STATE_CHANGED:{
					//retrieveVideoInfo();
					GstState oldState;
					GstState newState;
					GstState pendingState;
					gst_message_parse_state_changed(m_GstMessage, &oldState, &newState, &pendingState);

				//	std::cout << "State changed: " << oldState << " " << newState << " " << pendingState << std::endl;

					if (newState == GST_STATE_PLAYING) {
						m_CurrentGstState = STATE_PLAYING;
					} else if(newState == GST_STATE_NULL){
						m_CurrentGstState = STATE_NULL;
					} else if(newState == GST_STATE_PAUSED){
						m_CurrentGstState = STATE_PAUSED;
					} else if(newState == GST_STATE_READY){
						m_CurrentGstState = STATE_READY;
					}

					if( (m_CurrentGstState == GST_STATE_PLAYING || m_CurrentGstState == GST_STATE_PAUSED) && m_PendingSeek){
						seekFrame(m_PendingSeekTime);
					}

				  }


				break;

				case GST_MESSAGE_ASYNC_DONE :{
					retrieveVideoInfo();
				}
				break;

				case GST_MESSAGE_NEW_CLOCK :{
					//GstClock* clock = gst_net_client_clock_new("new", "127.0.0.1", 6767, 0);
					//gst_pipeline_use_clock(GST_PIPELINE(m_GstPipeline), clock);
				}
				break;

				case GST_MESSAGE_SEGMENT_DONE : {

					std::cout << "Segment done" << std::endl;
 					if(m_StopOnLoopComplete){
 						stop();
 						m_StopOnLoopComplete = false;
 					} else {
						std::cout << "Segment done seek " << m_iDurationInNs << std::endl;
						gst_element_seek( GST_ELEMENT( m_GstPipeline ),
							m_fSpeed,
							GST_FORMAT_TIME,
							(GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT),
							GST_SEEK_TYPE_SET,
							0,
							GST_SEEK_TYPE_SET,
							m_iDurationInNs );

					}
					}
												break;
				case GST_MESSAGE_EOS:
					//std::cout << "EOS" << std::endl;
					switch ( m_LoopMode )
					{
						case NO_LOOP:
							pause();
							if(mVideoCompleteCallback) mVideoCompleteCallback(this);
							break;

						case LOOP:
							{

							gst_element_seek( GST_ELEMENT( m_GstPipeline ),
								m_fSpeed,
								GST_FORMAT_TIME,
								(GstSeekFlags)(GST_SEEK_FLAG_FLUSH),// | GST_SEEK_FLAG_SEGMENT),
								GST_SEEK_TYPE_SET,
								0,
								GST_SEEK_TYPE_SET,
								m_iDurationInNs );
							play();

							}
							/*
							if(m_StopOnLoopComplete){
								stop();
								m_StopOnLoopComplete = false;
							} else {
								stop();
								setPosition(0);
								play();
							}
							*/

							break;


						case BIDIRECTIONAL_LOOP:
							std::cout << "bi-directional looping not implemented!" << std::endl;
							//m_PlayDirection = (PlayDirection)-m_PlayDirection;
							//stop();
							//play();
							break;

						default:
							break;
					}
					break;

				default:
					break;
				}
			}

			gst_message_unref( m_GstMessage );
		}
	}
}

void GStreamerWrapper::onEosFromVideoSource( GstAppSink* appsink, void* listener )
{
	( ( GStreamerWrapper *)listener )->videoEosCallback();
}

void GStreamerWrapper::onEosFromAudioSource( GstAppSink* appsink, void* listener )
{
	( ( GStreamerWrapper *)listener )->audioEosCallback();
}

GstFlowReturn GStreamerWrapper::onNewPrerollFromVideoSource( GstAppSink* appsink, void* listener )
{
	//GstBuffer* gstVideoSinkBuffer = gst_app_sink_pull_preroll( GST_APP_SINK( appsink ) );
	//( ( GStreamerWrapper *)listener )->newVideoSinkPrerollCallback( gstVideoSinkBuffer );
	//gst_buffer_unref( gstVideoSinkBuffer );

	GstSample* gstVideoSinkBuffer = gst_app_sink_pull_preroll( GST_APP_SINK( appsink ) );
	( ( GStreamerWrapper *)listener )->newVideoSinkPrerollCallback( gstVideoSinkBuffer );
	gst_sample_unref( gstVideoSinkBuffer );

	return GST_FLOW_OK;
}

GstFlowReturn GStreamerWrapper::onNewPrerollFromAudioSource( GstAppSink* appsink, void* listener )
{
	GstSample* gstAudioSinkBuffer = gst_app_sink_pull_preroll( GST_APP_SINK( appsink ) );
	( ( GStreamerWrapper * )listener )->newAudioSinkPrerollCallback( gstAudioSinkBuffer );
	gst_sample_unref( gstAudioSinkBuffer );

	return GST_FLOW_OK;
}

GstFlowReturn GStreamerWrapper::onNewBufferFromVideoSource( GstAppSink* appsink, void* listener )
{
	//Poco::Timestamp::TimeVal pre = Poco::Timestamp().epochMicroseconds();
	
	GstSample* gstVideoSinkBuffer = gst_app_sink_pull_sample( GST_APP_SINK( appsink ) );

	//Poco::Timestamp::TimeVal mid = Poco::Timestamp().epochMicroseconds();
	
	( ( GStreamerWrapper * )listener )->newVideoSinkBufferCallback( gstVideoSinkBuffer );

	//Poco::Timestamp::TimeVal copied = Poco::Timestamp().epochMicroseconds();
	
	gst_sample_unref( gstVideoSinkBuffer );

	//Poco::Timestamp::TimeVal post = Poco::Timestamp().epochMicroseconds();
	//std::cout <<  "mid: " << (float)(mid - pre) / 1000000.0f << " copied: " << (float)(copied - mid) / 1000000.0f << " post: " << (float)(post - copied) / 1000000.0f << std::endl;

	return GST_FLOW_OK;
}

GstFlowReturn GStreamerWrapper::onNewBufferFromAudioSource( GstAppSink* appsink, void* listener )
{
	GstSample* gstAudioSinkBuffer = gst_app_sink_pull_sample( GST_APP_SINK( appsink ) );
	( ( GStreamerWrapper * )listener )->newAudioSinkBufferCallback( gstAudioSinkBuffer );
	gst_sample_unref( gstAudioSinkBuffer );

	return GST_FLOW_OK;
}

void GStreamerWrapper::newVideoSinkPrerollCallback( GstSample* videoSinkSample )
{

	GstBuffer* buff = gst_sample_get_buffer(videoSinkSample);	


	GstMapInfo map;
	GstMapFlags flags = GST_MAP_READ;
	gst_buffer_map(buff, &map, flags);

	unsigned int videoBufferSize = map.size; //(unsigned int)(gst_buffer_get_size(buff));
	// Allocate memory for the video pixels according to the vide appsink buffer size
	if ( m_cVideoBuffer == NULL )
	{
		if(!m_PendingSeek) m_bIsNewVideoFrame = true;
	//	m_cVideoBuffer = new unsigned char[videoBufferSize];
	}
	
	// Copy the video appsink buffer data to our unsigned char array
	//memcpy( (unsigned char *)m_cVideoBuffer, (unsigned char *)GST_BUFFER_DATA( videoSinkBuffer ), videoBufferSize );
	memcpy( (unsigned char *)m_cVideoBuffer, map.data, videoBufferSize );

	//gst_sample_unref(videoSinkSample);
	gst_buffer_unmap(buff, &map);
	//gst_buffer_unref(buff);
}

void GStreamerWrapper::newVideoSinkBufferCallback( GstSample* videoSinkSample )
{
	if(!m_PendingSeek) m_bIsNewVideoFrame = true;

	GstBuffer* buff = gst_sample_get_buffer(videoSinkSample);	
	GstMapInfo map;
	GstMapFlags flags = GST_MAP_READ;
	gst_buffer_map(buff, &map, flags);

	// Copy the video appsink buffer data to our unsigned char array
	memcpy( (unsigned char *)m_cVideoBuffer, map.data, map.size );

	gst_buffer_unmap(buff, &map);
//	gst_buffer_unref(buff);
}

void GStreamerWrapper::videoEosCallback()
{

}

void GStreamerWrapper::newAudioSinkPrerollCallback( GstSample* audioSinkBuffer )
{
	//if ( m_cAudioBuffer == NULL )
	//{
	//	m_iAudioBufferSize = GST_BUFFER_SIZE( audioSinkBuffer );
	//	m_cAudioBuffer = new unsigned char[m_iAudioBufferSize];

	//	////////////////////////////////////////////////////////////////////////// AUDIO DATA

	//	/*
	//		Note: For some reason, with this version of GStreamer the only way to retrieve the audio metadata
	//		is to read the caps from the audio appsink buffer and via a GstStructure we can retrieve the needed
	//		values from the caps. After lots of research I also found another possibility by using GstAudioInfo
	//		but this struct is not available in this version.

	//		If a later version of GStreamer is ever compiled in a valid way so it can be used with Visual Studio
	//		it would definitely be a good idea to retrieve the audio information somewhere else in the code.
	//		But this piece of code does it well for now.
	//	*/

	//	// Get Audio metadata
	//	// http://gstreamer.freedesktop.org/data/doc/gstreamer/head/pwg/html/section-types-definitions.html
	//	GstCaps* audioCaps = gst_buffer_get_caps( audioSinkBuffer );
	//	GstStructure* gstStructure = gst_caps_get_structure( audioCaps, 0 );

	//	// Is audio data signed or not?
	//	gboolean isAudioSigned;
	//	gst_structure_get_boolean( gstStructure, "signed", &isAudioSigned );
	//	m_bIsAudioSigned = (bool)(isAudioSigned);

	//	// Number of channels
	//	gst_structure_get_int( gstStructure, "channels", &m_iNumAudioChannels );
	//	// Audio sample rate
	//	gst_structure_get_int( gstStructure, "rate", &m_iAudioSampleRate );
	//	// Audio width
	//	gst_structure_get_int( gstStructure, "width", &m_iAudioWidth );

	//	// Calculate the audio buffer size without the number of channels and audio width
	//	m_iAudioDecodeBufferSize = m_iAudioBufferSize / m_iNumAudioChannels / ( m_iAudioWidth / 8 );

	//	// Audio endianness
	//	gint audioEndianness;
	//	gst_structure_get_int( gstStructure, "endianness",  &audioEndianness );
	//	m_AudioEndianness = (Endianness)audioEndianness;

	//	gst_caps_unref( audioCaps );
	//}
	//else
	//{
	//	// The Audio Buffer size may change during runtime so we keep track if the buffer changes
	//	// If so, delete the old buffer and re-allocate it with the respective new buffer size
	//	int bufferSize = GST_BUFFER_SIZE( audioSinkBuffer );
	//	if ( m_iAudioBufferSize != bufferSize )
	//	{
	//		// Allocate the audio data array according to the audio appsink buffer size
	//		m_iAudioBufferSize = bufferSize;
	//		delete [] m_cAudioBuffer;
	//		m_cAudioBuffer = NULL;

	//		m_cAudioBuffer = new unsigned char[m_iAudioBufferSize];
	//	}
	//}

	//// Copy the audio appsink buffer data to our unsigned char array
	//memcpy( (unsigned char *)m_cAudioBuffer, (unsigned char *)GST_BUFFER_DATA( audioSinkBuffer ), GST_BUFFER_SIZE( audioSinkBuffer ) );
}

void GStreamerWrapper::newAudioSinkBufferCallback( GstSample* audioSinkBuffer )
{
	//// The Audio Buffer size may change during runtime so we keep track if the buffer changes
	//// If so, delete the old buffer and re-allocate it with the respective new buffer size
	//int bufferSize = GST_BUFFER_SIZE( audioSinkBuffer );

	//if ( m_iAudioBufferSize != bufferSize )
	//{
	//	m_iAudioBufferSize = bufferSize;
	//	delete [] m_cAudioBuffer;
	//	m_cAudioBuffer = NULL;

	//	m_cAudioBuffer = new unsigned char[m_iAudioBufferSize];

	//	// Recalculate the audio decode buffer size due to change in buffer size
	//	m_iAudioDecodeBufferSize = m_iAudioBufferSize / m_iNumAudioChannels / ( m_iAudioWidth / 8 );
	//}

	//// Copy the audio appsink buffer data to our unsigned char array
	//memcpy( (unsigned char *)m_cAudioBuffer, (unsigned char *)GST_BUFFER_DATA( audioSinkBuffer ), GST_BUFFER_SIZE( audioSinkBuffer ) );
}

void GStreamerWrapper::audioEosCallback()
{

}

void GStreamerWrapper::setVideoCompleteCallback( const std::function<void(GStreamerWrapper* video)> &func ){
	mVideoCompleteCallback = func;
}



};
