#include "util/helpers/boundedWhile.h"
#include "util/helpers/includeGTest.h" // Used in place of gTest/gTest.h directly to suppress warnings in the header
#include "util/fileUtils/fileUtils.h"
#include "util/helpers/quoteMacro.h"

#include "coretech/common/shared/math/radians.h"
#include "coretech/common/shared/types.h"
#include "coretech/common/engine/math/pose.h"

#include "coretech/common/engine/math/quad_impl.h"

#include "coretech/vision/engine/camera.h"
#include "coretech/vision/engine/compressedImage.h"
#include "coretech/vision/engine/image.h"
#include "coretech/vision/engine/observableObject.h"
#include "coretech/vision/engine/perspectivePoseEstimation.h"
#include "coretech/vision/engine/profiler.h"

using namespace Anki;

template<typename PRECISION>
void SolveQuarticTestHelper(std::array<PRECISION,4>& roots_computed,
                            const PRECISION tolerance)
{
  const std::array<PRECISION,5> factors = {
    {-3593989.0, -33048.973667, 316991.744900, 33048.734165, -235.623396}
  };
  
  const std::array<PRECISION,4> roots_groundTruth = {
    {0.334683441970975, 0.006699578943935, -0.136720934135068, -0.213857711381642}
  };
  
  EXPECT_TRUE(Vision::P3P::solveQuartic(factors, roots_computed) == RESULT_OK);
  for(s32 i=0; i<4; ++i) {
    EXPECT_NEAR(roots_groundTruth[i], roots_computed[i], tolerance);
  }
}

GTEST_TEST(PoseEstimation, SolveQuartic)
{
  { // Single precision
    std::array<float_t,4> roots_computed;
    SolveQuarticTestHelper(roots_computed, 1e-6f);
  }
  
  { // Double precision
    std::array<double_t,4> roots_computed;
    SolveQuarticTestHelper(roots_computed, 1e-12);
  }
  
} // GTEST_TEST(PoseEstimation, SolveQuartic)

