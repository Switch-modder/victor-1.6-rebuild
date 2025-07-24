/**
 * File: testUserIntentMap.cpp
 *
 * Author: Brad Neuman
 * Created: 2018-02-01
 *
 * Description: Test for the user intent map
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#include "gtest/gtest.h"

#include "engine/aiComponent/behaviorComponent/userIntentComponent.h"
#include "engine/aiComponent/behaviorComponent/userIntentData.h"
#include "engine/aiComponent/behaviorComponent/userIntents.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/robot.h"

#include "clad/externalInterface/messageGameToEngine.h"
#include "clad/types/behaviorComponent/userIntent.h"

#include "coretech/common/engine/utils/timer.h"

#include "test/engine/behaviorComponent/testBehaviorFramework.h"
#include "test/engine/callWithoutError.h"

#include <json/json.h>

#include <string>


using namespace Anki;
using namespace Anki::Vector;

namespace {

const std::string& testMapConfig = R"json(
{
  "user_intent_map": [
    {
      "cloud_intent": "cloud_intent_1",
      "user_intent": "test_user_intent_1"
    },
    {
      "cloud_intent": "cloud_intent_2",
      "user_intent": "test_user_intent_2"
    },
    {
      "cloud_intent": "cloud_time_intent",
      "user_intent": "set_timer"
    },
    {
      "cloud_intent": "cloud_name_intent",
      "user_intent": "test_name"
    },
    {
      "cloud_intent": "cloud_time_intent_substitution",
      "user_intent": "test_timeWithUnits",
      "cloud_substitutions": {
        "timer_duration.time": "time",
        "timer_duration.units": "units"
      },
      "cloud_numerics": ["timer_duration.time"]
    },
    {
      "app_intent": "intent_meet_victor",
      "user_intent": "meet_victor",
      "app_substitutions": {
        "param": "username"
      }
    }
  ],

  "unmatched_intent": "unmatched_intent",
  
  "is_test": true // ignore a data validation step that ensures the above contains ALL clad enum values
})json";

std::string GetSimpleCloudJson(const std::string& request)
{
  std::string jsonIntent = "{\"intent\": \"" + request + "\"}";
  return jsonIntent;
}

}

void CreateComponent(const std::string& json, std::unique_ptr<UserIntentComponent>& comp, const Robot& robot)
{
  Json::Reader reader;
  Json::Value config;
  const bool parsedOK = reader.parse(json, config, false);
  ASSERT_TRUE(parsedOK);

  comp.reset(new UserIntentComponent(robot, config));
}
  
void Reset(UserIntent& intent)
{
  intent = UserIntent::Createunmatched_intent({});
}
  
TEST(UserIntentMap, CreateComponent)
{
  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  const Robot& robot = testBehaviorFramework.GetRobot();
  std::unique_ptr<UserIntentComponent> comp;
  CreateComponent(testMapConfig, comp, robot);

  ASSERT_TRUE(comp != nullptr);
}

TEST(UserIntentMap, UserIntentsValid)
{
  EXPECT_TRUE(IsValidUserIntentTag("test_user_intent_1"));
  EXPECT_TRUE(IsValidUserIntentTag("test_user_intent_2"));
  EXPECT_FALSE(IsValidUserIntentTag("test_user_intent_3"));
  EXPECT_FALSE(IsValidUserIntentTag("cloud_intent_1"));
}

TEST(UserIntentMap, TriggerWord)
{
  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  const Robot& robot = testBehaviorFramework.GetRobot();
  std::unique_ptr<UserIntentComponent> comp;
  CreateComponent(testMapConfig, comp, robot);

  EXPECT_FALSE(comp->IsTriggerWordPending());

  comp->SetTriggerWordPending(true);
  EXPECT_TRUE(comp->IsTriggerWordPending());

  comp->ClearPendingTriggerWord();
  EXPECT_FALSE(comp->IsTriggerWordPending());

  comp->SetTriggerWordPending(true);
  EXPECT_TRUE(comp->IsTriggerWordPending());

  comp->SetTriggerWordPending(true);
  EXPECT_TRUE(comp->IsTriggerWordPending());

  comp->ClearPendingTriggerWord();
  EXPECT_FALSE(comp->IsTriggerWordPending());  
}

TEST(UserIntentMap, UserIntent)
{
  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  const Robot& robot = testBehaviorFramework.GetRobot();
  std::unique_ptr<UserIntentComponent> comp;
  CreateComponent(testMapConfig, comp, robot);

  EXPECT_FALSE(comp->IsAnyUserIntentPending());
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));

  comp->DevSetUserIntentPending(USER_INTENT(test_user_intent_1), UserIntentSource::Voice);
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(unmatched_intent)));

  comp->ActivateUserIntent(USER_INTENT(test_user_intent_1), "test", false);
  EXPECT_FALSE(comp->IsAnyUserIntentPending());
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));
  EXPECT_TRUE(comp->IsUserIntentActive(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(unmatched_intent)));
  EXPECT_EQ(comp->GetActiveUserIntent()->source, UserIntentSource::Voice);

  comp->DeactivateUserIntent(USER_INTENT(test_user_intent_1));
  EXPECT_FALSE(comp->IsAnyUserIntentPending());
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(unmatched_intent)));

  
  comp->DevSetUserIntentPending(USER_INTENT(test_user_intent_2), UserIntentSource::App);
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(unmatched_intent)));

  comp->ActivateUserIntent(USER_INTENT(test_user_intent_2), "test", false);
  EXPECT_FALSE(comp->IsAnyUserIntentPending());
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(test_user_intent_1)));
  EXPECT_TRUE(comp->IsUserIntentActive(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(unmatched_intent)));
  EXPECT_EQ(comp->GetActiveUserIntent()->source, UserIntentSource::App);
  
  comp->DeactivateUserIntent(USER_INTENT(test_user_intent_2));
  EXPECT_FALSE(comp->IsAnyUserIntentPending());
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(unmatched_intent)));
}

TEST(UserIntentMap, CloudIntent)
{
  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  const Robot& robot = testBehaviorFramework.GetRobot();
  std::unique_ptr<UserIntentComponent> comp;
  CreateComponent(testMapConfig, comp, robot);

  EXPECT_FALSE(comp->IsAnyUserIntentPending());
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));

  comp->SetCloudIntentPendingFromExpandedJSON( GetSimpleCloudJson( "cloud_intent_1" ) );
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));

  comp->DropUserIntent(USER_INTENT(test_user_intent_1));
  EXPECT_FALSE(comp->IsAnyUserIntentPending());
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));

  comp->SetCloudIntentPendingFromExpandedJSON( GetSimpleCloudJson( "cloud_intent_2" ) );
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));

  comp->SetCloudIntentPendingFromExpandedJSON( GetSimpleCloudJson( "cloud_intent_1" ) );
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));

  comp->SetCloudIntentPendingFromExpandedJSON( GetSimpleCloudJson( "asdf" ) );
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));

  comp->DropUserIntent(USER_INTENT(test_user_intent_1));
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));

  comp->DropUserIntent(USER_INTENT(unmatched_intent));
  EXPECT_FALSE(comp->IsAnyUserIntentPending());
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));
}
  
TEST(UserIntentMap, AppIntent)
{
  TestBehaviorFramework testBehaviorFramework;
  
  testBehaviorFramework.InitializeStandardBehaviorComponent(nullptr, nullptr, true);
  IncrementBaseStationTimerTicks();
  
  Robot& robot = testBehaviorFramework.GetRobot();
  
  auto& uic = testBehaviorFramework.GetBehaviorComponent().GetComponent<UserIntentComponent>();
  
  EXPECT_FALSE( uic.IsAnyUserIntentPending() );
  
  // tick the behavior to make sure ticking has no effect, unless we broadcast
  {
    AICompMap emptyMap;
    testBehaviorFramework.GetBehaviorComponent().UpdateDependent(emptyMap);
  }
  
  EXPECT_FALSE( uic.IsAnyUserIntentPending() );
  
  IncrementBaseStationTimerTicks();
  
  // broadcast an app intent
  const char* name = "Cozmo";
  
  ExternalInterface::AppIntent appIntent;
  appIntent.intent = "intent_meet_victor";
  appIntent.param = name;
  ASSERT_TRUE( robot.HasExternalInterface() );
  robot.GetExternalInterface()->Broadcast(ExternalInterface::MessageGameToEngine(std::move(appIntent)));
  
  // Tick the behavior component to send the message to the user intent component
  {
    AICompMap emptyMap;
    testBehaviorFramework.GetBehaviorComponent().UpdateDependent(emptyMap);
  }
  
  EXPECT_TRUE( uic.IsAnyUserIntentPending() );
  EXPECT_TRUE( uic.IsUserIntentPending( USER_INTENT(meet_victor) ) );
  UserIntent intent;
  EXPECT_TRUE( uic.IsUserIntentPending( USER_INTENT(meet_victor), intent ) );
  EXPECT_EQ( intent.GetTag(), USER_INTENT(meet_victor) );
  EXPECT_EQ( intent.Get_meet_victor().username, name );
  uic.DropUserIntent( USER_INTENT(meet_victor) );
  EXPECT_FALSE( uic.IsAnyUserIntentPending() );
}

TEST(UserIntentMap, IntentExpiration)
{
  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  const Robot& robot = testBehaviorFramework.GetRobot();
  std::unique_ptr<UserIntentComponent> comp;
  CreateComponent(testMapConfig, comp, robot);

  BaseStationTimer::getInstance()->UpdateTime(0);
  BCCompMap emptyMap;
  comp->UpdateDependent(emptyMap);
  
  comp->DevSetUserIntentPending(USER_INTENT(test_user_intent_1), UserIntentSource::Voice);
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));

  const bool errGGotSet = CallWithoutError( [&]() {
    for( float t=0.1f; t<1.0f; t+=0.1f ) {
      BaseStationTimer::getInstance()->UpdateTime(t);
      comp->UpdateDependent(emptyMap);
    }
  });
  EXPECT_TRUE( errGGotSet );

  EXPECT_FALSE(comp->IsAnyUserIntentPending());
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(unmatched_intent)));
}

TEST(UserIntentMap, JsonIntent)
{
  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  const Robot& robot = testBehaviorFramework.GetRobot();
  std::unique_ptr<UserIntentComponent> comp;
  CreateComponent(testMapConfig, comp, robot);

  EXPECT_FALSE(comp->SetCloudIntentPendingFromExpandedJSON(""));
  EXPECT_FALSE(comp->IsAnyUserIntentPending());

  EXPECT_FALSE(comp->SetCloudIntentPendingFromExpandedJSON(R"json(
  {
     'invalid", "format: no way }}}} this cant be valid
  )json"));
  EXPECT_FALSE(comp->IsAnyUserIntentPending());

  EXPECT_FALSE(comp->SetCloudIntentPendingFromExpandedJSON(R"json(
  {
     "wrong_key": "cloud_intent_1"
  })json"));
  EXPECT_FALSE(comp->IsAnyUserIntentPending());

  EXPECT_TRUE(comp->SetCloudIntentPendingFromExpandedJSON(R"json(
  {
     "intent": "cloud_intent_1"
  })json"));
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));

  // this one is not expecting any data "data_tag"
  EXPECT_TRUE(comp->SetCloudIntentPendingFromExpandedJSON(R"json(
  {
     "intent": "cloud_intent_2",
     "params": {
       "data_tag": 3.1415
     }
  })json"));
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));
}

TEST(UserIntentMap, ExtraData)
{
  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  const Robot& robot = testBehaviorFramework.GetRobot();
  std::unique_ptr<UserIntentComponent> comp;
  CreateComponent(testMapConfig, comp, robot);

  UserIntent data;
  
  EXPECT_TRUE(comp->SetCloudIntentPendingFromExpandedJSON(R"json(
  {
     "intent": "cloud_time_intent",
     "params": {
       "time_s": 42
     }
  })json"));
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  Reset(data);
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(set_timer), data));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(set_timer)));
  EXPECT_EQ(data.GetTag(), UserIntentTag::set_timer);
  EXPECT_EQ(data.Get_set_timer().time_s, 42);

  comp->ActivateUserIntent(USER_INTENT(set_timer), "test", false);
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(set_timer), data));
  EXPECT_TRUE(comp->IsUserIntentActive(USER_INTENT(set_timer)));

  {
    UserIntentPtr activeIntent = comp->GetUserIntentIfActive(USER_INTENT(set_timer));
    ASSERT_TRUE(activeIntent != nullptr);
    EXPECT_EQ(activeIntent->intent.GetTag(), UserIntentTag::set_timer);
    EXPECT_EQ(activeIntent->intent.Get_set_timer().time_s, 42);
    EXPECT_EQ(activeIntent->source, UserIntentSource::Voice);
  }

  comp->DeactivateUserIntent(USER_INTENT(set_timer));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(set_timer), data));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(set_timer)));


  EXPECT_TRUE(comp->SetCloudIntentPendingFromExpandedJSON(R"json(
  {
     "intent": "cloud_time_intent",
     "params": {
       "time_s": 9001
     }
  })json"));
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  Reset(data);
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(set_timer), data));
  EXPECT_EQ(data.GetTag(), UserIntentTag::set_timer);
  EXPECT_EQ(data.Get_set_timer().time_s, 9001);

  comp->ActivateUserIntent(USER_INTENT(set_timer), "test", false);
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(set_timer)));
  EXPECT_TRUE(comp->IsUserIntentActive(USER_INTENT(set_timer)));

  {
    UserIntentPtr activeIntent = comp->GetUserIntentIfActive(USER_INTENT(set_timer));
    ASSERT_TRUE(activeIntent != nullptr);
    EXPECT_EQ(activeIntent->intent.GetTag(), UserIntentTag::set_timer);
    EXPECT_EQ(activeIntent->intent.Get_set_timer().time_s, 9001);
    EXPECT_EQ(activeIntent->source, UserIntentSource::Voice);
  }

  comp->DeactivateUserIntent(USER_INTENT(set_timer));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(set_timer), data));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(set_timer)));

  
  EXPECT_TRUE(comp->SetCloudIntentPendingFromExpandedJSON(R"json(
  {
     "intent": "cloud_name_intent",
     "params": {
       "name": "Victor"
     }
  })json"));
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  Reset(data);
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(set_timer), data));
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(test_name), data));
  EXPECT_EQ(data.GetTag(), UserIntentTag::test_name);
  EXPECT_EQ(data.Get_test_name().name, "Victor");
              
  comp->ActivateUserIntent(USER_INTENT(test_name), "test", false);
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_name)));
  EXPECT_TRUE(comp->IsUserIntentActive(USER_INTENT(test_name)));

  {
    UserIntentPtr activeIntent = comp->GetUserIntentIfActive(USER_INTENT(test_name));
    ASSERT_TRUE(activeIntent != nullptr);
    EXPECT_EQ(activeIntent->intent.GetTag(), UserIntentTag::test_name);
    EXPECT_EQ(activeIntent->intent.Get_test_name().name, "Victor");
    EXPECT_EQ(activeIntent->source, UserIntentSource::Voice);
  }

  comp->DeactivateUserIntent(USER_INTENT(test_name));
  EXPECT_FALSE(comp->IsUserIntentPending(USER_INTENT(test_name), data));
  EXPECT_FALSE(comp->IsUserIntentActive(USER_INTENT(test_name)));

  
  // extra data with params that aren't camelCase or snake_case, and passing an int as a string
  EXPECT_TRUE(comp->SetCloudIntentPendingFromExpandedJSON(R"json(
  {
    "intent": "cloud_time_intent_substitution",
    "params": {
      "timer_duration.time": "60",
      "timer_duration.units": "s"
    }
  })json"));
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  Reset(data);
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(test_timeWithUnits), data));
  EXPECT_EQ(data.GetTag(), UserIntentTag::test_timeWithUnits);
  EXPECT_EQ(data.Get_test_timeWithUnits().time, 60);
  EXPECT_EQ(data.Get_test_timeWithUnits().units, UserIntent_Test_Time_Units::s);

  // TODO:(bn) add test of app intents
}

TEST(UserIntentMap, CloudMessageTest)
{
  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  const Robot& robot = testBehaviorFramework.GetRobot();
  std::unique_ptr<UserIntentComponent> comp;
  BCCompMap emptyMap;
  CreateComponent(testMapConfig, comp, robot);
  
  EXPECT_FALSE(comp->SetCloudIntentPendingFromString("{}"));
  comp->UpdateDependent(emptyMap);
  EXPECT_FALSE(comp->IsAnyUserIntentPending());

  EXPECT_FALSE(comp->SetCloudIntentPendingFromString(R"json({
    "type": "result"
  })json"));
  comp->UpdateDependent(emptyMap);
  EXPECT_FALSE(comp->IsAnyUserIntentPending());

  EXPECT_FALSE(comp->SetCloudIntentPendingFromString(R"json({
    "type": "type_that_doesnt_exist"
  })json"));
  comp->UpdateDependent(emptyMap);
  EXPECT_FALSE(comp->IsAnyUserIntentPending());


  EXPECT_TRUE(comp->SetCloudIntentPendingFromString(R"json({
    "intent": "cloud_intent_2"
  })json"));
  comp->UpdateDependent(emptyMap);
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_2)));

  EXPECT_TRUE(comp->SetCloudIntentPendingFromString(R"json({
    "type": "result",
    "intent": "cloud_intent_1"
  })json"));
  comp->UpdateDependent(emptyMap);
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(test_user_intent_1)));

  UserIntent data;
  EXPECT_TRUE(comp->SetCloudIntentPendingFromString(R"json(
  {
    "intent": "cloud_time_intent_substitution",
    "parameters": "{\"timer_duration.time\": \"60\",\"timer_duration.units\": \"s\"}\n"
  })json"));
  comp->UpdateDependent(emptyMap);
  EXPECT_TRUE(comp->IsAnyUserIntentPending());
  Reset(data);
  EXPECT_TRUE(comp->IsUserIntentPending(USER_INTENT(test_timeWithUnits), data));
  EXPECT_EQ(data.GetTag(), UserIntentTag::test_timeWithUnits);
  EXPECT_EQ(data.Get_test_timeWithUnits().time, 60);
  EXPECT_EQ(data.Get_test_timeWithUnits().units, UserIntent_Test_Time_Units::s);

}
