/*
 *  ARWrapperNativeCarsExample.cpp
 *  ARToolKit for Android
 *
 *  Disclaimer: IMPORTANT:  This Daqri software is supplied to you by Daqri
 *  LLC ("Daqri") in consideration of your agreement to the following
 *  terms, and your use, installation, modification or redistribution of
 *  this Daqri software constitutes acceptance of these terms.  If you do
 *  not agree with these terms, please do not use, install, modify or
 *  redistribute this Daqri software.
 *
 *  In consideration of your agreement to abide by the following terms, and
 *  subject to these terms, Daqri grants you a personal, non-exclusive
 *  license, under Daqri's copyrights in this original Daqri software (the
 *  "Daqri Software"), to use, reproduce, modify and redistribute the Daqri
 *  Software, with or without modifications, in source and/or binary forms;
 *  provided that if you redistribute the Daqri Software in its entirety and
 *  without modifications, you must retain this notice and the following
 *  text and disclaimers in all such redistributions of the Daqri Software.
 *  Neither the name, trademarks, service marks or logos of Daqri LLC may
 *  be used to endorse or promote products derived from the Daqri Software
 *  without specific prior written permission from Daqri.  Except as
 *  expressly stated in this notice, no other rights or licenses, express or
 *  implied, are granted by Daqri herein, including but not limited to any
 *  patent rights that may be infringed by your derivative works or by other
 *  works in which the Daqri Software may be incorporated.
 *
 *  The Daqri Software is provided by Daqri on an "AS IS" basis.  DAQRI
 *  MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION
 *  THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE, REGARDING THE DAQRI SOFTWARE OR ITS USE AND
 *  OPERATION ALONE OR IN COMBINATION WITH YOUR PRODUCTS.
 *
 *  IN NO EVENT SHALL DAQRI BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL
 *  OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION,
 *  MODIFICATION AND/OR DISTRIBUTION OF THE DAQRI SOFTWARE, HOWEVER CAUSED
 *  AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE),
 *  STRICT LIABILITY OR OTHERWISE, EVEN IF DAQRI HAS BEEN ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  Copyright 2015 Daqri LLC. All Rights Reserved.
 *  Copyright 2011-2015 ARToolworks, Inc. All Rights Reserved.
 *
 *  Author(s): Julian Looser, Philip Lamb
 */

#include <AR/gsub_es.h>
#include <Eden/glm.h>
#include <assert.h>
#include <jni.h>
#include <ARWrapper/ARToolKitWrapperExportedAPI.h>
#include <unistd.h> // chdir()
#include <android/log.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <assert.h>
#include <android/asset_manager.h>
#include <pthread.h>

#include <android/asset_manager_jni.h>

// Utility preprocessor directive so only one change needed if Java class name changes
#define JNIFUNCTION_DEMO(sig) Java_org_artoolkit_ar_samples_ARSimpleNativeCars_SimpleNativeRenderer_##sig

extern "C" {
	JNIEXPORT void JNICALL JNIFUNCTION_DEMO(demoInitialise(JNIEnv* env, jobject object, jobject assetManager));
	JNIEXPORT void JNICALL JNIFUNCTION_DEMO(demoShutdown(JNIEnv* env, jobject object));
	JNIEXPORT void JNICALL JNIFUNCTION_DEMO(demoSurfaceCreated(JNIEnv* env, jobject object));
	JNIEXPORT void JNICALL JNIFUNCTION_DEMO(demoSurfaceChanged(JNIEnv* env, jobject object, jint w, jint h));
	JNIEXPORT void JNICALL JNIFUNCTION_DEMO(demoDrawFrame(JNIEnv* env, jobject obj));	
};

typedef struct ARModel {
	int patternID;
	ARdouble transformationMatrix[16];
	bool visible;
	GLMmodel* obj;
} ARModel;

#define NUM_MODELS 7
static ARModel models[NUM_MODELS] = {0};

static float lightAmbient[4] = {0.1f, 0.1f, 0.1f, 1.0f};
static float lightDiffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
static float lightPosition[4] = {0.0f, 0.0f, 1.0f, 0.0f};

//sound
//static SLObjectItf outputMixObject = NULL;
//static SLEngineItf engineEngine;
//static SLObjectItf fdPlayerObject = NULL;
//static SLPlayItf fdPlayerPlay;
// engine interfaces
static SLObjectItf engineObject = NULL;
static SLEngineItf engineEngine;

// output mix interfaces
static SLObjectItf outputMixObject = NULL;
static SLEnvironmentalReverbItf outputMixEnvironmentalReverb = NULL;