GTEST_TEST(PoseEstimation, FromQuads)
{
  // Parameters
  const Radians Xangle = DEG_TO_RAD(-10);
  const Radians Yangle = DEG_TO_RAD( 4);
  const Radians Zangle = DEG_TO_RAD( 3);

  const Point3f translation(10.f, 15.f, 100.f);
  
  const f32 markerSize = 26.f;
  
  const f32 focalLength_x = 317.2f;
  const f32 focalLength_y = 318.4f;
  const f32 camCenter_x   = 151.9f;
  const f32 camCenter_y   = 129.0f;
  const u16 camNumRows    = 240;
  const u16 camNumCols    = 320;
  
  const Quad2f projNoise(Point2f(0.1740f,    0.0116f),
                         Point2f(0.0041f,    0.0073f),
                         Point2f(0.0381f,    0.1436f),
                         Point2f(0.2249f,    0.0851f));
  /*
  const Quad2f projNoise(Point2f(-0.0310f,    0.1679f),
                         Point2f( 0.3724f,   -0.3019f),
                         Point2f( 0.3523f,    0.1793f),
                         Point2f( 0.3543f,    0.4076f));
  */
  
  const f32 distThreshold      = 3.f;
  const Radians angleThreshold = DEG_TO_RAD(2);
  const f32 pixelErrThreshold  = 1.f;
  
  // Set up the true pose
  const Pose3d origin;
  Pose3d poseTrue;
  poseTrue.SetParent(origin);
  poseTrue.RotateBy(RotationVector3d(Xangle, X_AXIS_3D()));
  poseTrue.RotateBy(RotationVector3d(Yangle, Y_AXIS_3D()));
  poseTrue.RotateBy(RotationVector3d(Zangle, Z_AXIS_3D()));
  poseTrue.SetTranslation(translation);
  
  // Create the 3D marker and put it in the specified pose relative to the camera
  Quad3f marker3d(Point3f(-1.f, -1.f, 0.f),
                  Point3f(-1.f,  1.f, 0.f),
                  Point3f( 1.f, -1.f, 0.f),
                  Point3f( 1.f,  1.f, 0.f));
  
  marker3d *= markerSize/2.f;
  
  Quad3f marker3d_atPose;
  poseTrue.ApplyTo(marker3d, marker3d_atPose);
  
  // Compute the ground truth projection of the marker in the image
  auto calib = std::make_shared<Vision::CameraCalibration>(camNumRows,    camNumCols,
                                                           focalLength_x, focalLength_y,
                                                           camCenter_x,   camCenter_y,
                                                           0.f);           // skew

  Pose3d cameraPose;
  cameraPose.SetParent(origin);
  Vision::Camera camera;
  camera.SetPose(cameraPose);
  camera.SetCalibration(calib);
  
  Quad2f proj;
  camera.Project3dPoints(marker3d_atPose, proj);
  
  // Add noise
  proj += projNoise;
  
  // Make sure all the corners projected within the image
  for(Quad::CornerName i_corner=Quad::FirstCorner; i_corner<Quad::NumCorners; ++i_corner)
  {
    ASSERT_TRUE(not std::isnan(proj[i_corner].x()));
    ASSERT_TRUE(not std::isnan(proj[i_corner].y()));
    ASSERT_GE(proj[i_corner].x(), 0.f);
    ASSERT_LT(proj[i_corner].x(), camNumCols);
    ASSERT_GE(proj[i_corner].y(), 0.f);
    ASSERT_LT(proj[i_corner].y(), camNumRows);
  }

  // Compute the pose of the marker w.r.t. camera from the noisy projection
  Pose3d poseEst;
  Result result = camera.ComputeObjectPose(proj, marker3d, poseEst);
  EXPECT_EQ(result, RESULT_OK);
  
  // Check if the estimated pose matches the true pose
  Vec3f Tdiff;
  Radians angleDiff;
  EXPECT_TRUE(poseEst.IsSameAs(poseTrue, distThreshold, angleThreshold, Tdiff, angleDiff));

  printf("Angular difference is %f degrees (threshold = %f degrees)\n",
         angleDiff.getDegrees(),
         angleThreshold.getDegrees());
  
  printf("Translation difference is %f, threshold = %f\n",
         Tdiff.Length(),  distThreshold);
  
  
  // Check if the reprojected points match the originals
  Quad2f reproj;
  Quad3f marker3d_est;
  poseEst.ApplyTo(marker3d, marker3d_est);
  camera.Project3dPoints(marker3d_est, reproj);
  for(Quad::CornerName i_corner=Quad::FirstCorner; i_corner<Quad::NumCorners; ++i_corner) {
    EXPECT_NEAR(reproj[i_corner].x(), proj[i_corner].x(), pixelErrThreshold);
    EXPECT_NEAR(reproj[i_corner].y(), proj[i_corner].y(), pixelErrThreshold);
  }
  
} // GTEST_TEST(PoseEstimation, FromQuads)


