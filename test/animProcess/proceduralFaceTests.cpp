#include "gtest/gtest.h"

#include <string>
#include <vector>
#include <functional>

#include "coretech/common/shared/array2d_impl.h"
#include "coretech/common/shared/types.h"
#include "coretech/common/shared/array2d_impl.h"
#include "cannedAnimLib/proceduralFace/proceduralFaceDrawer.h"
#include "util/random/randomGenerator.h"
#include "util/fileUtils/fileUtils.h"

#define GENERATE_TEST_FILES 0
#define DISPLAY_FAILURES 0
#define SAVE_FAILURE_DIFFS 0

using namespace Anki;
using namespace Anki::Vector;

extern std::string resourcePath;

// Sweep all parameters and make sure we don't trigger an assert or crash when
// we try to actually draw the face. (Clipping should prevent that.)
TEST(ProceduralFace, ParameterSweep)
{
  ProceduralFace procFace;
  
  const std::vector<ProceduralFace::Value> values {
    -1000.f, -100.f, 0.f, 1.f, -1.f, 100, .00001f, -.00001f, .1f, 100000.f,
    90.f, 180.f, 270.f, 360.f, 45.f, -180.f, -360.f
  };
  
  // NOTE: This only checks same scale in both directions
  const std::vector<ProceduralFace::Value> faceScales = {
    -100.f, -1.f, 0.f, 0.5f, 1.f, 1.5f, 100.f
  };
  
  const std::vector<ProceduralFace::Value> faceAngles = {
    -1000.f, -360.f, -180.f -90.f, -45.f -10.f, -1.f, -0.001f, 0.f,
    .001f, 1.f, 10.f, 45.f, 90.f, 180.f, 360.f, 1000.f
  };
  
  using Param = ProceduralEyeParameter;
  
  // We know the following is gonna issue a zillion warnings. Let's not have them
  // all display.
  ProceduralFace::EnableClippingWarning(false);

  Vision::ImageRGB565 _procFaceImg(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);

  // Note: whichever test runs first generates the noise textures that are retained for
  //       the duration of the test. For determinism hard-code the same seed for all tests.
  Util::RandomGenerator rng(1);

  for(auto faceScale : faceScales) {
    procFace.SetFaceScale({faceScale, faceScale});

    for(auto faceAngle : faceAngles) {
      procFace.SetFaceAngle(faceAngle);
      
      for(size_t iParam = 0; iParam < (size_t)Param::NumParameters; ++iParam)
      {
        for(auto value : values) {
          procFace.SetParameterBothEyes((Param)iParam, value);

          EXPECT_NO_FATAL_FAILURE(ProceduralFaceDrawer::DrawFace(procFace, rng, _procFaceImg));
        }
      }
    }
  }
  
} // TEST(ProceduralFace, ParameterSweep)

// Compare two images by getting the square-root of sum of squared error
static double ImageDifferenceFraction(const Vision::ImageRGB565& testImage, const Vision::ImageRGB565& storedImage)
{
  EXPECT_GT(testImage.GetNumRows(), 0);
  EXPECT_GT(testImage.GetNumCols(), 0);
  EXPECT_EQ(testImage.GetNumRows(), storedImage.GetNumRows());
  EXPECT_EQ(testImage.GetNumCols(), storedImage.GetNumCols());
  
  // Get the max difference across channels and count the number of pixels
  // that change by "too much"
  s32 numTooDifferent = 0;
  const u8 kPixDiffThresh = 4; // "too much" change, in brightness levels
  const Vision::PixelRGB565* testPixel_   = testImage.GetRow(0);
  const Vision::PixelRGB565* storedPixel_ = storedImage.GetRow(0);
  for(s32 i=0; i < testImage.GetNumElements(); ++i)
  {
    const Vision::PixelRGB& testPixel   = testPixel_[i].ToPixelRGB();
    const Vision::PixelRGB& storedPixel = storedPixel_[i].ToPixelRGB();
    
#define ABSDIFF_U8(__p1__, __p2__) static_cast<u8>(std::abs((s32)__p1__ - (s32)__p2__))

    const Vision::PixelRGB absPixDiff(ABSDIFF_U8(testPixel.r(), storedPixel.r()),
                                      ABSDIFF_U8(testPixel.g(), storedPixel.g()),
                                      ABSDIFF_U8(testPixel.b(), storedPixel.b()));
    
#undef ABSDIFF_U8
    
    const u8 maxDiff = absPixDiff.max();
    if(maxDiff > kPixDiffThresh)
    {
      ++numTooDifferent;
    }
  }
  
  const double tooDifferentFraction = (double)numTooDifferent / (double)testImage.GetNumElements();
  return tooDifferentFraction;
}

