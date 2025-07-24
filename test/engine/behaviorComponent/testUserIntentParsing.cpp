/**
 * File: testUserIntentsParsing.cpp
 *
 * Author: ross
 * Created: 2018 Mar 20
 *
 * Description: tests that intents from various sources are parsed into valid user intents
 *
 * Copyright: Anki, Inc. 2018
 *
 **/


#include "coretech/common/engine/utils/data/dataPlatform.h"
#include "engine/aiComponent/behaviorComponent/userIntentComponent.h"
#include "engine/aiComponent/behaviorComponent/userIntentMap.h"
#include "engine/cozmoContext.h"
#include "gtest/gtest.h"
#include "test/engine/behaviorComponent/testBehaviorFramework.h"
#include "test/engine/behaviorComponent/testIntentsFramework.h"
#include "util/fileUtils/fileUtils.h"

#include "json/json.h"

#include <sstream>


using namespace Anki;
using namespace Anki::Vector;

extern CozmoContext* cozmoContext;

namespace {
  
TEST(UserIntentsParsing, CloudSampleFileParses)
{
  Json::Value config;
  
  // change to false to test locally
  if( true ) {
    
    // this unit test only runs as a required build step of the voice-intent-resolution-config, which
    // pulls victor master, sets ANKI_TEST_INTENT_SAMPLE_FILE, and runs the mac build
    std::string inFilename;
    const char* szFilename = getenv("ANKI_TEST_INTENT_SAMPLE_FILE");
    if( szFilename != nullptr ) {
      inFilename = szFilename;
    } else {
      return;
    }
    
    const std::string fileContents{ Util::FileUtils::ReadFile( inFilename ) };
    
    Json::Reader reader;
    const bool parsedOK = reader.parse(fileContents, config, false);
    EXPECT_TRUE(parsedOK);
    PRINT_NAMED_INFO("UserIntentsParsing.TestIntentSampleFile.ConfigSize", "Size of JSON root = %d", config.size());
    
  } else {
    
    // you need to generate this file first with makeSampleIntents.py
    const std::string jsonFilename = "test/aiTests/cloudIntentSamples.json";
    const bool parsedOK = cozmoContext->GetDataPlatform()->readAsJson(Util::Data::Scope::Resources,
                                                                      jsonFilename,
                                                                      config);
    EXPECT_TRUE( parsedOK );
    
  }
  
  EXPECT_FALSE( config.empty() );
  
  TestBehaviorFramework tbf;
  tbf.InitializeStandardBehaviorComponent();
  
  TestIntentsFramework tih;
  
  // all labels we consider "complete" should be hit once by the samples
  std::set<std::string> completedLabels = tih.GetCompletedLabels();
  std::set<std::string> sampleLabels;
  
  for( const auto& sample : config ) {
    std::stringstream ss;
    ss << sample;
    
    UserIntent intent;
    const bool parsed = tih.TryParseCloudIntent( tbf, ss.str(), &intent );
    EXPECT_TRUE( parsed ) << "could not parse sample cloud intent " << ss.str();
    
    if( intent.GetTag() != UserIntentTag::unmatched_intent ) {
      const auto& label = tih.GetLabelForIntent( intent );
      if(label.empty() ) {
        PRINT_NAMED_INFO("UserIntentsParsing.TestIntentSampleFile.SampleLabel",
                         "The label for %s is empty", ss.str().c_str());
      } else {
        PRINT_NAMED_INFO("UserIntentsParsing.TestIntentSampleFile.SampleLabel",
                         "The label for %s is %s", ss.str().c_str(), label.c_str());
        sampleLabels.insert( label );
      }
    }
  }
  
  // before we check that all labels we consider completed have a sample cloud
  // intent, display those labels for debugging
  auto itSamples = sampleLabels.begin();
  while( itSamples != sampleLabels.end() ) {
    PRINT_NAMED_INFO("UserIntentsParsing.CloudSampleFileParses.SampleLabels", "[ %s ]", itSamples->c_str());
    ++itSamples;
  }
  auto itCompleted = completedLabels.begin();
  while( itCompleted != completedLabels.end() ) {
    PRINT_NAMED_INFO("UserIntentsParsing.CloudSampleFileParses.CompletedLabels", "[ %s ]", itCompleted->c_str());
    ++itCompleted;
  }

  // check that all labels we consider completed have a sample cloud intent
  itSamples = sampleLabels.begin();
  itCompleted = completedLabels.begin();
  while( itSamples != sampleLabels.end() && itCompleted != completedLabels.end() ) {
    while( itSamples != sampleLabels.end() ) {
      if( *itSamples == *itCompleted ) {
        ++itSamples;
        break;
      } else {
        ++itSamples;
      }
    }
    ++itCompleted;
  }
  
  EXPECT_FALSE(itSamples == sampleLabels.end() && itCompleted != completedLabels.end())
    << "Could not find " << *itCompleted << " and maybe more ";
  
  // it should be ok if user_intent_map has a cloud intent that isnt completed, but it should
  // exist in dialogflow samples; otherwise it should be considered garbage.
  UserIntentMap intentMap( cozmoContext->GetDataLoader()->GetUserIntentConfig(), cozmoContext );
  std::vector<std::string> cloudIntentsList = intentMap.DevGetCloudIntentsList();
  for( const auto& cloudName : cloudIntentsList ) {
    // if this cloud intent has "test_parsing" set to false in user_intent_map, then
    // skip it and do not check if it exists in Dialogflow sample file
    if( !intentMap.GetTestParsingBoolFromCloudIntent(cloudName) ) {
      continue;
    }
    //PRINT_NAMED_INFO("UserIntentsParsing.CloudSampleFileParses.CheckingDialogflowSamples",
    //                 "Looking for Dialogflow sample that matches '%s'", cloudName.c_str());
    bool found = false;
    for( const auto& elem : config ) {
      if( elem["intent"] == cloudName ) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE( found ) << "Could not find user_intent_map cloud intent " << cloudName << " in sample file";
  }
}

TEST(UserIntentsParsing, CompletedInCloudList)
{
  // tests that every completed intent has a match for both app and cloud in user_intent_map.
  
  UserIntentMap intentMap( cozmoContext->GetDataLoader()->GetUserIntentConfig(), cozmoContext );
  
  std::vector<std::string> cloudIntentsList = intentMap.DevGetCloudIntentsList();
  std::vector<std::string> appIntentsList = intentMap.DevGetAppIntentsList();
  
  TestIntentsFramework tih;
  std::set<std::string> completedLabels = tih.GetCompletedLabels();
  for( const auto& completedLabel : completedLabels ) {
    const auto intent = tih.GetCompletedIntent(completedLabel);
    const auto intentTag = intent.GetTag();
    
    // does this exist in cloudIntentsList AND appIntentsList?
    auto itCloud = std::find_if( cloudIntentsList.begin(), cloudIntentsList.end(), [&](const auto& x ) {
      return (intentMap.GetUserIntentFromCloudIntent( x ) == intentTag);
    });
    EXPECT_TRUE( itCloud != cloudIntentsList.end() ) << "Could not find " << UserIntentTagToString(intentTag) << " in cloud intent list";
    
    // app intents haven't been fully realized;
    // commenting out this test until app intents are actually required
    //auto itApp = std::find_if( appIntentsList.begin(), appIntentsList.end(), [&](const auto& x ) {
    //  return (intentMap.GetUserIntentFromAppIntent( x ) == intentTag);
    //});
    //EXPECT_TRUE( itApp != appIntentsList.end() ) << "Could not find " << UserIntentTagToString(intentTag) << " in app intent list";
  }
}


} // namespace