GTEST_TEST(Camera, VisibilityAndOcclusion)
{
  // Create a camera looking at several objects, check to see that expected
  // objects are visible / occluded
  
  Pose3d poseOrigin;
  
  const Pose3d camPose(0.f, Z_AXIS_3D(), {0.f, 0.f, 0.f}, poseOrigin);
  
  const auto calib = std::make_shared<Vision::CameraCalibration>(240, 320,
                                                                 300.f, 300.f,
                                                                 160.f, 120.f);
  
  Vision::Camera camera(57);
  
  EXPECT_TRUE(camera.GetID() == 57);
  
  camera.SetPose(camPose);
  camera.SetCalibration(calib);
  
  // Note that object pose is in camera coordinates
  const Pose3d obj1Pose(M_PI_2, X_AXIS_3D(), {0.f, 0.f, 100.f}, poseOrigin);
  Vision::KnownMarker object1(0, obj1Pose, 15.f);

  camera.AddOccluder(object1);
  
  // For readability below:
  const bool RequireObjectBehind      = true;
  const bool DoNotRequireObjectBehind = false;
  
  // Without any occluders, we expect this object, positioned right in front
  // of the camera, to be visible from the camera -- if we don't require
  // there to be an object behind it to say it is visible.
  EXPECT_TRUE( object1.IsVisibleFrom(camera, DEG_TO_RAD(5), 5.f, DoNotRequireObjectBehind) );
  
  // Same check, but now we require there to be an object behind the object
  // to consider it visible.  There are no other objects, so we can't have seen
  // anything behind this object, so we want this to return false.
  EXPECT_FALSE( object1.IsVisibleFrom(camera, DEG_TO_RAD(5), 5.f, RequireObjectBehind) );
  
  // Add another object behind first, again in camera coordinates
  const Pose3d obj2Pose(M_PI_2, X_AXIS_3D(), {0.f, 0.f, 150.f}, poseOrigin);
  Vision::KnownMarker object2(0, obj2Pose, 20.f);
  
  camera.AddOccluder(object2);
  
  // Now, object2 is behind object 1...
  // ... we expect object2 not to be visible, irrespective of the
  // "requireObjectBehind" flag, because it is occlued by object1.
  EXPECT_FALSE( object2.IsVisibleFrom(camera, DEG_TO_RAD(5), 5.f, RequireObjectBehind) );
  EXPECT_FALSE( object2.IsVisibleFrom(camera, DEG_TO_RAD(5), 5.f, DoNotRequireObjectBehind) );
  
  // ... and we now _do_ expect to see object1 when requireObjectBehind is true,
  // unlike above.
  EXPECT_TRUE( object1.IsVisibleFrom(camera, DEG_TO_RAD(5), 5.f, RequireObjectBehind) );
  
  // If we clear the occluders, object2 should also be visible, now that it is
  // not occluded, but only if we do not require there to be an object behind
  // it
  camera.ClearOccluders();
  EXPECT_TRUE( object2.IsVisibleFrom(camera, DEG_TO_RAD(5), 5.f, DoNotRequireObjectBehind) );
  EXPECT_FALSE( object2.IsVisibleFrom(camera, DEG_TO_RAD(5), 5.f, RequireObjectBehind) );
  
  // Move object1 around and make sure it is NOT visible when...
  // ...it is not facing the camera
  object1.SetPose(Pose3d(M_PI_4, X_AXIS_3D(), {0.f, 0.f, 100.f}, poseOrigin));
  EXPECT_FALSE( object1.IsVisibleFrom(camera, DEG_TO_RAD(5), 5.f, DoNotRequireObjectBehind) );
  object1.SetPose(Pose3d(0, X_AXIS_3D(), {0.f, 0.f, 100.f}, poseOrigin));
  EXPECT_FALSE( object1.IsVisibleFrom(camera, DEG_TO_RAD(5), 5.f, DoNotRequireObjectBehind) );
  
  // ...it is too far away (and thus too small in the image)
  object1.SetPose(Pose3d(3*M_PI_2, X_AXIS_3D(), {0.f, 0.f, 1000.f}, poseOrigin));
  EXPECT_FALSE( object1.IsVisibleFrom(camera, DEG_TO_RAD(5), 5.f, DoNotRequireObjectBehind) );
  
  // ...it is not within the field of view
  object1.SetPose(Pose3d(M_PI_2, X_AXIS_3D(), {100.f, 0.f, 100.f}, poseOrigin));
  EXPECT_FALSE( object1.IsVisibleFrom(camera, DEG_TO_RAD(5), 5.f, DoNotRequireObjectBehind) );
  
  // ...it is behind the camera
  object1.SetPose(Pose3d(M_PI_2, X_AXIS_3D(), {100.f, 0.f, -100.f}, poseOrigin));
  EXPECT_FALSE( object1.IsVisibleFrom(camera, DEG_TO_RAD(5), 5.f, DoNotRequireObjectBehind) );

} // GTEST_TEST(Camera, VisibilityAndOcclusion)

