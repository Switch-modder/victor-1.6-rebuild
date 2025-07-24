/**
 * File: visionComponent.h
 *
 * Author: Andrew Stein
 * Date:   11/20/2014
 *
 * Description: Container for the thread containing the basestation vision
 *              system, which provides methods for managing and communicating
 *              with it.
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef ANKI_VECTOR_BASESTATION_VISION_PROC_THREAD_H
#define ANKI_VECTOR_BASESTATION_VISION_PROC_THREAD_H

#include "coretech/vision/engine/cameraCalibration.h"
#include "coretech/common/engine/robotTimeStamp.h"
#include "coretech/vision/engine/visionMarker.h"
#include "coretech/vision/engine/faceTracker.h"
#include "util/entityComponent/entity.h"
#include "engine/components/visionScheduleMediator/iVisionModeSubscriber.h"
#include "engine/engineTimeStamp.h"
#include "engine/vision/visionSystemInput.h"

#include "clad/types/cameraParams.h"
#include "clad/types/imageTypes.h"
#include "clad/types/loadedKnownFace.h"
#include "clad/types/salientPointTypes.h"
#include "clad/types/visionModes.h"

#include "util/console/consoleInterface.h"
#include "util/helpers/noncopyable.h"
#include "util/signals/simpleSignal.hpp"

#include <thread>
#include <mutex>
#include <list>
#include <vector>

namespace Anki {

// Forward declaration
namespace Util {
namespace Data {
  class DataPlatform;
}
}
  
namespace Vision {
  class CompressedImage;
  class TrackedFace;
}
  
namespace Vector {

// Forward declaration
class Robot;
class CozmoContext;
struct ImageSaverParams;
struct VisionProcessingResult;
class VisionSystem;
class VizManager;
  
namespace ExternalInterface {
struct RobotCompletedFactoryDotTest;
}
  
struct DockingErrorSignal;

  class VisionComponent : public IDependencyManagedComponent<RobotComponentID>,
                          public Util::noncopyable,
                          public IVisionModeSubscriber
  {
  public:

    typedef void(VisionResultCallback)(const VisionProcessingResult& result);
  
    VisionComponent();
    virtual ~VisionComponent();

    //////
    // IDependencyManagedComponent functions
    //////
    virtual void InitDependent(Vector::Robot* robot, const RobotCompMap& dependentComps) override;
    virtual void GetInitDependencies(RobotCompIDSet& dependencies) const override {
      dependencies.insert(RobotComponentID::CozmoContextWrapper);
    };
    virtual void AdditionalInitAccessibleComponents(RobotCompIDSet& components) const override {
      components.insert(RobotComponentID::CozmoContextWrapper);
    };

    virtual void GetUpdateDependencies(RobotCompIDSet& dependencies) const override {
      dependencies.insert(RobotComponentID::Movement);
      // Applies the scheduling consequences of the last frame's subscriptions before ticking VisionComponent
      dependencies.insert(RobotComponentID::VisionScheduleMediator);
    };
    virtual void UpdateDependent(const RobotCompMap& dependentComps) override;
    //////
    // end IDependencyManagedComponent functions
    //////

    // SetNextImage does nothing until enabled
    void Enable(bool enable) { _enabled = enable; }
    
    // Set whether vision system runs synchronously or on its own thread
    void SetIsSynchronous(bool isSync);

    // Calibration must be provided before Update() will run
    void SetCameraCalibration(std::shared_ptr<Vision::CameraCalibration> camCalib);
    
    // Provide image for processing, with corresponding robot state.
    // In synchronous mode, the image is processed immediately in this thread.
    // In asynchronous mode, it will be processed as soon as VisionSystem thread is ticked.
    // Will Release and Invalidate buffer should this fail
    Result SetNextImage(Vision::ImageBuffer& buffer);

    // Pause/Unpause processing of images
    // Note: Will still capture images from CameraService but they will be discarded
    void Pause(bool isPaused);

    // Whether or not to capture images from CameraService
    void EnableImageCapture(bool enable);

    // Returns true as long as we are/will be processing images
    bool IsProcessingImages();
    
    // Set whether or not markers queued while robot is "moving" (meaning it is
    // turning too fast or head is moving too fast) will be considered
    void   EnableVisionWhileRotatingFast(bool enable);
    bool   IsVisionWhileRotatingFastEnabled() const;

    // Set the camera's capture format
    // Non blocking but will cause VisionComponent/System to
    // wait until no one is using the shared camera memory before
    // requesting the format change and will continue to
    // wait until we once again receive frames from the camera
    bool   SetCameraCaptureFormat(Vision::ImageEncoding format);
    
    // Returns true while camera format is in the process of changing
    bool   IsWaitingForCaptureFormatChange() const;
    
    // Looks through all results available from the VisionSystem and processes them.
    // This updates the Robot's BlockWorld and FaceWorld using those results.
    Result UpdateAllResults();
    
    // Individual processing update helpers. These are called individually by
    // UpdateAllResults() above, but are exposed as public for Unit Test usage.
    Result UpdateFaces(const VisionProcessingResult& result);
    Result UpdatePets(const VisionProcessingResult& procResult);
    Result UpdateVisionMarkers(const VisionProcessingResult& result);
    Result UpdateMotionCentroid(const VisionProcessingResult& result);
    Result UpdateLaserPoints(const VisionProcessingResult& result);
    Result UpdateOverheadEdges(const VisionProcessingResult& result);
    Result UpdateComputedCalibration(const VisionProcessingResult& result);
    Result UpdateCameraParams(const VisionProcessingResult& procResult);
    Result UpdateVisualObstacles(const VisionProcessingResult& procResult);
    Result UpdateSalientPoints(const VisionProcessingResult& result);
    Result UpdatePhotoManager(const VisionProcessingResult& procResult);
    Result UpdateDetectedIllumination(const VisionProcessingResult& procResult);
    Result UpdateMirrorMode(const VisionProcessingResult& procResult);

    const Vision::Camera& GetCamera(void) const;
    Vision::Camera& GetCamera(void);
    
    const std::shared_ptr<Vision::CameraCalibration> GetCameraCalibration() const;
    bool IsCameraCalibrationSet() const { return _camera->IsCalibrated(); }
    Result ClearCalibrationImages();

    RobotTimeStamp_t GetLastProcessedImageTimeStamp() const;
    
    TimeStamp_t GetProcessingPeriod_ms() const;
    TimeStamp_t GetFramePeriod_ms() const;
    
    Result SendCompressedImage(const Vision::CompressedImage& img, const std::string& identifier);
    
    // Detected markers will only be queued for BlockWorld processing if the robot
    // was turning by less than these amounts when they were observed.
    // Use values < 0 to set to defaults
    void SetMarkerDetectionTurnSpeedThresholds(f32 bodyTurnSpeedThresh_degPerSec,
                                               f32 headTurnSpeedThresh_degPerSec);

    // Get the current thresholds in case you want to be able to restore what they
    // were before you changed them
    void GetMarkerDetectionTurnSpeedThresholds(f32& bodyTurnSpeedThresh_degPerSec,
                                               f32& headTurnSpeedThresh_degPerSec) const;
    
    // Add an occluder to the camera for the cross-bar of the lift in its position
    // at the requested time
    void AddLiftOccluder(const RobotTimeStamp_t t_request);
    
    // Camera calibration
    void StoreNextImageForCameraCalibration(const Rectangle<s32>& targetROI);
    void StoreNextImageForCameraCalibration(); // Target ROI = entire image
    
    bool WillStoreNextImageForCameraCalibration() const { return _storeNextImageForCalibration;  }
    size_t  GetNumStoredCameraCalibrationImages() const;
    
    // Get jpeg compressed data of calibration images
    // dotsFoundMask has bits set to 1 wherever the corresponding calibration image was found to contain a valid target
    std::list<std::vector<u8> > GetCalibrationImageJpegData(u8* dotsFoundMask = nullptr) const;
    
    // Get the specified calibration pose to the robot. 'whichPose' must be [0,numCalibrationimages].
    Result GetCalibrationPoseToRobot(size_t whichPose, Pose3d& p) const;

    // Call to compute calibration from previously stored images
    void EnableComputingCameraCalibration(bool enable) { EnableMode(VisionMode::Calibration, enable); }
    
    // For factory test behavior use only: tell vision component to find the
    // four dots for the test target and compute camera pose. Result is
    // broadcast via an EngineToGame::RobotCompletedFactoryDotTest message.
    void UseNextImageForFactoryDotTest() { _doFactoryDotTest = true; }
    
    // Called automatically when doFactoryDotTest=true. Exposed publicly for unit test
    Result FindFactoryTestDotCentroids(const Vision::Image& image,
                                       ExternalInterface::RobotCompletedFactoryDotTest& msg);
    
    // Used by FindFactoryTestDotCentroids iff camera is already calibrated.
    // Otherwise call manually by populating obsQuad with the dot centroids.
    Result ComputeCameraPoseVsIdeal(const Quad2f& obsQuad, Pose3d& pose) const;

    // Return true if there is still room for a *new* named face.
    // No need to check this if *merging* with an existing named face.
    bool CanAddNamedFace();
    
    // Links a name with a face ID and sets up the robot's ability to say that name.
    // Then merges the newly-named ID into mergeWithID, if set to a valid existing ID (which should already be named!)
    void AssignNameToFace(Vision::FaceID_t faceID, const std::string& name,
                          Vision::FaceID_t mergeWithID = Vision::UnknownFaceID);
    
    // Enable face enrollment mode and optionally specify the ID for which 
    // enrollment is allowed (use UnknownFaceID to indicate "any" ID).
    // Enrollment will automatically disable after numEnrollments. (Use 
    // a value < 0 to enable ongoing enrollments.)
    void SetFaceEnrollmentMode(Vision::FaceID_t forFaceID = Vision::UnknownFaceID,
                               s32 numEnrollments = -1,
                               bool forceNewID = false);

#if ANKI_DEV_CHEATS
    void SaveAllRecognitionImages(const std::string& imagePathPrefix);
    void DeleteAllRecognitionImages();
#endif

    // Erase faces
    Result EraseFace(Vision::FaceID_t faceID);
    void   EraseAllFaces();
    
    // Get a list of names and their IDs
    void RequestEnrolledNames();
    
    // Will assign a new name to a given face ID. The old name is provided as a
    // safety measure: the rename will only happen if the given old name matches
    // the one currently assigned to faceID. Otherwise, failure is returned.
    // On success, a RobotLoadedKnownFace message with the new ID/name pairing is broadcast.
    Result RenameFace(Vision::FaceID_t faceID, const std::string& oldName, const std::string& newName);
    
    // Returns true if the provided name has been enrolled
    bool IsNameTaken(const std::string& name);
    
    // Returns the set of IDs with the given name.
    // Generally there will only be one entry, but its possible we may support multiple enrollments
    // with the same name.
    std::set<Vision::FaceID_t> GetFaceIDsWithName(const std::string& name);
    
    // Load/Save face album data to/from file.
    Result SaveFaceAlbumToFile(const std::string& path);
    Result LoadFaceAlbumFromFile(const std::string& path); // Broadcasts any loaded names and IDs
    Result LoadFaceAlbumFromFile(const std::string& path, std::list<Vision::LoadedKnownFace>& loadedFaces); // Populates list, does not broadcast
    Result SaveFaceAlbum(); // use album path specified in vision_config.json
    Result LoadFaceAlbum(); // use album path specified in vision_config.json, broadcast loaded names and IDs
    
    // NOTE: if params.path is empty, it will default to <cachePath>/camera/images (where cachePath comes from DataPlatform)
    void SetSaveImageParameters(const ImageSaverParams& params);

    // This is for faking images being processed for unit tests
    void FakeImageProcessed(RobotTimeStamp_t t);
    
    // Templated message handler used internally by AnkiEventUtil
    template<typename T>
    void HandleMessage(const T& msg);
    
    void EnableAutoExposure(bool enable);
    void EnableWhiteBalance(bool enable);
    void SetAndDisableCameraControl(const Vision::CameraParams& params);
    
    s32 GetMinCameraExposureTime_ms() const;
    s32 GetMaxCameraExposureTime_ms() const;
    f32 GetMinCameraGain() const;
    f32 GetMaxCameraGain() const;
    
    Vision::CameraParams GetCurrentCameraParams() const;
    
    // Captures image data from HAL, if available, and puts it in buffer
    // Returns true if image was captured, false if not
    bool CaptureImage(Vision::ImageBuffer& buffer);

    // Releases captured image back to CameraService
    bool ReleaseImage(Vision::ImageBuffer& buffer);
    
    bool LookupGroundPlaneHomography(f32 atHeadAngle, Matrix_3x3f& H) const;

    bool HasStartedCapturingImages() const { return _hasStartedCapturingImages; }

    // These methods control which faces are tracked, and turn face
    // recognition on/off. The goal here is to avoid resetting face
    // detection when in an action that tracks a face
    // (e.g. TrackFaceAction). Note these methods are not safe
    // to call from more than one place and only work now because
    // they are only called from track face action. If there are
    // going to be other callers of these methods we should rework
    // how this is exposed.
    void AddAllowedTrackedFace(const Vision::FaceID_t faceID);
    void ClearAllowedTrackedFaces();
    
    // Text to display at the top of the screen when MirrorMode is enabled.
    // Whomever calls this last "wins".
    void SetMirrorModeDisplayString(const std::string& str, const ColorRGBA& color);

    // Enables the capture of a single image
    // will leave image capture disabled once the image is captured
    // Normal image capture can be reenabled with EnableImageCapture(true)
    void CaptureOneFrame();

    void EnableImageSending(bool enable);
    void EnableMirrorMode(bool enable);

    void EnableSendingSDKImageChunks(bool enable) { _sendProtoImageChunks = enable; }
    bool IsSendingSDKImageChunks() { return _sendProtoImageChunks; }

    Vision::ImageQuality GetLastImageQuality() const { return _lastImageQuality; }
    IlluminationState GetLastIlluminationState() const { return _lastIlluminationState; }

  protected:
    
    // Non-rotated points representing the lift cross bar
    std::vector<Point3f> _liftCrossBarSource;
    
    bool _isInitialized = false;
    bool _hasStartedCapturingImages = false;
    
    Robot* _robot = nullptr;
    const CozmoContext* _context = nullptr;
    
    VisionSystem* _visionSystem = nullptr;
    VizManager*   _vizManager = nullptr;
    std::map<std::string, s32> _vizDisplayIndexMap;
    std::list<std::pair<EngineTimeStamp_t, Vision::SalientPoint>> _salientPointsToDraw;
    std::string _mirrorModeDisplayString;
    ColorRGBA _mirrorModeStringColor;
    
    // Robot stores the calibration, camera just gets a reference to it
    // This is so we can share the same calibration data across multiple
    // cameras (e.g. those stored inside the pose history)
    std::unique_ptr<Vision::Camera> _camera;
    bool _enabled = false;
    
    bool _isSynchronous = false;
    
    // variables which are updated in the main (engine) thread but checked on the worker thread
    // must be volatile to avoid potential caching issues
    volatile bool _running = false;
    volatile bool _paused  = false;

    std::mutex _lock;
    
    // Input for VisionSystem
    // While _visionSystemInput.locked is true this is being processed by VisionSystem
    // and can not be modified by VisionComponent
    VisionSystemInput _visionSystemInput = {};

    // This mutex/condition_variable pair is used for the main thread to signal the
    // Processor thread that an image is ready to be processed (or we are shutting down)
    std::mutex              _imageReadyMutex;
    std::condition_variable _imageReadyCondition;

    bool _storeNextImageForCalibration = false;
    Rectangle<s32> _calibTargetROI;
    
    constexpr static f32 kDefaultBodySpeedThresh = DEG_TO_RAD(60);
    constexpr static f32 kDefaultHeadSpeedThresh = DEG_TO_RAD(10);
    f32 _markerDetectionBodyTurnSpeedThreshold_radPerSec = kDefaultBodySpeedThresh;
    f32 _markerDetectionHeadTurnSpeedThreshold_radPerSec = kDefaultHeadSpeedThresh;
    
    RobotTimeStamp_t _lastReceivedImageTimeStamp_ms = 0;
    RobotTimeStamp_t _lastProcessedImageTimeStamp_ms = 0;
    TimeStamp_t _processingPeriod_ms = 0;  // How fast we are processing frames
    TimeStamp_t _framePeriod_ms = 0;       // How fast we are receiving frames
    
    Vision::ImageQuality _lastImageQuality = Vision::ImageQuality::Good;
    Vision::ImageQuality _lastBroadcastImageQuality = Vision::ImageQuality::Unchecked;
    RobotTimeStamp_t  _currentQualityBeginTime_ms = 0;
    RobotTimeStamp_t  _waitForNextAlert_ms = 0;
    
    bool _visionWhileRotatingFastEnabled = false;

    Vision::ImageEncoding _desiredImageFormat = Vision::ImageEncoding::NoneImageEncoding;

    // State machine to make sure nothing is using the shared memory from the camera system
    // before we request a different camera capture format as well as to wait
    // until we get a frame from the camera after changing formats before unpausing
    // vision component
    enum class CaptureFormatState
    {
      None,
      WaitingForProcessingToStop,
      WaitingForFrame
    };
    CaptureFormatState _captureFormatState = CaptureFormatState::None;

    EngineTimeStamp_t _lastImageCaptureTime_ms = 0;
    
    std::string _faceAlbumName;
    
    std::thread _processingThread;
    
    std::vector<Signal::SmartHandle> _signalHandles;
    
    struct Homography {
      Matrix_3x3f H;
      bool        isGroundPlaneROIVisible;
    };
    std::map<f32,Homography> _groundPlaneHomographyLUT; // keyed on head angle in radians

    // Factory centroid finder: returns the centroids of the 4 factory test dots,
    // computes camera pose w.r.t. the target and broadcasts a RobotCompletedFactoryDotTest
    // message. This runs on the main thread and should only be used for factory tests.
    // Is run automatically when _doFactoryDotTest=true and sets it back to false when done.
    bool _doFactoryDotTest = false;
    
    // Threading for OpenCV
    int _openCvNumThreads = 1;

    bool _enableImageCapture = true;

    bool _captureOneImage = false;

    bool _sendProtoImageChunks = false;

    // Time at which we attempted to restart the camera as we had not received a valid image for
    // some amount of time
    EngineTimeStamp_t _restartingCameraTime_ms = 0;

    IlluminationState _lastIlluminationState = IlluminationState::Unknown;

    #if REMOTE_CONSOLE_ENABLED
    // Array of pairs of ConsoleVars and their associated values used for toggling VisionModes
    using VisionModeConsoleVarPair = std::pair<Util::ConsoleVar<bool>*, bool>;
    std::array<VisionModeConsoleVarPair, static_cast<u32>(VisionMode::Count)> _visionModeConsoleVars;
    #endif
    
    void ReadVisionConfig(const Json::Value& config);
    void PopulateGroundPlaneHomographyLUT(f32 angleResolution_rad = DEG_TO_RAD(0.25f));
    
    void Processor();

    void UpdateVisionSystem(const VisionSystemInput& input);
    
    void Lock();
    void Unlock();
    
    // Used for asynchronous run mode
    void Start(); // SetCameraCalibration() must have been called already
    void Stop();
    
    // Helper for loading face album data from file / robot
    void BroadcastLoadedNamesAndIDs(const std::list<Vision::LoadedKnownFace>& loadedFaces) const;
    
    void VisualizeObservedMarkerIn3D(const Vision::ObservedMarker& marker) const;

    // Updates the state of requesting for a camera capture format change
    void UpdateCaptureFormatChange(s32 gotNumRows=0);

    // Do whatever we need to do for camera calbration
    // such as saving images or running dot test
    void UpdateForCalibration();

    // Send images from result's displayImg and debug image lists to Viz/SDK
    void SendImages(VisionProcessingResult& result);

    void SetLiftCrossBar();

    Vision::ImageEncoding GetCurrentImageFormat() const;

    // Dynamically creates console vars for all the vision modes
    void SetupVisionModeConsoleVars();

    // Watches vision mode console vars and does things when they change
    void UpdateVisionModeConsoleVars();

    // Enable/disable different types of processing
    Result EnableMode(VisionMode mode, bool enable);

    // Tracks how long we have/haven't been receiving images for
    // and takes appropriate action to deal with a lack of images
    void UpdateImageReceivedChecks(bool gotImage);

  }; // class VisionComponent
  
  inline void VisionComponent::Pause(bool isPaused) {
    if(_paused == isPaused)
    {
      return;
    }

    PRINT_CH_INFO("VisionComponent", "VisionComponent.Pause.Set", "Setting Paused from %d to %d",
                  _paused, isPaused);
    _paused = isPaused;
  }
  
  inline const Vision::Camera& VisionComponent::GetCamera(void) const {
    return *_camera;
  }
  
  inline Vision::Camera& VisionComponent::GetCamera(void) {
    return *_camera;
  }
  
  inline const std::shared_ptr<Vision::CameraCalibration> VisionComponent::GetCameraCalibration() const {
    return _camera->GetCalibration();
  }
  
  inline void VisionComponent::EnableVisionWhileRotatingFast(bool enable) {
    _visionWhileRotatingFastEnabled = enable;
    EnableMode(VisionMode::Markers_FastRotation, _visionWhileRotatingFastEnabled);
  }
  
  inline bool VisionComponent::IsVisionWhileRotatingFastEnabled() const {
    return _visionWhileRotatingFastEnabled;
  }
  
  inline void VisionComponent::SetMarkerDetectionTurnSpeedThresholds(f32 bodyTurnSpeedThresh_degPerSec,
                                                                     f32 headTurnSpeedThresh_degPerSec)
  {
    if(bodyTurnSpeedThresh_degPerSec < 0) {
      _markerDetectionBodyTurnSpeedThreshold_radPerSec = kDefaultBodySpeedThresh;
    } else {
      _markerDetectionBodyTurnSpeedThreshold_radPerSec = DEG_TO_RAD(bodyTurnSpeedThresh_degPerSec);
    }
    
    if(headTurnSpeedThresh_degPerSec < 0) {
      _markerDetectionHeadTurnSpeedThreshold_radPerSec = kDefaultHeadSpeedThresh;
    } else {
      _markerDetectionHeadTurnSpeedThreshold_radPerSec = DEG_TO_RAD(headTurnSpeedThresh_degPerSec);
    }
  }

  inline void VisionComponent::GetMarkerDetectionTurnSpeedThresholds(f32& bodyTurnSpeedThresh_degPerSec,
                                                                     f32& headTurnSpeedThresh_degPerSec) const
  {
    bodyTurnSpeedThresh_degPerSec = RAD_TO_DEG(_markerDetectionBodyTurnSpeedThreshold_radPerSec);
    headTurnSpeedThresh_degPerSec = RAD_TO_DEG(_markerDetectionHeadTurnSpeedThreshold_radPerSec);
  }
  
  inline void VisionComponent::StoreNextImageForCameraCalibration() {
    StoreNextImageForCameraCalibration(Rectangle<s32>(-1,-1,0,0));
  }
  
  inline void VisionComponent::StoreNextImageForCameraCalibration(const Rectangle<s32>& targetROI) {
    _storeNextImageForCalibration = true;
    _calibTargetROI = targetROI;
  }
  
  inline TimeStamp_t VisionComponent::GetFramePeriod_ms() const
  {
    return _framePeriod_ms;
  }
  
  inline TimeStamp_t VisionComponent::GetProcessingPeriod_ms() const
  {
    return _processingPeriod_ms;
  }
  
  inline void VisionComponent::SetMirrorModeDisplayString(const std::string& str, const ColorRGBA& color)
  {
    _mirrorModeDisplayString = str;
    _mirrorModeStringColor = color;
  }

} // namespace Vector
} // namespace Anki

#endif // ANKI_VECTOR_BASESTATION_VISION_PROC_THREAD_H