// buffer queue player interfaces
static SLObjectItf bqPlayerObject = NULL;
static SLPlayItf bqPlayerPlay;
static SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue;
static SLEffectSendItf bqPlayerEffectSend;
static SLMuteSoloItf bqPlayerMuteSolo;
static SLVolumeItf bqPlayerVolume;
static SLmilliHertz bqPlayerSampleRate = 0;
static jint   bqPlayerBufSize = 0;
static short *resampleBuf = NULL;
// a mutext to guard against re-entrance to record & playback
// as well as make recording and playing back to be mutually exclusive
// this is to avoid crash at situations like:
//    recording is in session [not finished]
//    user presses record button and another recording coming in
// The action: when recording/playing back is not finished, ignore the new request
static pthread_mutex_t  audioEngineLock = PTHREAD_MUTEX_INITIALIZER;

// aux effect on the output mix, used by the buffer queue player
static const SLEnvironmentalReverbSettings reverbSettings =
    SL_I3DL2_ENVIRONMENT_PRESET_STONECORRIDOR;

// URI player interfaces
static SLObjectItf uriPlayerObject = NULL;
static SLPlayItf uriPlayerPlay;
static SLSeekItf uriPlayerSeek;
static SLMuteSoloItf uriPlayerMuteSolo;
static SLVolumeItf uriPlayerVolume;

// file descriptor player interfaces
static SLObjectItf fdPlayerObject = NULL;
static SLPlayItf fdPlayerPlay;
static SLSeekItf fdPlayerSeek;
static SLMuteSoloItf fdPlayerMuteSolo;
static SLVolumeItf fdPlayerVolume;

// recorder interfaces
static SLObjectItf recorderObject = NULL;
static SLRecordItf recorderRecord;
static SLAndroidSimpleBufferQueueItf recorderBufferQueue;

// synthesized sawtooth clip
#define SAWTOOTH_FRAMES 8000
static short sawtoothBuffer[SAWTOOTH_FRAMES];

// 5 seconds of recorded audio at 16 kHz mono, 16-bit signed little endian
#define RECORDER_FRAMES (16000 * 5)
static short recorderBuffer[RECORDER_FRAMES];
static unsigned recorderSize = 0;

// pointer and size of the next player buffer to enqueue, and number of remaining buffers
static short *nextBuffer;
static unsigned nextSize;
static int nextCount;

JNIEXPORT void JNICALL JNIFUNCTION_DEMO(demoInitialise(JNIEnv* env, jobject object,  jobject assetManager)) {

    const char *model0file = "Data/models/wolfD/wolfD.obj";

	const char *model1file = "Data/models/horseD/horseD.obj";

	const char *model2file = "Data/models/cat/catD.obj";

	const char *model3file = "Data/models/giraffe/giraffeD.obj";

	const char *model4file = "Data/models/dog/dogD.obj";

	const char *model5file = "Data/models/deer/deerD.obj";

	const char *model6file = "Data/models/bear/bearD.obj";


    const char *utf8 = "background.mp3";
   // const char *utf8 = (*env)->GetStringUTFChars(env, filename, NULL);
//open an asset
      SLresult result;

    AAssetManager *mgr = AAssetManager_fromJava(env, assetManager);
    assert(NULL != mgr);
    AAsset* asset = AAssetManager_open(mgr, utf8, AASSET_MODE_UNKNOWN);


    off_t start, length;
    int fd = AAsset_openFileDescriptor(asset, &start, &length);
   // assert(0 <= fd);
   // AAsset_close(asset);

    //configure audio source
    SLDataLocator_AndroidFD loc_fd = {SL_DATALOCATOR_ANDROIDFD, fd, start, length};
    SLDataFormat_MIME format_mime = {SL_DATAFORMAT_MIME, NULL, SL_CONTAINERTYPE_UNSPECIFIED};
    SLDataSource audioSrc = {&loc_fd, &format_mime};


    // configure audio sink
    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
    SLDataSink audioSnk = {&loc_outmix, NULL};

    // create audio player
  const SLInterfaceID ids[3] = {SL_IID_BUFFERQUEUE, SL_IID_VOLUME, SL_IID_EFFECTSEND,
                                      /*SL_IID_MUTESOLO,*/};
      const SLboolean req[3] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE,
                                     /*SL_BOOLEAN_TRUE,*/ };

     // result = (*engineEngine)->CreateAudioPlayer(engineEngine, &bqPlayerObject, &audioSrc, &audioSnk,
     //        3, ids, req);
     // assert(SL_RESULT_SUCCESS == result);
      //(void)result;
