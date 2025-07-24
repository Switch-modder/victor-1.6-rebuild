/**
 * File: BehaviorTakeAPhotoCoordinator.cpp
 *
 * Author: Kevin M. Karol
 * Created: 2018-06-05
 *
 * Description: Behavior which handles the behavior flow when the user wants to take a photo
 *
 * Copyright: Anki, Inc. 2018
 *
 **/


#include "engine/aiComponent/behaviorComponent/behaviors/photoTaking/behaviorTakeAPhotoCoordinator.h"

#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorExternalInterface.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/behaviorComponent/userIntentComponent.h"
#include "engine/aiComponent/behaviorComponent/userIntentData.h"
#include "engine/components/photographyManager.h"
#include "engine/faceWorld.h"
#include "engine/robot.h"

#include "util/console/consoleInterface.h"

namespace Anki {
namespace Vector {
  
namespace {
#define CONSOLE_GROUP "Behaviors.TakeAPhoto"
  
CONSOLE_VAR(f32, kHeadAngleDeg,                  CONSOLE_GROUP, 25.0);
CONSOLE_VAR(f32, kReadyToTakePhotoTimeout_sec,   CONSOLE_GROUP, 3.f);
CONSOLE_VAR(f32, kTakingPhotoTimeout_sec,        CONSOLE_GROUP, 6.f);
  
#undef CONSOLE_GROUP
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorTakeAPhotoCoordinator::InstanceConfig::InstanceConfig()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorTakeAPhotoCoordinator::DynamicVariables::DynamicVariables()
: isASelfie(false)
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorTakeAPhotoCoordinator::BehaviorTakeAPhotoCoordinator(const Json::Value& config)
 : ICozmoBehavior(config)
{

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorTakeAPhotoCoordinator::~BehaviorTakeAPhotoCoordinator()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorTakeAPhotoCoordinator::WantsToBeActivatedBehavior() const
{
  auto& uic = GetBehaviorComp<UserIntentComponent>();
  const bool isPhotoPending = uic.IsUserIntentPending(USER_INTENT(take_a_photo));
  return isPhotoPending;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorTakeAPhotoCoordinator::GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const
{
  modifiers.wantsToBeActivatedWhenOffTreads = true;
  modifiers.visionModesForActiveScope->insert({ VisionMode::SaveImages, EVisionUpdateFrequency::High });
  modifiers.visionModesForActiveScope->insert({ VisionMode::AutoExp_MinGain, EVisionUpdateFrequency::High });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorTakeAPhotoCoordinator::GetAllDelegates(std::set<IBehavior*>& delegates) const
{
  delegates.insert(_iConfig.frameFacesBehavior.get());
  delegates.insert(_iConfig.storageIsFullBehavior.get());
  delegates.insert(_iConfig.driveOffChargerBehavior.get());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorTakeAPhotoCoordinator::GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorTakeAPhotoCoordinator::InitBehavior()
{
  const auto& BC = GetBEI().GetBehaviorContainer();
  _iConfig.frameFacesBehavior          = BC.FindBehaviorByID(BEHAVIOR_ID(FrameFaces));
  _iConfig.storageIsFullBehavior       = BC.FindBehaviorByID(BEHAVIOR_ID(SingletonICantDoThat));
  _iConfig.driveOffChargerBehavior     = BC.FindBehaviorByID(BEHAVIOR_ID(DriveOffChargerFace));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorTakeAPhotoCoordinator::OnBehaviorActivated() 
{
  // reset dynamic variables
  _dVars = DynamicVariables();
  UserIntentPtr intentData = SmartActivateUserIntent(USER_INTENT(take_a_photo));
  const bool isStorageFull = GetBEI().GetPhotographyManager().IsPhotoStorageFull();

  if(isStorageFull){
    TransitionToStorageIsFull();
    static const bool kSucceeded = false;
    GetBEI().GetPhotographyManager().SendDASEvent(kSucceeded, "TooManyPhotos");
    return; // nothing to do here, especially don't go down and enable High Res photo mode
  }else if(intentData != nullptr){
    const bool robotPickedUp = GetBEI().GetRobotInfo().GetOffTreadsState() != OffTreadsState::OnTreads;
    _dVars.isASelfie = !(intentData->intent.Get_take_a_photo().empty_or_selfie.empty());
    // If we're taking a selfie we need to center the faces first - otherwise just take a photo
    if(_dVars.isASelfie && !robotPickedUp){
      TransitionToDriveOffCharger();
    }else{
      DelegateIfInControl(new MoveHeadToAngleAction(DEG_TO_RAD(kHeadAngleDeg)), [this](){
        TransitionToFocusingAnimation();
      });
    }
  }else{
    PRINT_NAMED_ERROR("BehaviorTakeAPhotoCoordinator.OnBehaviorActivated.NullIntentData",
                      "");
  }

  if(RESULT_OK != GetBEI().GetPhotographyManager().EnablePhotoMode(true))
  {
    PRINT_NAMED_ERROR("BehaviorTakeAPhotoCoordinator.OnBehaviorActivated.EnablePhotoModeFailed",
                      "");
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorTakeAPhotoCoordinator::OnBehaviorDeactivated()
{
  if(RESULT_OK != GetBEI().GetPhotographyManager().EnablePhotoMode(false))
  {
    PRINT_NAMED_ERROR("BehaviorTakeAPhotoCoordinator.OnBehaviorDectivated.DisablePhotoModeFailed",
                      "");
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorTakeAPhotoCoordinator::TransitionToStorageIsFull()
{
  ANKI_VERIFY(_iConfig.storageIsFullBehavior->WantsToBeActivated(), 
              "BehaviorTakeAPhotoCoordinator.TransitionToStorageIsFull.DoesNotWantToBeActivated", 
              "");
  DelegateIfInControl(_iConfig.storageIsFullBehavior.get());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorTakeAPhotoCoordinator::TransitionToDriveOffCharger()
{
  const bool isOnCharger = GetBEI().GetRobotInfo().IsOnChargerContacts();
  if(isOnCharger){
    ANKI_VERIFY(_iConfig.driveOffChargerBehavior->WantsToBeActivated(), 
            "BehaviorTakeAPhotoCoordinator.TransitionToDriveOffCharger.DoesNotWantToBeActivated", 
            "");
    DelegateIfInControl(_iConfig.driveOffChargerBehavior.get(), 
                        &BehaviorTakeAPhotoCoordinator::TransitionToFrameFaces);
  }else{
    TransitionToFrameFaces();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorTakeAPhotoCoordinator::TransitionToFrameFaces()
{
  ANKI_VERIFY(_iConfig.frameFacesBehavior->WantsToBeActivated(),
              "BehaviorTakeAPhotoCoordinator.TransitionToFrameFaces.DoesNotWantToBeActivated", 
              "");
  const auto imageTimestampWhenStarted = GetBEI().GetRobotInfo().GetLastImageTimeStamp();
  DelegateIfInControl(_iConfig.frameFacesBehavior.get(),
                      [this, imageTimestampWhenStarted] () {
                        const bool sawAnyFaces = GetBEI().GetFaceWorld().HasAnyFaces(imageTimestampWhenStarted);
                        if (sawAnyFaces) {
                          TransitionToFocusingAnimation();
                        } else {
                          PRINT_CH_INFO("Behaviors", "BehaviorTakeAPhotoCoordinator.TransitionToFrameFaces.NoFacesFound",
                                        "Did not see any faces - playing \"I don't know\" animation");
                          DelegateIfInControl(new TriggerAnimationAction(AnimationTrigger::ICantDoThat));
                          static const bool kSucceeded = false;
                          GetBEI().GetPhotographyManager().SendDASEvent(kSucceeded, "UnableToFindFace");
                        }
                      });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorTakeAPhotoCoordinator::TransitionToFocusingAnimation()
{
  DelegateIfInControl(new TriggerAnimationAction(AnimationTrigger::TakeAPictureFocusing),
                      &BehaviorTakeAPhotoCoordinator::TransitionToTakePhoto);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorTakeAPhotoCoordinator::TransitionToTakePhoto()
{
  if(GetBEI().GetPhotographyManager().IsReadyToTakePhoto())
  {
    // We should normally be ready immediately because the animations should have taken long enough to switch
    // camera formats
    CaptureCurrentImage();
  }
  else
  {
    // We generally should not need to wait any more than for the animations to play, so issue an error and
    // wait for the photo manager to be ready to take a photo and *then* transition
    PRINT_NAMED_ERROR("BehaviorTakeAPhotoCoordinator.TransitionToTakePhoto.NotReadyAfterAnimating", "");
    
    WaitForLambdaAction* waitAction = new WaitForLambdaAction([](Robot& robot) {
      return robot.GetPhotographyManager().IsReadyToTakePhoto();
    }, kReadyToTakePhotoTimeout_sec);
    
    auto transition = [this](ActionResult result) {
      if(ActionResult::SUCCESS == result) {
        this->CaptureCurrentImage();
      }
      else {
        PRINT_NAMED_ERROR("BehaviorTakeAPhotoCoordinator.TransitionToTakePhoto.NotReadyAfterTimeout", "");
      }
    };
    
    DelegateIfInControl(waitAction, transition);
  }
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorTakeAPhotoCoordinator::CaptureCurrentImage()
{
  auto photoHandle = GetBEI().GetPhotographyManager().TakePhoto();

  // Wait for photo to be taken before continuing
  auto waitLambda = [photoHandle](Robot& robot) {
    return robot.GetPhotographyManager().WasPhotoTaken(photoHandle);
  };
  
  CompoundActionSequential* action = new CompoundActionSequential({
    new TriggerAnimationAction(AnimationTrigger::TakeAPictureCapture),
    new WaitForLambdaAction(waitLambda, kTakingPhotoTimeout_sec),
  });
  
  DelegateIfInControl(action, [this, photoHandle](ActionResult result) {
    if(ActionResult::SUCCESS == result) {
      PRINT_CH_INFO("Behaviors", "BehaviorTakeAPhotoCoordinator.CaptureCurrentImage.PhotoWasTaken",
                    "Handle: %zu", photoHandle);
    }
    else {
      GetBEI().GetPhotographyManager().CancelTakePhoto();
      PRINT_NAMED_ERROR("BehaviorTakeAPhotoCoordinator.CaptureCurrentImage.TakePhotoTimedOut",
                        "Handle: %zu Timeout: %.2fsec",
                        photoHandle, kTakingPhotoTimeout_sec);
    }
  });
}

}
}
