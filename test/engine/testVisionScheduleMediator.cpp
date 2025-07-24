/**
 * File: testVisionScheduleMediator.cpp
 * 
 * Author: Sam Russell
 * Created: 2018-02-21
 * 
 * Description: Unit tests for the VisionScheduleMediator
 * 
 * Copyright: Anki, Inc. 2018
 * 
 * --gtest_filter=VisionScheduleMediator
 * 
 **/

#include "gtest/gtest.h"

// access VSM internals for test
#define private public
#define protected public

#include "engine/components/visionComponent.h"
#include "engine/components/visionScheduleMediator/visionScheduleMediator.h"
#include "engine/components/visionScheduleMediator/iVisionModeSubscriber.h"
#include "engine/vision/visionModeSchedule.h"
#include "engine/cozmoContext.h"
#include "engine/robot.h"
#include "engine/robotComponents_fwd.h"
#include "util/entityComponent/dependencyManagedEntity.h"

using namespace Anki;
using namespace Anki::Vector;

extern CozmoContext* cozmoContext;

namespace {

void CreateVSMConfig(const std::string& jsonString, Json::Value& config) 
{
  Json::Reader reader;
  const bool parsedOK = reader.parse(jsonString, config, false);
  ASSERT_TRUE(parsedOK);
}

class TestSubscriber : IVisionModeSubscriber
{
public:
  explicit TestSubscriber(VisionScheduleMediator* vsm, std::set<VisionModeRequest> visionModes)
  : _vsm(vsm)
  , _visionModes(visionModes)
  {
  }

  void Subscribe()
  {
    _vsm->SetVisionModeSubscriptions(this, _visionModes);
  }

  void Unsubscribe()
  {
    _vsm->ReleaseAllVisionModeSubscriptions(this);
  }

private:
  VisionScheduleMediator* _vsm;
  std::set<VisionModeRequest> _visionModes;
};

} // namespace 