GTEST_TEST(ColorPixels, Wrapping)
{
  using namespace Anki::Vision;
  PixelRGB p1(255, 100, 0);
  const PixelRGB p2(255, 255, 1);
  
  p1 -= p2;
  
  EXPECT_EQ(0, p1.r());
  EXPECT_EQ(0, p1.g());
  EXPECT_EQ(0, p1.b());
}

GTEST_TEST(ColorPixels, Accessors)
{
  using namespace Anki::Vision;
  
  PixelRGB_<u8> p(23,11,45);
  
  ASSERT_EQ(23, p.r());
  ASSERT_EQ(11, p.g());
  ASSERT_EQ(45, p.b());
  
  ASSERT_EQ(11, p.min());
  ASSERT_EQ(45, p.max());
  
  ASSERT_TRUE(p.IsDarkerThan(50, false));
  ASSERT_TRUE(p.IsDarkerThan(50, true));
  ASSERT_TRUE(p.IsBrighterThan(5, false));
  ASSERT_TRUE(p.IsBrighterThan(5, true));
  
  ASSERT_FALSE(p.IsDarkerThan(15, false));
  ASSERT_TRUE(p.IsDarkerThan(15, true));
  ASSERT_FALSE(p.IsBrighterThan(40, false));
  ASSERT_TRUE(p.IsBrighterThan(40, true));
  
  p.r() = 223;
  p.g() = 211;
  p.b() = 245;
  
  ASSERT_EQ(223, p[0]);
  ASSERT_EQ(211, p[1]);
  ASSERT_EQ(245, p[2]);
  
  
  PixelRGBA q(23,11,45, 128);
  
  ASSERT_EQ(23, q.r());
  ASSERT_EQ(11, q.g());
  ASSERT_EQ(45, q.b());
  ASSERT_EQ(128, q.a());
  
  ASSERT_TRUE(q.IsDarkerThan(50, false));
  ASSERT_TRUE(q.IsDarkerThan(50, true));
  ASSERT_TRUE(q.IsBrighterThan(5, false));
  ASSERT_TRUE(q.IsBrighterThan(5, true));
  
  ASSERT_FALSE(q.IsDarkerThan(15, false));
  ASSERT_TRUE(q.IsDarkerThan(15, true));
  ASSERT_FALSE(q.IsBrighterThan(40, false));
  ASSERT_TRUE(q.IsBrighterThan(40, true));
  
  q.r() = 223;
  q.g() = 211;
  q.b() = 245;
  q.a() = 200;
  
  ASSERT_EQ(223, q[0]);
  ASSERT_EQ(211, q[1]);
  ASSERT_EQ(245, q[2]);
  ASSERT_EQ(200, q[3]);

}

GTEST_TEST(ColorPixels, GrayConverters)
{
  using namespace Anki::Vision;
  
  const PixelRGB_<u8> p_u8(23,11,45);
  
  ASSERT_EQ(22, p_u8.gray());
  ASSERT_EQ(18, p_u8.weightedGray());
  
  const PixelRGB_<f32> p_f32(23.f, 11.f, 45.f);
  
  ASSERT_NEAR(22.5f, p_f32.gray(), 1e-3f);
  ASSERT_NEAR(18.464f, p_f32.weightedGray(), 1e-3f);
  
  const PixelRGBA p_alpha(23,11,45, 255);
  
  ASSERT_EQ(22, p_alpha.gray());
  ASSERT_EQ(18, p_alpha.weightedGray());
  
}