static void testFaceAgainstStoredVersion(const ProceduralFace& procFace, const std::string& filename)
{
  // Generate procedural face

  Vision::ImageRGB565 procFaceImg;
  procFaceImg.Allocate(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);

  // Note: whichever test runs first generates the noise textures that are retained for
  //       the duration of the test. For determinism hard-code the same seed for all tests.
  Util::RandomGenerator rng(1);

  ProceduralFaceDrawer::DrawFace(procFace, rng, procFaceImg);
#if GENERATE_TEST_FILES
  const size_t slash = filename.find_last_of("/");
  const std::string generatedFilename = std::string("/")+Util::FileUtils::FullFilePath({"tmp", filename.substr(slash+1)});
  procFaceImg.Save(generatedFilename);

  // Load just generated procedural face

  Vision::ImageRGB565 savedFaceImg;
  savedFaceImg.Load(generatedFilename);
#else
  // Load previously generated procedural face

  Vision::ImageRGB565 savedFaceImg;
  savedFaceImg.Load(filename);
#endif


  // Compare, threshold is 0.01f based on inspecton
  // Note: see comment on random number generation and determinism
  const double kDiffFractionThreshold = 0.01;
  const double diffFrac = ImageDifferenceFraction(procFaceImg, savedFaceImg);
  EXPECT_LT(diffFrac, kDiffFractionThreshold);
  
  if((DISPLAY_FAILURES || SAVE_FAILURE_DIFFS) && (diffFrac >= kDiffFractionThreshold))
  {
    // Do the diff in RGB (not RGB565)
    Vision::ImageRGB procFaceImgRGB, savedFaceImgRGB, diffImageRGB;
    procFaceImgRGB.SetFromRGB565(procFaceImg);
    savedFaceImgRGB.SetFromRGB565(savedFaceImg);
    cv::absdiff(procFaceImgRGB.get_CvMat_(), savedFaceImgRGB.get_CvMat_(), diffImageRGB.get_CvMat_());
    
    if(DISPLAY_FAILURES)
    {
      procFaceImg.Display("Generated");
      savedFaceImg.Display("GroundTruth");
      diffImageRGB.Display("Diff", 0);
    }
    
    if(SAVE_FAILURE_DIFFS)
    {
      const size_t slash = filename.find_last_of("/");
      const std::string diffFilename = std::string("/")+Util::FileUtils::FullFilePath({"tmp", "diff", filename.substr(slash+1)});
      diffImageRGB.Save(diffFilename);
    }
  }
}

