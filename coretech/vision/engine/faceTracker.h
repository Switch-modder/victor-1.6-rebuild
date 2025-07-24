/**
 * File: faceTracker.h
 *
 * Author: Andrew Stein
 * Date:   8/18/2015
 *
 * Description: Wrapper for underlying face detection and tracking mechanism, 
 *              the details of which are hidden inside a private implementation.
 *
 * Copyright: Anki, Inc. 2015
 **/

#ifndef __Anki_Vision_FaceTracker_H__
#define __Anki_Vision_FaceTracker_H__

#include "coretech/common/shared/types.h"
#include "coretech/vision/engine/debugImageList.h"
#include "coretech/vision/engine/faceIdTypes.h"
#include "coretech/vision/engine/trackedFace.h"
#include "clad/types/loadedKnownFace.h"

#include <list>

// Forward declaration:
namespace Json {
  class Value;
}

namespace Anki {
namespace Vision {
  
  class Camera;
  class CompressedImage;
  class Image;
  
  class FaceTracker
  {
  public:
    
    FaceTracker(const Camera& camera,
                const std::string& modelPath,
                const Json::Value& config);
    
    ~FaceTracker();
    
    // Returns the faces found and any IDs that may have been updated (e.g. due
    // to a new recognition or a merge of existing records).
    // CropFactor is the fraction of original image width to use for detection/tracking
    Result Update(const Vision::Image&        frameOrig,
                  const float                 cropFactor,
                  std::list<TrackedFace>&     faces,
                  std::list<UpdatedFaceID>&   updatedIDs,
                  DebugImageList<CompressedImage>& debugImages);
    
    // These methods control which faces we are going to track, the rest
    // of the faces will be discarded. Also if there are any allowed
    // faces, facial recognition will be disabled. If there are no
    // allowed tracked faces all faces detected will be tracked and
    // facial recognition will occur "normally".
    void AddAllowedTrackedFace(const FaceID_t faceID);
    bool HaveAllowedTrackedFaces();
    // This method will clear all the tracked faces and reset the
    // face tracker.
    void ClearAllowedTrackedFaces();

    // If the robot moves we will call ClearAllowedTrackedFaces if 
    // we don't have any allowed tracked faces.
    void AccountForRobotMove();
    
    void EnableDisplay(bool enabled);
    
    void SetRecognitionIsSynchronous(bool isSynchronous);
    
    void SetFaceEnrollmentMode(FaceID_t forFaceID = UnknownFaceID,
                               s32 numEnrollments = -1,
                               bool forceNewID = false);
    
    void EnableEmotionDetection(bool enable);
    void EnableSmileDetection(bool enable);
    void EnableGazeDetection(bool enable);
    void EnableBlinkDetection(bool enable);
    
    // Will return false if the private implementation does not support face recognition
    static bool IsRecognitionSupported();
    
    // Enable/Disable automatic recognition/enrollment of faces that are detected (i.e. if disabled, just
    // detect and track faces)
    void EnableRecognition(bool enable);
    bool IsRecognitionEnabled() const;
    
    // returns the minimum distance between eyes a face has to have in order to be enrollable
    static float GetMinEyeDistanceForEnrollment();
    
    // returns true if there is room for another named face
    // call before trying to use AssignNameToID (and not merging into existing ID)
    bool   CanAddNamedFace() const;
    
    // if mergeWithID=UnknownFaceID, will fail if there is not enough room for a new named face
    Result AssignNameToID(FaceID_t faceID, const std::string& name, FaceID_t mergeWithID);
    
    Result EraseFace(FaceID_t faceID);
    void   EraseAllFaces();
    Result RenameFace(FaceID_t faceID, const std::string& oldName, const std::string& newName,
                      Vision::RobotRenamedEnrolledFace& renamedFace);
    
    std::vector<Vision::LoadedKnownFace> GetEnrolledNames() const;
    
    Result SaveAlbum(const std::string& albumName);
    Result LoadAlbum(const std::string& albumName, std::list<LoadedKnownFace>& loadedFaces);
    
    Result GetSerializedData(std::vector<u8>& albumData,
                             std::vector<u8>& enrollData);
    
    Result SetSerializedData(const std::vector<u8>& albumData,
                             const std::vector<u8>& enrollData,
                             std::list<LoadedKnownFace>& loadedFaces);

    void PrintTiming();

#if ANKI_DEVELOPER_CODE
    // For testing/evaluation:
    Result DevAddFaceToAlbum(const Image& img, const TrackedFace& face, int albumEntry);
    Result DevFindFaceInAlbum(const Image& img, const TrackedFace& face, int& albumEntry, float& score) const;
    Result DevFindFaceInAlbum(const Image& img, const TrackedFace& face, const int maxMatches,
                              std::vector<std::pair<int, float>>& matches) const;
    float DevComputePairwiseMatchScore(int faceID1, int faceID2) const;
    float DevComputePairwiseMatchScore(int faceID1, const Image& img2, const TrackedFace& face2) const;
#endif

#if ANKI_DEV_CHEATS
    void SaveAllRecognitionImages(const std::string& imagePathPrefix);
    void DeleteAllRecognitionImages();
#endif // ANKI_DEV_CHEATS
    
  private:
    
    // Forward declaration
    class Impl;
    
    std::unique_ptr<Impl> _pImpl;
    
  }; // class FaceTracker
  
} // namespace Vision
} // namespace Anki

#endif // __Anki_Vision_FaceTracker_H__
