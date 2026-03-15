//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"

namespace td {

enum class FileType : int32 {
  Thumbnail,
  ProfilePhoto,
  Photo,
  VoiceNote,
  Video,
  Document,
  Encrypted,
  Temp,
  Sticker,
  Audio,
  Animation,
  EncryptedThumbnail,
  Wallpaper,
  VideoNote,
  SecureDecrypted,
  SecureEncrypted,
  Background,
  DocumentAsFile,
  Ringtone,
  CallLog,
  PhotoStory,
  VideoStory,
  SelfDestructingPhoto,
  SelfDestructingVideo,
  SelfDestructingVideoNote,
  SelfDestructingVoiceNote,
  Size,
  None
};

constexpr int32 MAX_FILE_TYPE = static_cast<int32>(FileType::Size);

inline FileType get_main_file_type(FileType file_type) {
  switch (file_type) {
    case FileType::EncryptedThumbnail:
      return FileType::Thumbnail;
    case FileType::DocumentAsFile:
      return FileType::Document;
    case FileType::SelfDestructingPhoto:
      return FileType::Photo;
    case FileType::SelfDestructingVideo:
      return FileType::Video;
    case FileType::SelfDestructingVideoNote:
      return FileType::VideoNote;
    case FileType::SelfDestructingVoiceNote:
      return FileType::VoiceNote;
    default:
      return file_type;
  }
}

inline CSlice get_file_type_unique_name(FileType file_type) {
  switch (file_type) {
    case FileType::Thumbnail:
      return CSlice("thumbnails");
    case FileType::ProfilePhoto:
      return CSlice("profile_photos");
    case FileType::Photo:
      return CSlice("photos");
    case FileType::VoiceNote:
      return CSlice("voice");
    case FileType::Video:
      return CSlice("videos");
    case FileType::Document:
      return CSlice("documents");
    case FileType::Encrypted:
      return CSlice("secret");
    case FileType::Temp:
      return CSlice("temp");
    case FileType::Sticker:
      return CSlice("stickers");
    case FileType::Audio:
      return CSlice("music");
    case FileType::Animation:
      return CSlice("animations");
    case FileType::EncryptedThumbnail:
      return CSlice("secret_thumbnails");
    case FileType::Wallpaper:
      return CSlice("wallpapers");
    case FileType::VideoNote:
      return CSlice("video_notes");
    case FileType::SecureDecrypted:
      return CSlice("passport");
    case FileType::SecureEncrypted:
      return CSlice("passport_encrypted");
    case FileType::Background:
      return CSlice("backgrounds");
    case FileType::DocumentAsFile:
      return CSlice("documents_as_file");
    case FileType::Ringtone:
      return CSlice("ringtones");
    case FileType::CallLog:
      return CSlice("call_logs");
    case FileType::PhotoStory:
      return CSlice("stories_photo");
    case FileType::VideoStory:
      return CSlice("stories_video");
    case FileType::SelfDestructingPhoto:
      return CSlice("self_destructing_photos");
    case FileType::SelfDestructingVideo:
      return CSlice("self_destructing_videos");
    case FileType::SelfDestructingVideoNote:
      return CSlice("self_destructing_video_notes");
    case FileType::SelfDestructingVoiceNote:
      return CSlice("self_destructing_voice");
    default:
      return CSlice("unknown");
  }
}

inline StringBuilder &operator<<(StringBuilder &string_builder, FileType file_type) {
  return string_builder << get_file_type_unique_name(file_type);
}

inline td_api::object_ptr<td_api::FileType> get_file_type_object(FileType file_type) {
  switch (file_type) {
    case FileType::Animation:
      return td_api::make_object<td_api::fileTypeAnimation>();
    case FileType::Audio:
      return td_api::make_object<td_api::fileTypeAudio>();
    case FileType::Document:
    case FileType::DocumentAsFile:
      return td_api::make_object<td_api::fileTypeDocument>();
    case FileType::Photo:
    case FileType::ProfilePhoto:
    case FileType::SelfDestructingPhoto:
      return td_api::make_object<td_api::fileTypePhoto>();
    case FileType::Sticker:
      return td_api::make_object<td_api::fileTypeSticker>();
    case FileType::Video:
    case FileType::SelfDestructingVideo:
      return td_api::make_object<td_api::fileTypeVideo>();
    case FileType::VideoNote:
    case FileType::SelfDestructingVideoNote:
      return td_api::make_object<td_api::fileTypeVideoNote>();
    case FileType::VoiceNote:
    case FileType::SelfDestructingVoiceNote:
      return td_api::make_object<td_api::fileTypeVoiceNote>();
    case FileType::Ringtone:
      return td_api::make_object<td_api::fileTypeNotificationSound>();
    default:
      return td_api::make_object<td_api::fileTypeNone>();
  }
}

}  // namespace td
