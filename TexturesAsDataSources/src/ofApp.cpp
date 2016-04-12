#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup() 
{
	fontSmall.load("Fonts/DIN.otf", 8);

    ofDisableArbTex();

	lights.push_back( new ofLightExt() );
	lights.push_back( new ofLightExt() );
	lights.push_back( new ofLightExt() );
	for( int i = 0; i < lights.size(); i++ ) { lights.at(i)->enable(); }
	
	ofBackground(ofColor::black);

	string mainSettingsPath = "Settings/Main.xml";
	gui.setup("Main", mainSettingsPath);
	
	gui.add( globalAmbient.set("Global Ambient", ofColor::black, ofColor(0,0,0,0), ofColor(255)));
	for( int i = 0; i < lights.size(); i++ )
	{
		lights.at(i)->addToPanel( &gui, "Light " + ofToString(i), 60, true );
	}
	
	floorMaterial.addToPanel( &gui, "Floor Material" );
	dancerMaterial.addToPanel( &gui, "Dancer Material" );
	spheresMaterial.addToPanel( &gui, "Spheres Material" );
	
	gui.loadFromFile(mainSettingsPath);
	gui.minimizeAll();


	//string filename = "Models/ManLowPoly_DanceStep3.fbx";
	string filename = "Models/WomanLowPoly_DanceStep3.fbx";
	ofMatrix4x4 meshBaseTransform = ofMatrix4x4::newScaleMatrix(0.01, 0.01, 0.01);
	meshBaseTransform.translate(0, 0.22, 0); // move it up a bit
	dancerMesh.load( filename );
	dancerMesh.setBaseTransform( meshBaseTransform );

	// Mask out areas to emit particles by loading something in here, needs to match the UVs of your model
	emissionMask.load( "EmissionMasks/EmissionMaskBackAndArms.png");
	
	lightingShader.load( "Shaders/BlinnPhongRadius/GL3/BlinnPhongRadius" );
	
	float tmpHeight = 1.93;
	camera.setAutoDistance(false);
	camera.setNearClip(0.01f);
	camera.setPosition(0, tmpHeight, -1.0);
	camera.lookAt(ofVec3f(0, tmpHeight*0.8, 1));

	floor.set(200, 200, 2, 2);
	floor.rotate(-90, ofVec3f(1, 0, 0));
	floor.move(ofVec3f(0, 0, 0));

	textureSize = 128;
	maxAge = 1.1;
	
	// Load shaders
	ofSetLogLevel( OF_LOG_VERBOSE );
	particleDraw.load("Shaders/Spheres/GL3/DrawInstancedGeometry");
	particleUpdate.load("Shaders/Spheres/GL3/Update");
	
	float radius = 0.01;
	singleParticleMesh = ofSpherePrimitive( radius, 6, OF_PRIMITIVE_TRIANGLES ).getMesh();
	
	particleDataFbo.allocateAsData(textureSize, textureSize, GL_RGBA32F, 2);
	
	// Initialise the starting and static data
	vector<ofVec4f> startPositionsAndAge;
	
	for( int x = 0; x < textureSize*textureSize; x++ )
	{
		ofVec3f pos = ofVec3f(0,1000,0); // move them way off to start with
		float startAge = ofRandom( maxAge ); // position is not very important, but age is, by setting the lifetime randomly somewhere in the middle we can get a steady stream emitting
		startPositionsAndAge.push_back( ofVec4f(pos.x, pos.y, pos.z, startAge) );
	}
	
	
	// Upload it to the source texture
	particleDataFbo.source()->getTexture(0).loadData( (float*)&startPositionsAndAge[0].x,	 textureSize, textureSize, GL_RGBA );
	
	ofDisableTextureEdgeHack();
	ofDisableArbTex();
	spawnPosTexture.allocate( textureSize, textureSize, GL_RGBA32F, GL_RGBA, GL_FLOAT );
	spawnVelTexture.allocate( textureSize, textureSize, GL_RGBA32F, GL_RGBA, GL_FLOAT );
	
	// If we do any interpolaton we can't use these as data arrays
	spawnPosTexture.setTextureMinMagFilter( GL_NEAREST, GL_NEAREST );
	spawnVelTexture.setTextureMinMagFilter( GL_NEAREST, GL_NEAREST );
	
	drawGui = false;
}