GTEST_TEST(ColorPixels, RGB565Conversion)
{
  using namespace Anki::Vision;
  
  // RGB24 to RGB565
  const std::vector<u8> valuesRGB = {
    0, 3, 6, 15, 31, 64, 197, 200, 255,
  };
  
#define SCALE(value,bits) ((value>>bits)<<bits)
  
  for(auto rValue : valuesRGB)
  {
    for(auto gValue : valuesRGB)
    {
      for(auto bValue : valuesRGB)
      {
        const PixelRGB pRGB(rValue, gValue, bValue);
        
        //printf("RGB=(%d,%d,%d)\n", rValue, gValue, bValue);
                
        const PixelRGB565 pRGB565(pRGB);
        
        //printf("RGB565=(%d,%d,%d)\n", pRGB565.r(), pRGB565.g(), pRGB565.b());
        
        EXPECT_EQ(pRGB565.r(), SCALE(pRGB.r(),3));
        EXPECT_EQ(pRGB565.g(), SCALE(pRGB.g(),2));
        EXPECT_EQ(pRGB565.b(), SCALE(pRGB.b(),3));
        
      }
    }
  }
  
  // RGB565 to RGB24
  const std::vector<u8> valuesRB = {
    0, 3, 6, 15, 20, 31,
  };
  
  const std::vector<u8> valuesG = {
    0, 3, 6, 15, 31, 42, 63,
  };
  
  for(auto rValue : valuesRB)
  {
    ASSERT_LT(rValue, 1<<5);
    rValue = (rValue << 3);
    
    for(auto gValue : valuesG)
    {
      ASSERT_LT(gValue, 1<<6);
      gValue = (gValue << 2);
      
      for(auto bValue : valuesRB)
      {
        ASSERT_LT(bValue, 1<<5);
        bValue = (bValue << 3);
        
        // For unswapped version
        const PixelRGB565 pRGB565(rValue, gValue, bValue);
        PixelRGB pRGB = pRGB565.ToPixelRGB();
        
        //printf("RGB565=(%d,%d,%d) vs. (%d,%d,%d)\n", rValue, gValue, bValue,
        //       pRGB565.r(), pRGB565.g(), pRGB565.b());
        
        //printf("RGB=(%d,%d,%d)\n", pRGB.r(), pRGB.g(), pRGB.b());
        
        EXPECT_EQ(pRGB.r(), pRGB565.r());
        EXPECT_EQ(pRGB.g(), pRGB565.g());
        EXPECT_EQ(pRGB.b(), pRGB565.b());
      }
    }
  }
  
  // RGB565 to BGRA32
  for(auto rValue : valuesRGB)
  {
    for(auto gValue : valuesRGB)
    {
      for(auto bValue : valuesRGB)
      {
        for(auto aValue : valuesRGB)
        {
          const PixelRGB565 pix565(rValue, gValue, bValue);
          const u32 color = pix565.ToBGRA32(aValue);
          
          const u8 bCheck = (color >> 24);
          const u8 gCheck = ((0x00FF0000 & color) >> 16);
          const u8 rCheck = ((0x0000FF00 & color) >> 8);
          const u8 aCheck = (0x000000FF & color);
          
          EXPECT_EQ(SCALE(bValue,3), bCheck);
          EXPECT_EQ(SCALE(gValue,2), gCheck);
          EXPECT_EQ(SCALE(rValue,3), rCheck);
          EXPECT_EQ(aValue, aCheck);
        }
      }
    }
  }
  
#undef SCALE
}