TEST(VisionScheduleMediator, Interleaving)
{
  
  std::string configString = R"json(
  {
    "VisionModeSettings" :
    [
      {
        "mode"         : "Markers",
        "low"          : 4,
        "med"          : 2,
        "high"         : 1,
        "standard"     : 2,
        "relativeCost" : 16
      },
      {
        "mode"         : "Faces",
        "low"          : 4,
        "med"          : 2,
        "high"         : 1,
        "standard"     : 2,
        "relativeCost" : 16
      },
      {
        "mode"         : "Pets",
        "low"          : 8,
        "med"          : 4,
        "high"         : 2,
        "standard"     : 8,
        "relativeCost" : 10
      },
      {
        "mode"         : "Motion",
        "low"          : 4,
        "med"          : 2,
        "high"         : 1,
        "standard"     : 2,
        "relativeCost" : 4
      }
    ]
  })json";
  Json::Value config;
  CreateVSMConfig(configString, config);
  VisionComponent visionComponent;
  VisionScheduleMediator vsm;
  vsm.Init(config);
  std::set<VisionMode> enabledModes;
  
  // vision component needs access to vision system to not complain, so needs Initing
  Robot robot(0, cozmoContext);
  DependencyManagedEntity<RobotComponentID> dependencies;
  dependencies.AddDependentComponent(RobotComponentID::CozmoContextWrapper, robot.GetComponentPtr<ContextWrapper>(), false);
  visionComponent.InitDependent( &robot, dependencies);

  TestSubscriber lowMarkerSubscriber(&vsm, { { VisionMode::Markers, EVisionUpdateFrequency::Low } });
  TestSubscriber medMarkerSubscriber(&vsm, { { VisionMode::Markers, EVisionUpdateFrequency::Med } });
  TestSubscriber highMarkerSubscriber(&vsm, { { VisionMode::Markers, EVisionUpdateFrequency::High } });

  TestSubscriber lowFaceSubscriber(&vsm, { { VisionMode::Faces, EVisionUpdateFrequency::Low } });
  TestSubscriber medFaceSubscriber(&vsm, { { VisionMode::Faces, EVisionUpdateFrequency::Med } });
  TestSubscriber highFaceSubscriber(&vsm, { { VisionMode::Faces, EVisionUpdateFrequency::High } });

  TestSubscriber lowPetSubscriber(&vsm, { { VisionMode::Pets, EVisionUpdateFrequency::Low } });
  TestSubscriber medPetSubscriber(&vsm, { { VisionMode::Pets, EVisionUpdateFrequency::Med } });
  TestSubscriber highPetSubscriber(&vsm, { { VisionMode::Pets, EVisionUpdateFrequency::High } });
  
  TestSubscriber lowMotionSubscriber(&vsm, { { VisionMode::Motion, EVisionUpdateFrequency::Low } });
  TestSubscriber medMotionSubscriber(&vsm, { { VisionMode::Motion, EVisionUpdateFrequency::Med } });
  TestSubscriber highMotionSubscriber(&vsm, { { VisionMode::Motion, EVisionUpdateFrequency::High } });

  // Test basic Subscription
  lowFaceSubscriber.Subscribe();
  lowMarkerSubscriber.Subscribe();
  lowPetSubscriber.Subscribe();
  lowMotionSubscriber.Subscribe();
  vsm.UpdateVisionSchedule(visionComponent, nullptr);
  AllVisionModesSchedule::ModeScheduleList scheduleList = vsm.GenerateBalancedSchedule(visionComponent);

  for(auto& modeSchedule : scheduleList){
    switch(modeSchedule.first){
      case VisionMode::Faces:
        EXPECT_TRUE((modeSchedule.second._schedule == std::vector<bool>({false, false, true, false})));
        break;
      case VisionMode::Markers:
        EXPECT_TRUE((modeSchedule.second._schedule == std::vector<bool>({false, false, false, true})));
        break;
      case VisionMode::Pets:
        EXPECT_TRUE((modeSchedule.second._schedule == std::vector<bool>({false, true, false, false,
                                                                         false, false, false, false})));
        break;
      case VisionMode::Motion:
        EXPECT_TRUE((modeSchedule.second._schedule == std::vector<bool>({true, false, false, false})));
        break;
      default:
        // If we get here... the unit test has been broken and should be fixed.
        EXPECT_TRUE(false) << "TestVisionScheduleMediator.UnhandledVisionMode";
        break;
    }
  }

  // Test Layered Subscription
  medFaceSubscriber.Subscribe();
  medMarkerSubscriber.Subscribe();
  medPetSubscriber.Subscribe();
  medMotionSubscriber.Subscribe();
  vsm.UpdateVisionSchedule(visionComponent, nullptr);
  scheduleList = vsm.GenerateBalancedSchedule(visionComponent);

  for(auto& modeSchedule : scheduleList){
    switch(modeSchedule.first){
      case VisionMode::Faces:
        EXPECT_TRUE((modeSchedule.second._schedule == std::vector<bool>({true, false})));
        break;
      case VisionMode::Markers:
        EXPECT_TRUE((modeSchedule.second._schedule == std::vector<bool>({false, true})));
        break;
      case VisionMode::Pets:
        EXPECT_TRUE((modeSchedule.second._schedule == std::vector<bool>({false, true, false, false})));
        break;
      case VisionMode::Motion:
        EXPECT_TRUE((modeSchedule.second._schedule == std::vector<bool>({true, false})));
        break;
      default:
        // If we get here... the unit test has been broken and should be fixed.
        EXPECT_TRUE(false) << "TestVisionScheduleMediator.UnhandledVisionMode";
        break;
    }
  }

  // Test basic Unsubscribe
  medFaceSubscriber.Unsubscribe();
  medMarkerSubscriber.Unsubscribe();
  medPetSubscriber.Unsubscribe();
  medMotionSubscriber.Unsubscribe();
  vsm.UpdateVisionSchedule(visionComponent, nullptr);
  scheduleList = vsm.GenerateBalancedSchedule(visionComponent);

  for(auto& modeSchedule : scheduleList){
    switch(modeSchedule.first){
      case VisionMode::Faces:
        EXPECT_TRUE((modeSchedule.second._schedule == std::vector<bool>({false, false, true, false})));
        break;
      case VisionMode::Markers:
        EXPECT_TRUE((modeSchedule.second._schedule == std::vector<bool>({false, false, false, true})));
        break;
      case VisionMode::Pets:
        EXPECT_TRUE((modeSchedule.second._schedule == std::vector<bool>({false, true, false, false,
                                                                         false, false, false, false})));
        break;
      case VisionMode::Motion:
        EXPECT_TRUE((modeSchedule.second._schedule == std::vector<bool>({true, false, false, false})));
        break;
      default:
        // If we get here... the unit test has been broken and should be fixed.
        EXPECT_TRUE(false) << "TestVisionScheduleMediator.UnhandledVisionMode";
        break;
    }
  }

  // Test full Unsubscribe
  lowFaceSubscriber.Unsubscribe();
  lowMarkerSubscriber.Unsubscribe();
  lowPetSubscriber.Unsubscribe();
  lowMotionSubscriber.Unsubscribe();
  vsm.UpdateVisionSchedule(visionComponent, nullptr);
  scheduleList = vsm.GenerateBalancedSchedule(visionComponent);
}
