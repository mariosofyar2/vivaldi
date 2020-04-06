// Copyright (c) 2015 Vivaldi Technologies AS. All rights reserved

#include "importer/chromium_profile_lock.h"
#include "base/files/file_util.h"


void ChromiumProfileLock::Init() {
}

void ChromiumProfileLock::Lock() {

}

void ChromiumProfileLock::Unlock() {

}

bool ChromiumProfileLock::HasAcquired() {
  return !base::IsLink(lock_file_);
}

bool ChromiumProfileLock::LockWithFcntl() {
  return false;
}