GTEST_TEST(ImageRGB, NormalizedColor)
{
  using namespace Anki::Vision;
  
  ImageRGB img(3,3);
  img(0,0) = {0,0,0};
  img(0,1) = {255,255,255};
  img(0,2) = {0, 255, 255};
  img(1,0) = {0, 0, 255};
  img(1,1) = {128, 0, 0};
  img(1,2) = {17, 0, 47};
  img(2,0) = {97, 54, 250};
  img(2,1) = {10, 10, 10};
  img(2,2) = {100, 200, 0};
  
  ImageRGB imgNorm;
  img.GetNormalizedColor(imgNorm);
  
  ASSERT_EQ(img.GetNumRows(), imgNorm.GetNumRows());
  ASSERT_EQ(img.GetNumCols(), imgNorm.GetNumCols());
  
  for(s32 i=0; i<img.GetNumRows(); ++i)
  {
    const PixelRGB* img_i = img.GetRow(i);
    const PixelRGB* imgNorm_i = imgNorm.GetRow(i);
    
    for(s32 j=0; j<img.GetNumCols(); ++j)
    {
      const PixelRGB& p = img_i[j];
      const PixelRGB& pNorm = imgNorm_i[j];
      
      const s32 sum = (s32)p.r() + (s32)p.g() + (s32)p.b();
      const PixelRGB truth(sum == 0 ? 0 : Util::numeric_cast<u8>(((s32)p.r() * 255)/sum),
                           sum == 0 ? 0 : Util::numeric_cast<u8>(((s32)p.g() * 255)/sum),
                           sum == 0 ? 0 : Util::numeric_cast<u8>(((s32)p.b() * 255)/sum));
      
      //printf("Img=(%d,%d,%d) ImgNorm=(%d,%d,%d) Truth=(%d,%d,%d)\n",
      //       p.r(), p.g(), p.b(), pNorm.r(), pNorm.g(), pNorm.b(), truth.r(), truth.g(), truth.b());
      
      // Note: rounding error depending on order of operations can yield +/-1 variation
      EXPECT_NEAR(truth.r(), pNorm.r(), 1);
      EXPECT_NEAR(truth.g(), pNorm.g(), 1);
      EXPECT_NEAR(truth.b(), pNorm.b(), 1);
    }
  }
  
  // Try with an ROI
  Rectangle<s32> roi(0,0,2,2);
  ImageRGB imgROI = img.GetROI(roi);
  EXPECT_FALSE(imgROI.IsContinuous());
  imgROI.GetNormalizedColor(imgNorm);
  ASSERT_EQ(imgROI.GetNumRows(), imgNorm.GetNumRows());
  ASSERT_EQ(imgROI.GetNumCols(), imgNorm.GetNumCols());
  
  for(s32 i=0; i<imgROI.GetNumRows(); ++i)
  {
    const PixelRGB* img_i = imgROI.GetRow(i);
    const PixelRGB* imgNorm_i = imgNorm.GetRow(i);
    
    for(s32 j=0; j<imgROI.GetNumCols(); ++j)
    {
      const PixelRGB& p = img_i[j];
      const PixelRGB& pNorm = imgNorm_i[j];
      
      const s32 sum = (s32)p.r() + (s32)p.g() + (s32)p.b();
      const PixelRGB truth(sum == 0 ? 0 : Util::numeric_cast<u8>(((s32)p.r() * 255)/sum),
                           sum == 0 ? 0 : Util::numeric_cast<u8>(((s32)p.g() * 255)/sum),
                           sum == 0 ? 0 : Util::numeric_cast<u8>(((s32)p.b() * 255)/sum));
      
      //printf("Img=(%d,%d,%d) ImgNorm=(%d,%d,%d) Truth=(%d,%d,%d)\n",
      //       p.r(), p.g(), p.b(), pNorm.r(), pNorm.g(), pNorm.b(), truth.r(), truth.g(), truth.b());
      
      // Note: rounding error depending on order of operations can yield +/-1 variation
      EXPECT_NEAR(truth.r(), pNorm.r(), 1);
      EXPECT_NEAR(truth.g(), pNorm.g(), 1);
      EXPECT_NEAR(truth.b(), pNorm.b(), 1);
    }
  }
}