//--------------------------------------------------------------
void ofApp::update() 
{    
	float t = ofGetElapsedTimef();

	ofSetGlobalAmbientColor( globalAmbient.get() );
	dancerMesh.update( ofGetElapsedTimef() );
	
	int numUniquePoints = dancerMesh.triangleMesh.getNumVertices() / 10;
	dancerMesh.updateRandomPoints( numUniquePoints, uniqueSpawnPoints );

	// Randomly grab from the unique spawn points to make data for every particle, even if it doesn't use it this frame
	spawnVelScratch.clear();
	spawnPosScratch.clear();
	for( int i = 0; i < textureSize*textureSize; i++ )
	{
		int randID = floorf(ofRandom(uniqueSpawnPoints.size()));
		
		spawnPosScratch.push_back( uniqueSpawnPoints.at(randID).pos );
		spawnVelScratch.push_back( uniqueSpawnPoints.at(randID).vel ); // We could mix in some velocity in the direction of the normal here
	}
	
	spawnPosTexture.loadData( (float*)&spawnPosScratch[0].x,	 textureSize, textureSize, GL_RGBA );
	spawnVelTexture.loadData( (float*)&spawnVelScratch[0].x,	 textureSize, textureSize, GL_RGBA );
	
	ofSetWindowTitle(ofToString(ofGetFrameRate(), 1));
}

//--------------------------------------------------------------
void ofApp::draw()
{
	
	ofDisableTextureEdgeHack(); // Important on devices that don't support NPOT textures!
	
	ofEnableBlendMode( OF_BLENDMODE_DISABLED ); // Important! We just want to write the data as is to the target fbo
	ofSetColor( ofColor::white );
	
	particleDataFbo.dest()->begin();
	
	particleDataFbo.dest()->activateAllDrawBuffers(); // if we have multiple color buffers in our FBO we need this to activate all of them
	
	particleUpdate.begin();
	
	particleUpdate.setUniformTexture( "particlePosAndAgeTexture",	particleDataFbo.source()->getTexture(0), 1 );
	//particleUpdate.setUniformTexture( "particleOldPosAndAgeTexture", particleDataFbo.dest()->getTexture(0), 2 );
	particleUpdate.setUniformTexture( "particleVelTexture", particleDataFbo.source()->getTexture(1), 2 );
	
	particleUpdate.setUniformTexture( "spawnPositionTexture", spawnPosTexture, 3 );
	particleUpdate.setUniformTexture( "spawnVelocityTexture", spawnVelTexture, 4 );
	
	particleUpdate.setUniform1f( "maxAge", maxAge );
	
	particleDataFbo.source()->draw(0,0);
	
	particleUpdate.end();
	
	particleDataFbo.dest()->end();
	
	particleDataFbo.swap();
	
	
	
	ofEnableDepthTest();

    camera.begin();


		lightingShader.begin();

			ofLightExt::setParams( &lightingShader, lights, ofGetCurrentMatrix(OF_MATRIX_MODELVIEW), false );
	
			floorMaterial.setParams( &lightingShader, false );
			floor.draw();

			dancerMaterial.setParams( &lightingShader, false );
			//dancerMesh.triangleMesh.draw();

		lightingShader.end();

		//DancerMesh::drawVelocities( uniqueSpawnPoints, 1.0 );

		ofSetColor( ofColor::white );
		ofEnableBlendMode( OF_BLENDMODE_ALPHA );
		
		particleDraw.begin();
		
		ofLightExt::setParams( &particleDraw, lights, ofGetCurrentMatrix(OF_MATRIX_MODELVIEW), false );
		
		particleDraw.setUniformTexture("particlePosAndAgeTexture", particleDataFbo.source()->getTexture(0),			1 );
		particleDraw.setUniformTexture("particleVelTexture", particleDataFbo.source()->getTexture(1),				2 );
		
		particleDraw.setUniformTexture( "spawnPositionTexture", spawnPosTexture,									4 );
		particleDraw.setUniformTexture( "spawnVelocityTexture", spawnVelTexture,									5 );
		
		particleDraw.setUniform2f("resolution", particleDataFbo.source()->getWidth(), particleDataFbo.source()->getHeight() );
		particleDraw.setUniform1f( "maxAge", maxAge );
		particleDraw.setUniformMatrix4f("normalMatrix", ofGetCurrentNormalMatrix() );

		spheresMaterial.setParams( &particleDraw, false );
	
		singleParticleMesh.drawInstanced( OF_MESH_FILL, textureSize*textureSize );
		
		particleDraw.end();

	
	
		if( drawGui )
		{
			for( unsigned int i = 0; i < lights.size(); i++ )
			{
				lights.at(i)->draw( 0.2 );
			}
		}
	
    camera.end();
	
	ofDisableDepthTest();

	if (drawGui)
	{
		gui.draw();
		fontSmall.drawStringShadowed(ofToString(ofGetFrameRate(), 1), ofGetWidth() - 30, ofGetHeight() - 15);
	}
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key)
{
	if (key == OF_KEY_TAB)
	{
		drawGui = !drawGui;
	}
	else if (key == ' ')
	{
	}
	else if (key == 'f')
	{
		ofToggleFullscreen();
	}
}