//
    // realize the player
    result = (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);
        assert(SL_RESULT_SUCCESS == result);
        (void)result;



	//PRIMUL MODEL - LUP
	models[0].patternID = arwAddMarker("single;Data/patt.wolf;80");
	arwSetMarkerOptionBool(models[0].patternID, ARW_MARKER_OPTION_SQUARE_USE_CONT_POSE_ESTIMATION, false);
	arwSetMarkerOptionBool(models[0].patternID, ARW_MARKER_OPTION_FILTERED, true);

	models[0].obj = glmReadOBJ2(model0file, 0, 0); // context 0, don't read textures yet.
	if (!models[0].obj) {
		LOGE("Error loading model from file '%s'.", model0file);
		exit(-1);
	}
	glmScale(models[0].obj, 1.0f);
	glEnable(GL_TEXTURE_2D);
    models[0].obj->flipTextureV = true;
	glmRotate(models[0].obj, 3.14159f / 2.0f, 1.0f, 0.0f, 0.0f);
	//glmRotate(models[0].obj, 3.14159f / 2.0f, 0.0f, 1.0f, 0.0f);
	glmCreateArrays(models[0].obj, GLM_SMOOTH | GLM_MATERIAL | GLM_TEXTURE);
	models[0].visible = false;


	//AL DOILEA MODEL
	models[1].patternID = arwAddMarker("single;Data/patt.yourpatt;80");
	arwSetMarkerOptionBool(models[1].patternID, ARW_MARKER_OPTION_SQUARE_USE_CONT_POSE_ESTIMATION, false);
	arwSetMarkerOptionBool(models[1].patternID, ARW_MARKER_OPTION_FILTERED, true);

	models[1].obj = glmReadOBJ2(model1file, 0, 0); // context 0, don't read textures yet.
	if (!models[1].obj) {
		LOGE("Error loading model from file '%s'.", model1file);
		exit(-1);
	}
	glmScale(models[1].obj, 1.0f);
	glEnable(GL_TEXTURE_2D);
    models[1].obj->flipTextureV = true;
	glmRotate(models[1].obj, 3.14159f / 2.0f, 1.0f, 0.0f, 0.0f);
	glmCreateArrays(models[1].obj, GLM_SMOOTH | GLM_MATERIAL | GLM_TEXTURE);
	models[1].visible = false;



	//AL TREILEA MODEL
	models[2].patternID = arwAddMarker("single;Data/patt.kitty;80");
	arwSetMarkerOptionBool(models[2].patternID, ARW_MARKER_OPTION_SQUARE_USE_CONT_POSE_ESTIMATION, false);
	arwSetMarkerOptionBool(models[2].patternID, ARW_MARKER_OPTION_FILTERED, true);
	models[2].obj = glmReadOBJ2(model2file, 0, 0); // context 0, don't read textures yet.
	if (!models[2].obj) {
		LOGE("Error loading model from file '%s'.", model2file);
		exit(-1);
	}

	glmScale(models[2].obj, 1.0f);
	glEnable(GL_TEXTURE_2D);
    models[2].obj->flipTextureV = true;
	glmRotate(models[2].obj, 3.14159f / 2.0f, 1.0f, 0.0f, 0.0f);
	glmCreateArrays(models[2].obj, GLM_SMOOTH | GLM_MATERIAL | GLM_TEXTURE);
	models[2].visible = false;



	//AL PATRULEA MODEL
	models[3].patternID = arwAddMarker("single;Data/patt.giraffe;80");
	arwSetMarkerOptionBool(models[3].patternID, ARW_MARKER_OPTION_SQUARE_USE_CONT_POSE_ESTIMATION, false);
	arwSetMarkerOptionBool(models[3].patternID, ARW_MARKER_OPTION_FILTERED, true);
	models[3].obj = glmReadOBJ2(model3file, 0, 0); // context 0, don't read textures yet.
	if (!models[3].obj) {
		LOGE("Error loading model from file '%s'.", model3file);
		exit(-1);
	}

	glmScale(models[3].obj, 1.0f);
	glEnable(GL_TEXTURE_2D);
    models[3].obj->flipTextureV = true;
	glmRotate(models[3].obj, 3.14159f / 2.0f, 1.0f, 0.0f, 0.0f);
	glmCreateArrays(models[3].obj, GLM_TEXTURE);
	models[3].visible = false;



	//AL 5lea MODEL
	models[4].patternID = arwAddMarker("single;Data/patt.dog;80");
	arwSetMarkerOptionBool(models[4].patternID, ARW_MARKER_OPTION_SQUARE_USE_CONT_POSE_ESTIMATION, false);
	arwSetMarkerOptionBool(models[4].patternID, ARW_MARKER_OPTION_FILTERED, true);
	models[4].obj = glmReadOBJ2(model4file, 0, 0); // context 0, don't read textures yet.
	if (!models[4].obj) {
	LOGE("Error loading model from file '%s'.", model4file);
	exit(-1);
	}

	glmScale(models[4].obj, 1.0f);
	glEnable(GL_TEXTURE_2D);
    models[4].obj->flipTextureV = true;
	glmRotate(models[4].obj, 3.14159f / 2.0f, 1.0f, 0.0f, 0.0f);
	glmCreateArrays(models[4].obj, GLM_SMOOTH | GLM_MATERIAL | GLM_TEXTURE);
	models[4].visible = false;


    //model 6 -deer
    models[5].patternID = arwAddMarker("single;Data/patt.deer;80");
    arwSetMarkerOptionBool(models[5].patternID, ARW_MARKER_OPTION_SQUARE_USE_CONT_POSE_ESTIMATION, false);
    arwSetMarkerOptionBool(models[5].patternID, ARW_MARKER_OPTION_FILTERED, true);
    models[5].obj = glmReadOBJ2(model5file, 0, 0); // context 0, don't read textures yet.
    if (!models[5].obj) {
    LOGE("Error loading model from file '%s'.", model5file);
    exit(-1);
   	}

   	glmScale(models[5].obj, 1.0f);
   	glEnable(GL_TEXTURE_2D);
    models[5].obj->flipTextureV = true;
    glmRotate(models[5].obj, 3.14159f / 2.0f, 1.0f, 0.0f, 0.0f);
    glmCreateArrays(models[5].obj, GLM_SMOOTH | GLM_MATERIAL | GLM_TEXTURE);
    models[5].visible = false;


    //model 6 -deer
    models[6].patternID = arwAddMarker("single;Data/patt.bear;80");
    arwSetMarkerOptionBool(models[6].patternID, ARW_MARKER_OPTION_SQUARE_USE_CONT_POSE_ESTIMATION, false);
    arwSetMarkerOptionBool(models[6].patternID, ARW_MARKER_OPTION_FILTERED, true);
    models[6].obj = glmReadOBJ2(model6file, 0, 0); // context 0, don't read textures yet.
    if (!models[6].obj) {
      LOGE("Error loading model from file '%s'.", model6file);
        exit(-1);
       	}

       	glmScale(models[6].obj, 1.0f);
       	glEnable(GL_TEXTURE_2D);
        models[6].obj->flipTextureV = true;
        glmRotate(models[6].obj, 3.14159f / 2.0f, 1.0f, 0.0f, 0.0f);
        glmCreateArrays(models[6].obj, GLM_SMOOTH | GLM_MATERIAL | GLM_TEXTURE);
        models[6].visible = false;
}