// Render known set of expressions and compare bitmap output to known good versions
TEST(ProceduralFace, RenderKeyframeCheck)
{
  const std::vector<std::string> files = {
    "anim_eyes_angry",
    "anim_eyes_awe",
    "anim_eyes_blink_line",
    "anim_eyes_look_happy",
    "anim_eyes_look_right",
    "anim_eyes_neutral"
  };

  for(auto const& key : files) {
    std::string fullpath = Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", key+".json"});
    SCOPED_TRACE(fullpath);

    Json::Reader reader;
    Json::Value data;
    bool success = reader.parse(Util::FileUtils::ReadFile(fullpath), data);
    ASSERT_TRUE(success);

    ProceduralFace face;
    face.SetFromJson(data[key][0]);

    testFaceAgainstStoredVersion(face, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", key+".png"}));
  }

} // TEST(ProceduralFace, RenderKeyframeCheck)


// Render a sub-set of parames with a fixed expression and compare bitmap output to known good versions
TEST(ProceduralFace, RenderParamsCheck)
{
  // Note: hard-coded values extracted by hand from .json files, original sources in resources

  // anim_eyes_neutral.json
  ProceduralFace anim_eyes_neutral;
  anim_eyes_neutral.SetFromValues(
                                  { 9.169665777907909, 0.0, 1.2143329245079946, 0.9052803986393223, 0.0, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
                                  { -10.206374337270427, 0.0, 1.2220369812777003, 0.9052803986393223, 0.0, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
                                  0.0f, // faceAngle_deg
                                  0.0f, 0.0f, // faceCenterX, faceCenterY
                                  1.0f, 1.0f, // faceScaleX, faceScaleY
                                  1.0f // scanlineOpacity
                                  );

  ProceduralFace procFace;

  procFace = anim_eyes_neutral;
  procFace.SetFaceScale({.5f, .25f});
  testFaceAgainstStoredVersion(procFace, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", "anim_eyes_neutral_scale.png"}));

  procFace = anim_eyes_neutral;
  procFace.SetFaceAngle(33.f);
  testFaceAgainstStoredVersion(procFace, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", "anim_eyes_neutral_angle.png"}));

  procFace = anim_eyes_neutral;
  procFace.SetScanlineOpacity(0.75f);
  testFaceAgainstStoredVersion(procFace, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", "anim_eyes_neutral_scanline.png"}));

  procFace = anim_eyes_neutral;
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::EyeCenterX, -FACE_DISPLAY_WIDTH/8);
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::EyeCenterY, -FACE_DISPLAY_HEIGHT/8);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::EyeCenterX, FACE_DISPLAY_WIDTH/8);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::EyeCenterY, FACE_DISPLAY_HEIGHT/8);
  testFaceAgainstStoredVersion(procFace, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", "anim_eyes_neutral_eyecenter.png"}));

  procFace = anim_eyes_neutral;
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::EyeScaleX, 0.75f);
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::EyeScaleY, 0.5f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::EyeScaleX, 0.5f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::EyeScaleY, 0.75f);
  testFaceAgainstStoredVersion(procFace, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", "anim_eyes_neutral_eyescale.png"}));

  procFace = anim_eyes_neutral;
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::EyeAngle, -45.f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::EyeAngle, 15.f);
  testFaceAgainstStoredVersion(procFace, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", "anim_eyes_neutral_eyeangle.png"}));

  procFace = anim_eyes_neutral;
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::LowerInnerRadiusX, 0.f);
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::LowerInnerRadiusY, 0.f);
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::UpperInnerRadiusX, 0.f);
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::UpperInnerRadiusY, 0.f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::UpperOuterRadiusX, 0.f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::UpperOuterRadiusY, 0.f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::LowerOuterRadiusX, 0.f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::LowerOuterRadiusY, 0.f);
  testFaceAgainstStoredVersion(procFace, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", "anim_eyes_neutral_radius.png"}));

  procFace = anim_eyes_neutral;
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::UpperLidY, 0.5f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::LowerLidY, 0.75f);
  testFaceAgainstStoredVersion(procFace, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", "anim_eyes_neutral_lidy.png"}));

  procFace = anim_eyes_neutral;
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::UpperLidAngle, -45.0f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::LowerLidAngle, 15.0f);
  testFaceAgainstStoredVersion(procFace, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", "anim_eyes_neutral_lidangle.png"}));

  procFace = anim_eyes_neutral;
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::UpperLidBend, 0.5f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::LowerLidBend, 0.75f);
  testFaceAgainstStoredVersion(procFace, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", "anim_eyes_neutral_lidbend.png"}));

  procFace = anim_eyes_neutral;
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::Saturation, -1.f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::Saturation, 1.f);
  testFaceAgainstStoredVersion(procFace, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", "anim_eyes_neutral_saturation.png"}));

  procFace = anim_eyes_neutral;
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::Lightness, 0.5f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::Lightness, 0.75f);
  testFaceAgainstStoredVersion(procFace, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", "anim_eyes_neutral_lightness.png"}));

  procFace = anim_eyes_neutral;
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::GlowSize, 0.5f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::GlowSize, 0.75f);
  testFaceAgainstStoredVersion(procFace, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", "anim_eyes_neutral_glowsize.png"}));

  procFace = anim_eyes_neutral;
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::HotSpotCenterX, -0.5f);
  procFace.SetParameter(ProceduralFace::Left, ProceduralFace::Parameter::HotSpotCenterY, -0.5f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::HotSpotCenterX, 0.5f);
  procFace.SetParameter(ProceduralFace::Right, ProceduralFace::Parameter::HotSpotCenterY, 0.5f);
  testFaceAgainstStoredVersion(procFace, Util::FileUtils::FullFilePath({resourcePath, "test", "animProcessTests", "anim_eyes_neutral_hotspot.png"}));

} // TEST(ProceduralFace, RenderParamsCheck)
