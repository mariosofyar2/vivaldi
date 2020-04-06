// Copyright (c) 2015 Vivaldi Technologies AS. All rights reserved
#include "testing/disable_unittests_macros.h"

// List of unittest disabled for Linux by Vivaldi
// On the form
//    DISABLE(foo,bar)
//    DISABLE(foo,baz)

  // Broke in v53
  DISABLE(ConditionalCacheDeletionHelperBrowserTest, TimeAndURL)

  DISABLE(ExtensionFetchTest, ExtensionCanFetchExtensionResource)

  DISABLE(SitePerProcessBrowserTest, RFPHDestruction)

  DISABLE(RenderFrameHostManagerTest, SwapProcessWithRelNoopenerAndTargetBlank)

  // Proprietary media codec tests
  DISABLE(AudioFileReaderTest, AAC)
  DISABLE(AudioFileReaderTest, CorruptMP3)
  DISABLE(AudioFileReaderTest, MP3)
  DISABLE_MULTI(AudioDecoderTest, Decode)
  DISABLE_MULTI(AudioDecoderTest, Initialize)
  DISABLE_MULTI(AudioDecoderTest, NoTimestamp)
  DISABLE_MULTI(AudioDecoderTest, ProduceAudioSamples)
  DISABLE_MULTI(AudioDecoderTest, Reset)
  DISABLE_MULTI(EncryptedMediaTest, Playback_AudioOnly_MP4)
  DISABLE_MULTI(EncryptedMediaTest, Playback_ClearVideo_WEBM_EncryptedAudio_MP4)
  DISABLE_MULTI(EncryptedMediaTest, Playback_EncryptedVideo_MP4_ClearAudio_WEBM)
  DISABLE_MULTI(EncryptedMediaTest, Playback_EncryptedVideo_WEBM_EncryptedAudio_MP4)
  DISABLE_MULTI(EncryptedMediaTest, Playback_VideoOnly_MP4)
  DISABLE(FFmpegDemuxerTest, HEVC_in_MP4_container)
  DISABLE(FFmpegDemuxerTest, IsValidAnnexB)
  DISABLE(FFmpegDemuxerTest, Mp3WithVideoStreamID3TagData)
  DISABLE(FFmpegDemuxerTest, NaturalSizeWithPASP)
  DISABLE(FFmpegDemuxerTest, NaturalSizeWithoutPASP)
  DISABLE(FFmpegDemuxerTest, Read_AC3_Audio)
  DISABLE(FFmpegDemuxerTest, Read_EAC3_Audio)
  DISABLE(FFmpegDemuxerTest, Read_Mp4_Media_Track_Info)
  DISABLE(FFmpegDemuxerTest, Rotate_Metadata_0)
  DISABLE(FFmpegDemuxerTest, Rotate_Metadata_180)
  DISABLE(FFmpegDemuxerTest, Rotate_Metadata_270)
  DISABLE(FFmpegDemuxerTest, Rotate_Metadata_90)
  DISABLE(MediaFileCheckerTest, MP3)
  DISABLE_MULTI(Mp3SeekFFmpegDemuxerTest, TestFastSeek)
  DISABLE(AudioFileReaderTest, MidStreamConfigChangesFail)
  DISABLE(FFmpegDemuxerTest, MP4_ZeroStszEntry)
  DISABLE(FFmpegDemuxerTest, NoID3TagData)
  DISABLE(EncryptedMediaSupportedTypesClearKeyTest, Audio_MP4)
  DISABLE(EncryptedMediaSupportedTypesClearKeyTest, Video_MP4)
  DISABLE(EncryptedMediaSupportedTypesExternalClearKeyTest, Audio_MP4)
  DISABLE(EncryptedMediaSupportedTypesExternalClearKeyTest, Video_MP4)
  DISABLE_ALL(MediaCanPlayTypeTest)

  // Assume these fails due to switches::kExtensionActionRedesign being disabled
  DISABLE(ToolbarActionViewInteractiveUITest, TestClickingOnOverflowedAction)

  // VB-22258
  DISABLE(ComponentFlashHintFileTest, CorruptionTest)
  DISABLE(ComponentFlashHintFileTest, ExistsTest)
  DISABLE(ComponentFlashHintFileTest, InstallTest)