JNIEXPORT void JNICALL JNIFUNCTION_DEMO(demoShutdown(JNIEnv* env, jobject object)) {
}

JNIEXPORT void JNICALL JNIFUNCTION_DEMO(demoSurfaceCreated(JNIEnv* env, jobject object)) {
	glStateCacheFlush(); // Make sure we don't hold outdated OpenGL state.
	for (int i = 0; i < NUM_MODELS; i++) {
	    if (models[i].obj) {
	        glmDelete(models[i].obj, 0);
	        models[i].obj = NULL;
	    }
	}
}

JNIEXPORT void JNICALL JNIFUNCTION_DEMO(demoSurfaceChanged(JNIEnv* env, jobject object, jint w, jint h)) {
	// glViewport(0, 0, w, h) has already been set.
}

JNIEXPORT void JNICALL JNIFUNCTION_DEMO(demoDrawFrame(JNIEnv* env, jobject obj)) {
	
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 

    // Set the projection matrix to that provided by ARToolKit.
	float proj[16];
	arwGetProjectionMatrix(proj);
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(proj);
	glMatrixMode(GL_MODELVIEW);
	
	glStateCacheEnableDepthTest();
	glStateCacheEnableLighting();	
	glEnable(GL_LIGHT0);
	
	for (int i = 0; i < NUM_MODELS; i++) {		
		models[i].visible = arwQueryMarkerTransformation(models[i].patternID, models[i].transformationMatrix);		
			
		if (models[i].visible) {

			glLoadMatrixf(models[i].transformationMatrix);		
			
			glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmbient);
			glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiffuse);
			glLightfv(GL_LIGHT0, GL_POSITION, lightPosition);

			glmDrawArrays(models[i].obj, 0);

		}

	}
	
}