GTEST_TEST(ResizeImage, CorrectSizes)
{
  Vision::Image origImg(10,20);
  Vision::Image largerSize(30,25);

  const Vision::ResizeMethod kMethod = Vision::ResizeMethod::NearestNeighbor;
  
  // Regular resize should yield 30x25 image
  origImg.Resize(largerSize, kMethod);
  EXPECT_EQ(30, largerSize.GetNumRows());
  EXPECT_EQ(25, largerSize.GetNumCols());
  EXPECT_EQ(10, origImg.GetNumRows());
  EXPECT_EQ(20, origImg.GetNumCols());
  
  // Keep aspect ratio should yield 12x25 (13 = 25/20 * 10)
  origImg.ResizeKeepAspectRatio(largerSize, kMethod);
  EXPECT_EQ(13, largerSize.GetNumRows());
  EXPECT_EQ(25, largerSize.GetNumCols());
  EXPECT_EQ(10, origImg.GetNumRows());
  EXPECT_EQ(20, origImg.GetNumCols());
  
  // Resize in place with keep aspect ratio and onlySmaller=true should not change origImg
  origImg.ResizeKeepAspectRatio(30, 25, Vision::ResizeMethod::NearestNeighbor, true);
  EXPECT_EQ(10, origImg.GetNumRows());
  EXPECT_EQ(20, origImg.GetNumCols());
  
  // Resize in place with keep aspect ratio and onlySmaller=false should yield 13x25
  origImg.ResizeKeepAspectRatio(30, 25, Vision::ResizeMethod::NearestNeighbor, false);
  EXPECT_EQ(13, origImg.GetNumRows());
  EXPECT_EQ(25, origImg.GetNumCols());  
}

GTEST_TEST(CompressedImage, Compress)
{
  using namespace Vision;

  // Empty compressed image
  CompressedImage cImg;
  const std::vector<u8>& buffer = cImg.GetCompressedBuffer();

  EXPECT_TRUE(buffer.empty());
  EXPECT_EQ(cImg.GetTimestamp(), 0);
  EXPECT_EQ(cImg.GetImageId(), 0);
  EXPECT_EQ(cImg.GetNumRows(), 0);
  EXPECT_EQ(cImg.GetNumCols(), 0);
  EXPECT_EQ(cImg.GetNumChannels(), 0);

  Image gray(5, 5, (u8)0);
  gray.SetTimestamp(1);
  gray.SetImageId(2);

  // Compress a gray image
  cImg.Compress(gray);

  EXPECT_EQ(buffer.size(), 332);
  EXPECT_EQ(cImg.GetTimestamp(), 1);
  EXPECT_EQ(cImg.GetImageId(), 2);
  EXPECT_EQ(cImg.GetNumRows(), 5);
  EXPECT_EQ(cImg.GetNumCols(), 5);
  EXPECT_EQ(cImg.GetNumChannels(), 1);

  ImageRGB rgb(10, 10, {0, 0, 0});
  rgb.SetTimestamp(3);
  rgb.SetImageId(4);

  // Construct and immediately compress an image
  CompressedImage cImg2(rgb);
  const std::vector<u8>& buffer2 = cImg2.GetCompressedBuffer();
  
  EXPECT_EQ(buffer2.size(), 631);
  EXPECT_EQ(cImg2.GetTimestamp(), 3);
  EXPECT_EQ(cImg2.GetImageId(), 4);
  EXPECT_EQ(cImg2.GetNumRows(), 10);
  EXPECT_EQ(cImg2.GetNumCols(), 10);
  EXPECT_EQ(cImg2.GetNumChannels(), 3);
}

GTEST_TEST(CompressedImage, Decompress)
{
  using namespace Vision;
  
  Image rgb(5, 5, (u8)255);
  CompressedImage img(rgb, 100);

  ImageRGB outRGB;
  bool res = img.Decompress(outRGB);
  EXPECT_FALSE(res);

  Image outGray;
  res = img.Decompress(outGray);
  EXPECT_TRUE(res);
  EXPECT_EQ(rgb, outGray);
}
